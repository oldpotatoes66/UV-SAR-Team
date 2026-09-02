#!/usr/bin/env python3
"""Export, import and clone MR channels between SAR-TEAM K6 and K1 radios.

Only channel records, channel names and channel attributes are accessed.
Radio settings, TEAM identity and calibration are deliberately untouched.
"""

import argparse
import csv
import datetime as dt
import json
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    serial = None


XOR_TABLE = bytes((22, 108, 20, 230, 46, 145, 13, 64,
                   33, 53, 213, 64, 19, 3, 233, 128))
TIMESTAMP = b"\x6A\x39\x57\x64"
CHANNEL_COUNT = 200
RECORD_SIZE = 16
NAME_SIZE = 16
CSV_FIELDS = ("Slot", "Name", "FrequencyMHz", "Duplex", "OffsetMHz",
              "Mode", "Power", "ScanList", "Band", "RecordHex")

MODE_TABLE = ("FM", "NFM", "AM")
POWER_TABLE = ("USER", "LOW1", "LOW2", "LOW3", "LOW4", "LOW5", "MID", "HIGH")

MODELS = {
    "k6": {"records": 0x0000, "names": 0x0F50, "attrs": 0x0D60,
           "attr_size": 1, "channel_count": 200},
    "k1": {"records": 0x0000, "names": 0x4000, "attrs": 0x8000,
           "attr_size": 2, "channel_count": 1024},
}


class RadioError(RuntimeError):
    pass


def crc16_xmodem(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc <<= 1
            crc = (crc ^ 0x1021) & 0xFFFF if crc & 0x10000 else crc & 0xFFFF
    return crc


def xor_data(data):
    return bytes(byte ^ XOR_TABLE[index % len(XOR_TABLE)]
                 for index, byte in enumerate(data))


class Radio:
    def __init__(self, port, model, timeout=10):
        if serial is None:
            raise RadioError("pyserial is required: python3 -m pip install pyserial")
        self.serial = serial.Serial(port, 38400, timeout=timeout)
        self.model = model

    def close(self):
        self.serial.close()

    def send(self, payload):
        body = payload + struct.pack("<H", crc16_xmodem(payload))
        frame = struct.pack(">HBB", 0xABCD, len(payload), 0) + \
            xor_data(body) + struct.pack(">H", 0xDCBA)
        self.serial.reset_input_buffer()
        self.serial.write(frame)
        self.serial.flush()

    def receive(self):
        header = self.serial.read(4)
        if len(header) != 4 or header[:2] != b"\xAB\xCD" or header[3] != 0:
            raise RadioError("invalid or incomplete reply header")
        encrypted = self.serial.read(header[2])
        footer = self.serial.read(4)
        if len(encrypted) != header[2] or len(footer) != 4 or footer[2:] != b"\xDC\xBA":
            raise RadioError("invalid or incomplete reply frame")
        return xor_data(encrypted)

    def hello(self):
        self.send(b"\x14\x05\x04\x00" + TIMESTAMP)
        reply = self.receive()
        if reply.startswith(b"\x18\x05"):
            raise RadioError("radio is in bootloader mode; restart it normally")
        version = reply[4:28].split(b"\0", 1)[0].decode("ascii", "replace")
        if not version:
            raise RadioError("radio did not return a firmware version")
        return version

    def read(self, address, size):
        if not 1 <= size <= 16:
            raise ValueError("transfer size must be 1..16 bytes")
        last_reply = b""
        attempts = 3 if self.model == "k1" else 1
        for _attempt in range(attempts):
            self.send(b"\x1B\x05\x08\x00" +
                      struct.pack("<HBB", address, size, 0) + TIMESTAMP)
            try:
                reply = self.receive()
            except RadioError:
                reply = b""
            last_reply = reply
            if len(reply) == 8 + size and reply[:2] == b"\x1C\x05" and \
                    struct.unpack("<H", reply[4:6])[0] == address and reply[6] == size:
                if self.model == "k1":
                    time.sleep(0.03)
                return reply[8:]
            time.sleep(0.1)
        detail = last_reply.hex() if last_reply else "no reply"
        raise RadioError(f"invalid read reply at 0x{address:04X}: {detail}")

    def write(self, address, data):
        if not 1 <= len(data) <= 16:
            raise ValueError("transfer size must be 1..16 bytes")
        payload = b"\x1D\x05" + struct.pack("<BBHBB", 8 + len(data), 0,
                  address, len(data), 1) + TIMESTAMP + data
        self.send(payload)
        reply = self.receive()
        if len(reply) < 6 or reply[:2] != b"\x1E\x05" or \
                struct.unpack("<H", reply[4:6])[0] != address:
            raise RadioError(f"radio did not confirm write at 0x{address:04X}")
        if self.model == "k1":
            time.sleep(0.05)


def read_region(radio, address, size):
    output = bytearray()
    while len(output) < size:
        count = min(16, size - len(output))
        output.extend(radio.read(address + len(output), count))
    return bytes(output)


def read_channels(radio, model, count=CHANNEL_COUNT):
    layout = MODELS[model]
    print("Reading channel records...", flush=True)
    records = read_region(radio, layout["records"], count * RECORD_SIZE)
    print("Reading channel names...", flush=True)
    names = read_region(radio, layout["names"], count * NAME_SIZE)
    print("Reading channel attributes...", flush=True)
    attrs = read_region(radio, layout["attrs"],
                        count * layout["attr_size"])
    return {"records": records, "names": names, "attrs": attrs}


def name_decode(raw):
    return raw.split(b"\0", 1)[0].split(b"\xFF", 1)[0].decode("ascii", "replace").rstrip()


def name_encode(name, model):
    raw = name.strip()[:10].encode("ascii", "replace")
    fill = b"\0" if model == "k6" else b"\xFF"
    return raw.ljust(NAME_SIZE, fill)


def channel_empty(record):
    frequency = struct.unpack("<I", record[:4])[0]
    return frequency in (0, 0xFFFFFFFF)


def scan_decode(raw, model):
    if model == "k6":
        return ((raw[0] >> 7) & 1) | (((raw[0] >> 6) & 1) << 1)
    return raw[1]


def band_decode(raw, model):
    return raw[0] & 7


def scan_convert(value, target_model):
    if target_model == "k1":
        return (0, 1, 2, 25)[value] if 0 <= value <= 3 else min(value, 25)
    if value == 0:
        return 0
    if value == 1:
        return 1
    if value == 25:
        return 3
    return 2


def attr_encode(band, scan, model):
    band &= 7
    if model == "k1":
        return bytes((band, scan_convert(scan, "k1")))
    scan = scan_convert(scan, "k6")
    return bytes((band | ((scan & 1) << 7) | (((scan >> 1) & 1) << 6),))


def record_summary(record):
    frequency = struct.unpack("<I", record[0:4])[0] * 10
    offset = struct.unpack("<I", record[4:8])[0] * 10
    direction = record[0x0B] & 0x0F
    duplex = "+" if direction == 1 else "-" if direction == 2 else ""
    mode_index = ((record[0x0B] >> 4) & 0x0F) * 2 + ((record[0x0C] >> 1) & 1)
    mode = MODE_TABLE[mode_index] if mode_index < len(MODE_TABLE) else "UNKNOWN"
    power = (record[0x0C] >> 2) & 7
    return frequency / 1e6, duplex, offset / 1e6 if duplex else 0, mode, POWER_TABLE[power]


def rows_from_image(image, model):
    layout = MODELS[model]
    rows = []
    for index in range(CHANNEL_COUNT):
        record = image["records"][index * RECORD_SIZE:(index + 1) * RECORD_SIZE]
        if channel_empty(record):
            continue
        name_raw = image["names"][index * NAME_SIZE:(index + 1) * NAME_SIZE]
        attr_size = layout["attr_size"]
        attr = image["attrs"][index * attr_size:(index + 1) * attr_size]
        frequency, duplex, offset, mode, power = record_summary(record)
        rows.append({
            "Slot": index + 1, "Name": name_decode(name_raw),
            "FrequencyMHz": f"{frequency:.5f}", "Duplex": duplex,
            "OffsetMHz": f"{offset:.5f}", "Mode": mode, "Power": power,
            "ScanList": scan_decode(attr, model), "Band": band_decode(attr, model),
            "RecordHex": record.hex().upper(),
        })
    return rows


def write_csv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def read_csv(path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or not {"Slot", "Name", "ScanList", "Band", "RecordHex"}.issubset(rows[0]):
        raise ValueError("CSV must contain Slot, Name, ScanList, Band and RecordHex columns")
    seen = set()
    result = []
    for line, row in enumerate(rows, 2):
        slot = int(row["Slot"])
        if not 1 <= slot <= CHANNEL_COUNT or slot in seen:
            raise ValueError(f"invalid or duplicate Slot on CSV line {line}: {slot}")
        seen.add(slot)
        record = bytes.fromhex(row["RecordHex"].strip())
        if len(record) != RECORD_SIZE or channel_empty(record):
            raise ValueError(f"invalid channel record on CSV line {line}")
        scan, band = int(row["ScanList"]), int(row["Band"])
        if not 0 <= scan <= 25 or not 0 <= band <= 6:
            raise ValueError(f"invalid ScanList or Band on CSV line {line}")
        result.append((slot - 1, row["Name"], record, scan, band))
    return result


def save_backup(directory, model, firmware, image):
    directory.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = directory / f"channels-{model}-{stamp}.json"
    payload = {"model": model, "firmware": firmware,
               "records_hex": image["records"].hex(),
               "names_hex": image["names"].hex(),
               "attrs_hex": image["attrs"].hex()}
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return path


def program_rows(radio, model, rows, current_image):
    layout = MODELS[model]
    if model == "k1":
        records = bytearray(current_image["records"])
        names = bytearray(current_image["names"])
        attrs = bytearray(current_image["attrs"])
        changed = {"records": set(), "names": set(), "attrs": set()}
        for index, name, record, scan, band in rows:
            rec_offset = index * RECORD_SIZE
            name_offset = index * NAME_SIZE
            attr_offset = index * layout["attr_size"]
            records[rec_offset:rec_offset + RECORD_SIZE] = record
            names[name_offset:name_offset + NAME_SIZE] = name_encode(name, model)
            attrs[attr_offset:attr_offset + 2] = attr_encode(band, scan, model)
            changed["records"].update((rec_offset, rec_offset + 8))
            changed["names"].update((name_offset, name_offset + 8))
            changed["attrs"].add((attr_offset // 8) * 8)

        # The K1 compatibility EEPROM writer commits exactly eight bytes per
        # command, regardless of the request length. Always send complete
        # aligned blocks built from the target's backup image.
        blocks = []
        for key, image in (("records", records), ("names", names), ("attrs", attrs)):
            for offset in sorted(changed[key]):
                block = bytes(image[offset:offset + 8])
                address = layout[key] + offset
                blocks.append((address, block))
        for address, block in blocks:
            radio.write(address, block)
        for address, block in blocks:
            if radio.read(address, 8) != block:
                raise RadioError(f"read-back verification failed at 0x{address:04X}")
        for number, (index, name, _record, _scan, _band) in enumerate(rows, 1):
            print(f"[{number}/{len(rows)}] channel {index + 1}: {name or '(no name)'} OK")
        return

    for number, (index, name, record, scan, band) in enumerate(rows, 1):
        attr = attr_encode(band, scan, model)
        name_raw = name_encode(name, model)
        radio.write(layout["records"] + index * RECORD_SIZE, record)
        radio.write(layout["names"] + index * NAME_SIZE, name_raw)
        radio.write(layout["attrs"] + index * layout["attr_size"], attr)
        if radio.read(layout["records"] + index * RECORD_SIZE, RECORD_SIZE) != record or \
                radio.read(layout["names"] + index * NAME_SIZE, NAME_SIZE) != name_raw or \
                radio.read(layout["attrs"] + index * layout["attr_size"], len(attr)) != attr:
            raise RadioError(f"read-back verification failed at channel {index + 1}")
        print(f"[{number}/{len(rows)}] channel {index + 1}: {name or '(no name)'} OK")


def prune_channels(radio, model, keep, image):
    layout = MODELS[model]
    count = layout["channel_count"]
    occupied = []
    for index in range(keep, count):
        offset = index * RECORD_SIZE
        if not channel_empty(image["records"][offset:offset + RECORD_SIZE]):
            occupied.append(index)
    if not occupied and model != "k1":
        print(f"No occupied channels above {keep}; nothing to delete.")
        return []

    if model == "k1":
        records = bytearray(image["records"])
        names = bytearray(image["names"])
        attrs = bytearray(image["attrs"])
        blocks = {"records": set(), "names": set(), "attrs": set()}
        for index in occupied:
            record_offset = index * RECORD_SIZE
            name_offset = index * NAME_SIZE
            records[record_offset:record_offset + 16] = b"\xFF" * 16
            names[name_offset:name_offset + 16] = b"\xFF" * 16
            blocks["records"].update((record_offset, record_offset + 8))
            blocks["names"].update((name_offset, name_offset + 8))

        # K1 decides whether a slot is visible from the attribute band marker,
        # even when its record is already all 0xFF. Normalise every slot above
        # the keep boundary to the firmware's canonical empty marker.
        for index in range(keep, count):
            attr_offset = index * 2
            attrs[attr_offset:attr_offset + 2] = b"\x07\x00"
            blocks["attrs"].add((attr_offset // 8) * 8)
        writes = []
        for key, data in (("records", records), ("names", names), ("attrs", attrs)):
            for offset in sorted(blocks[key]):
                writes.append((layout[key] + offset, bytes(data[offset:offset + 8])))
        for address, block in writes:
            radio.write(address, block)
        for address, block in writes:
            if radio.read(address, 8) != block:
                raise RadioError(f"delete verification failed at 0x{address:04X}")
    else:
        for index in occupied:
            radio.write(layout["records"] + index * 16, b"\xFF" * 16)
            radio.write(layout["names"] + index * 16, b"\xFF" * 16)
            radio.write(layout["attrs"] + index, b"\xFF")
    return occupied


def open_radio(port, model):
    radio = Radio(port, model)
    try:
        firmware = radio.hello()
        print(f"Radio firmware: {firmware}")
        return radio, firmware
    except Exception:
        radio.close()
        raise


def main():
    parser = argparse.ArgumentParser(
        description="Batch channel tool for SAR-TEAM K6 and K1 radios")
    parser.add_argument("--port", required=True, help="radio serial port")
    parser.add_argument("--model", required=True, choices=MODELS)
    parser.add_argument("--backup-dir", type=Path, default=Path("channel-backups"))
    sub = parser.add_subparsers(dest="command", required=True)
    export = sub.add_parser("export", help="export occupied channels to CSV")
    export.add_argument("--csv", type=Path, required=True)
    import_cmd = sub.add_parser("import", help="backup, write and verify CSV channels")
    import_cmd.add_argument("--csv", type=Path, required=True)
    prune = sub.add_parser("prune", help="delete occupied channels above a slot")
    prune.add_argument("--keep", type=int, required=True,
                       help="keep slots 1 through this number")
    args = parser.parse_args()

    radio, firmware = open_radio(args.port, args.model)
    try:
        count = MODELS[args.model]["channel_count"] if args.command == "prune" else CHANNEL_COUNT
        image = read_channels(radio, args.model, count)
        if args.command == "export":
            rows = rows_from_image(image, args.model)
            write_csv(args.csv, rows)
            print(f"Exported {len(rows)} occupied channels: {args.csv}")
        elif args.command == "import":
            rows = read_csv(args.csv)
            backup = save_backup(args.backup_dir, args.model, firmware, image)
            print(f"Full channel-area backup: {backup}")
            print(f"Writing {len(rows)} channels; other channel slots are unchanged.")
            program_rows(radio, args.model, rows, image)
            print("All requested channels were written and verified.")
        else:
            if not 1 <= args.keep <= count:
                raise ValueError(f"--keep must be between 1 and {count}")
            backup = save_backup(args.backup_dir, args.model, firmware, image)
            print(f"Full channel-area backup: {backup}")
            removed = prune_channels(radio, args.model, args.keep, image)
            slots = ", ".join(str(index + 1) for index in removed)
            print(f"Deleted {len(removed)} occupied channel(s) above {args.keep}: {slots or 'none'}")
    finally:
        radio.close()
    print("Power-cycle the radio before using the new channel list.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RadioError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
