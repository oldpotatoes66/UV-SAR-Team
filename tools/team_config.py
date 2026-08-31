#!/usr/bin/env python3
"""Configure SAR-TEAM callsign and ARTS defaults in UV-K5/K6 EEPROM."""

import argparse
import csv
import datetime as dt
import json
import re
import struct
import sys
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None


EEPROM_ADDRESS = 0x1FF8
CONFIG_TAG = 0xA0
CONFIG_TAG_MASK = 0xE0
CALLSIGN_LEN = 6
XOR_TABLE = bytes((22, 108, 20, 230, 46, 145, 13, 64,
                   33, 53, 213, 64, 19, 3, 233, 128))


class RadioError(RuntimeError):
    pass


def crc16_xmodem(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc <<= 1
            if crc & 0x10000:
                crc = (crc ^ 0x1021) & 0xFFFF
    return crc


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def xor_data(data):
    return bytes(byte ^ XOR_TABLE[index % len(XOR_TABLE)]
                 for index, byte in enumerate(data))


class Radio:
    def __init__(self, port):
        if serial is None:
            raise RadioError("pyserial is required: python3 -m pip install pyserial")
        self.serial = serial.Serial(port, 38400, timeout=1)

    def close(self):
        self.serial.close()

    def send(self, payload):
        body = payload + struct.pack("<H", crc16_xmodem(payload))
        frame = struct.pack(">HBB", 0xABCD, len(payload), 0) + \
            xor_data(body) + struct.pack(">H", 0xDCBA)
        self.serial.write(frame)
        self.serial.flush()

    def receive(self):
        header = self.serial.read(4)
        if len(header) != 4 or header[:2] != b"\xAB\xCD" or header[3] != 0:
            raise RadioError("invalid or incomplete reply header")
        payload = self.serial.read(header[2])
        footer = self.serial.read(4)
        if len(payload) != header[2] or len(footer) != 4 or footer[2:] != b"\xDC\xBA":
            raise RadioError("invalid or incomplete reply frame")
        return xor_data(payload)

    def hello(self):
        self.send(b"\x14\x05\x04\x00\x6A\x39\x57\x64")
        reply = self.receive()
        if reply.startswith(b"\x18\x05"):
            raise RadioError("radio is in bootloader mode; restart it normally")
        version = reply[4:28].split(b"\0", 1)[0].decode("ascii", "replace")
        if not version:
            raise RadioError("radio did not return a firmware version")
        return version

    def read_config(self):
        payload = b"\x1B\x05\x08\x00" + \
            struct.pack("<HBB", EEPROM_ADDRESS, 8, 0) + b"\x6A\x39\x57\x64"
        self.send(payload)
        reply = self.receive()
        data = reply[8:16]
        if len(data) != 8:
            raise RadioError("configuration read returned the wrong length")
        return data

    def write_config(self, data):
        if len(data) != 8:
            raise ValueError("configuration must be exactly 8 bytes")
        payload = b"\x1D\x05" + struct.pack("<BBHBB", 16, 0,
                  EEPROM_ADDRESS, 8, 1) + b"\x6A\x39\x57\x64" + data
        self.send(payload)
        reply = self.receive()
        if len(reply) < 6 or reply[0] != 0x1E or \
                reply[4] != (EEPROM_ADDRESS & 0xFF) or \
                reply[5] != (EEPROM_ADDRESS >> 8):
            raise RadioError("radio did not confirm the EEPROM write")


def encode_config(callsign, interval, power, alerts, cw):
    callsign = callsign.strip().upper()
    if not re.fullmatch(r"[A-Z0-9]{3,6}", callsign):
        raise ValueError("callsign must contain 3-6 uppercase letters or digits")
    flags = CONFIG_TAG
    if interval == 15:
        flags |= 1
    if power not in (1, 2, 3):
        raise ValueError("power must be 1, 2, or 3")
    flags |= (power - 1) << 1
    if alerts:
        flags |= 1 << 3
    if cw:
        flags |= 1 << 4
    data = callsign.encode("ascii").ljust(CALLSIGN_LEN, b" ") + bytes((flags,))
    return data + bytes((crc8(data),))


def decode_config(data):
    flags = data[6]
    valid = (flags & CONFIG_TAG_MASK) == CONFIG_TAG and data[7] == crc8(data[:7])
    power = ((flags >> 1) & 3) + 1
    callsign = data[:6].decode("ascii", "replace").rstrip(" \0\xff")
    if power > 3 or not re.fullmatch(r"[A-Z0-9]{3,6}", callsign):
        valid = False
    return {
        "valid": valid,
        "callsign": callsign if valid else None,
        "interval": 15 if flags & 1 else 25,
        "power": power if power <= 3 else None,
        "alerts": bool(flags & (1 << 3)),
        "cw_default": bool(flags & (1 << 4)),
        "raw_hex": data.hex(),
    }


def save_backup(directory, firmware, data, label=""):
    directory.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    suffix = "-" + re.sub(r"[^A-Za-z0-9_-]+", "_", label) if label else ""
    path = directory / f"team-config-{stamp}{suffix}.json"
    record = {"firmware": firmware, "address": hex(EEPROM_ADDRESS),
              "configuration": decode_config(data)}
    path.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")
    return path


def on_off(value):
    value = value.strip().lower()
    if value not in ("on", "off"):
        raise ValueError("boolean setting must be 'on' or 'off'")
    return value == "on"


def main():
    parser = argparse.ArgumentParser(
        description="Configure SAR-TEAM callsign and ARTS defaults")
    parser.add_argument("--port", required=True,
                        help="serial port, e.g. /dev/cu.usbserial-0001")
    parser.add_argument("--backup-dir", type=Path,
                        default=Path("team-config-backups"))
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("show", help="read and display the current configuration")
    configure = sub.add_parser("configure", help="write and verify configuration")
    configure.add_argument("--callsign", required=True)
    configure.add_argument("--interval", type=int, choices=(15, 25), default=25)
    configure.add_argument("--power", type=int, choices=(1, 2, 3), default=3)
    configure.add_argument("--alerts", choices=("on", "off"), default="on")
    configure.add_argument("--cw", choices=("on", "off"), default="off")
    clear = sub.add_parser("clear", help="remove the callsign and disable CW")
    clear.add_argument("--yes", action="store_true", help="confirm configuration removal")
    batch = sub.add_parser("batch", help="configure multiple radios from a CSV roster")
    batch.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()

    if args.command == "clear" and not args.yes:
        parser.error("clear requires --yes")

    def program(wanted=None, label=""):
        radio = Radio(args.port)
        try:
            firmware = radio.hello()
            current = radio.read_config()
            print(f"Radio firmware: {firmware}")
            if wanted is None:
                print(json.dumps(decode_config(current), indent=2,
                                 ensure_ascii=False))
                return
            backup = save_backup(args.backup_dir, firmware, current, label)
            print(f"Backup: {backup}")
            radio.write_config(wanted)
            actual = radio.read_config()
            if actual != wanted:
                raise RadioError(
                    f"read-back mismatch: {actual.hex()} != {wanted.hex()}")
            print("Verified:")
            print(json.dumps(decode_config(actual), indent=2,
                             ensure_ascii=False))
        finally:
            radio.close()

    if args.command == "show":
        program()
    elif args.command == "configure":
        program(encode_config(args.callsign, args.interval, args.power,
                              on_off(args.alerts), on_off(args.cw)))
    elif args.command == "clear":
        program(b"\xFF" * 8)
    else:
        with args.csv.open(newline="", encoding="utf-8-sig") as stream:
            rows = list(csv.DictReader(stream))
        required = {"Label", "Callsign", "Interval", "Power", "Alerts", "CW"}
        if not rows or not required.issubset(rows[0]):
            raise ValueError("CSV columns must be: " + ", ".join(sorted(required)))
        for index, row in enumerate(rows, 1):
            label = row["Label"].strip() or f"radio-{index}"
            wanted = encode_config(
                row["Callsign"], int(row["Interval"]), int(row["Power"]),
                on_off(row["Alerts"]), on_off(row["CW"]))
            answer = input(f"[{index}/{len(rows)}] Connect and power on {label} "
                           f"({row['Callsign']}), then press Enter; q quits: ")
            if answer.strip().lower() == "q":
                break
            program(wanted, label)
            print("Power off this radio and connect the next one.\n")
    print("Power-cycle the configured radio(s) before entering TEAM mode.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RadioError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
