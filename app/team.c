#include "team.h"

#ifdef ENABLE_TEAM_MODE

#include "../audio.h"
#include "../board.h"
#include "../dcs.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
#include "../driver/eeprom.h"
#include "../driver/gpio.h"
#include "../driver/keyboard.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../external/printf/printf.h"
#include "../frequencies.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/helper.h"
#include "../ui/main.h"
#include <stdint.h>
#include <string.h>

#define TEAM_WEAK_TICKS 3500u
#define TEAM_LOST_TICKS 6000u
#define TEAM_LOST_REPEAT_TICKS 6000u
#define TEAM_TX_DURATION_TICKS 100u
#define TEAM_TX_ARM_DELAY_TICKS 300u
#define TEAM_PTT_TIMEOUT_TICKS 6000u
#define TEAM_CW_INTERVAL_TICKS 60000u
#define TEAM_CW_DOT_MS 60u
#define TEAM_CONFIG_ADDRESS 0x1FF8u
#define TEAM_CONFIG_TAG 0xA0u
#define TEAM_CONFIG_TAG_MASK 0xE0u
#define TEAM_CONFIG_INTERVAL_15 (1u << 0)
#define TEAM_CONFIG_POWER_SHIFT 1u
#define TEAM_CONFIG_ALERT (1u << 3)
#define TEAM_CONFIG_CW (1u << 4)
#define TEAM_CALLSIGN_LEN 6u

typedef struct {
    char callSign[TEAM_CALLSIGN_LEN + 1];
    uint8_t txInterval;
    uint8_t powerLevel;
    bool alertSound;
    bool cwDefault;
    bool valid;
} TEAM_Config_t;

static bool TEAM_DelayCanExit(uint16_t delayMs);
static const char *TEAM_Feedback;
static uint8_t TEAM_FeedbackTicks;

static void TEAM_SetFeedback(const char *text)
{
    TEAM_Feedback = text;
    TEAM_FeedbackTicks = 100; // Keep the confirmation visible for one second.
}

static void TEAM_UpdateBattery(void)
{
    BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4],
                             &gBatteryCurrent);
    BATTERY_GetReadings(false);
}

static bool TEAM_BatteryAllowsTx(void)
{
    // Preserve the final few percent for reception and local alarms.
    return gBatteryDisplayLevel > 1 && gBatteryDisplayLevel <= 6;
}

static uint8_t TEAM_ConfigCrc(const uint8_t *data)
{
    uint8_t crc = 0;

    for (uint8_t i = 0; i < 7; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) :
                                (uint8_t)(crc << 1);
    }
    return crc;
}

static TEAM_Config_t TEAM_LoadConfig(void)
{
    uint8_t raw[8];
    TEAM_Config_t config = {{0}, 25, 3, true, false, false};
    uint8_t length = 0;
    uint8_t power;

    EEPROM_ReadBuffer(TEAM_CONFIG_ADDRESS, raw, sizeof(raw));
    power = (raw[6] >> TEAM_CONFIG_POWER_SHIFT) & 3u;
    if ((raw[6] & TEAM_CONFIG_TAG_MASK) != TEAM_CONFIG_TAG ||
        raw[7] != TEAM_ConfigCrc(raw) || power > 2)
        return config;

    for (uint8_t i = 0; i < TEAM_CALLSIGN_LEN; i++) {
        char c = (char)raw[i];
        if (c == ' ' || c == '\0' || c == (char)0xff)
            break;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return config;
        config.callSign[length++] = c;
    }
    if (length < 3)
        return config;

    config.callSign[length] = '\0';
    config.txInterval = raw[6] & TEAM_CONFIG_INTERVAL_15 ? 15 : 25;
    config.powerLevel = power + 1;
    config.alertSound = (raw[6] & TEAM_CONFIG_ALERT) != 0;
    config.cwDefault = (raw[6] & TEAM_CONFIG_CW) != 0;
    config.valid = true;
    return config;
}

static void TEAM_ConfigureReceiver(uint8_t dcsCode)
{
    DCS_CodeType_t oldCodeType = gTxVfo->pRX->CodeType;
    uint8_t oldCode = gTxVfo->pRX->Code;

    // Use the normal radio setup path so every BK4819 DCS detector register is
    // initialized exactly as it is during ordinary coded reception. Restore the
    // channel data immediately: TEAM mode must not alter EEPROM configuration.
    gTxVfo->pRX->CodeType = CODE_TYPE_DIGITAL;
    gTxVfo->pRX->Code = dcsCode;
    RADIO_SetupRegisters(true);
    gTxVfo->pRX->CodeType = oldCodeType;
    gTxVfo->pRX->Code = oldCode;
    BK4819_WriteRegister(BK4819_REG_3F,
        BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST |
        BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

static void TEAM_SetReceiveAudio(bool enabled)
{
    if (enabled) {
        // RADIO_SetupRegisters leaves REG_47 routed to MUTE. The speaker GPIO
        // alone is therefore insufficient; restore the FM demodulator first.
        RADIO_SetModulation(gTxVfo->Modulation);
        AUDIO_AudioPathOn();
    } else {
        BK4819_SetAF(BK4819_AF_MUTE);
        AUDIO_AudioPathOff();
    }
}

static bool TEAM_PlayAlert(bool recovered, uint8_t dcsCode, bool playSound)
{
    uint8_t count = recovered ? 2 : 3;
    uint16_t tone = recovered ? 1000 : 440;
    bool completed = true;

    // These are local speaker tones only. Rebuild the receive path afterwards
    // because the tone generator temporarily replaces BK4819 RX audio routing.
    if (playSound) {
        BK4819_PlayTone(tone, true);
        AUDIO_AudioPathOn();
    }
    while (count--) {
        if (!recovered)
            GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
        if (playSound)
            BK4819_ExitTxMute();
        if (!TEAM_DelayCanExit(recovered ? 100 : 450)) {
            completed = false;
            if (playSound)
                BK4819_EnterTxMute();
            GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
            break;
        }
        if (playSound)
            BK4819_EnterTxMute();
        if (!recovered)
            GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
        if (count && !TEAM_DelayCanExit(recovered ? 100 : 150)) {
            completed = false;
            break;
        }
    }
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
    if (playSound) {
        AUDIO_AudioPathOff();
        BK4819_TurnsOffTones_TurnsOnRX();
        TEAM_ConfigureReceiver(dcsCode);
    }
    return completed;
}

static void TEAM_Render(bool seen, uint16_t ageTicks, uint16_t carrierTicks,
                        int lastDbm, uint8_t dcsCode, uint32_t frequency,
                        bool autoTx, bool transmitting, bool txAllowed,
                        uint16_t txCountdown, uint8_t txInterval,
                        uint8_t powerLevel, bool alertSound,
                        bool cwEnabled, bool cwTransmitting,
                        const TEAM_Config_t *config)
{
    char text[24];
    const char *state = !TEAM_BatteryAllowsTx() ? "LOW BAT TX OFF" :
        (cwTransmitting ? "CW ID TX" :
        (transmitting ? "TRANSMITTING" :
        (!txAllowed && autoTx ? "TX BLOCKED" :
        (!seen ? (carrierTicks < 300 ? "RF NO DCS" : "NO LINK") :
        (ageTicks < TEAM_WEAK_TICKS ? "LINK OK" :
        (ageTicks < TEAM_LOST_TICKS ? "LINK WEAK" : "LINK LOST"))))));

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    sprintf(text, "%u.%05u", frequency / 100000, frequency % 100000);
    UI_PrintStringSmallBold(text, 0, 127, 0);
    UI_PrintStringSmallBold(autoTx ? "TEAM AUTO TX" : "TEAM RX ONLY", 18, 110, 1);
    if (TEAM_FeedbackTicks) {
        UI_PrintStringSmallBold(TEAM_Feedback, 10, 118, 2);
    } else {
        UI_PrintStringSmallBold(state, 24, 112, 2);
    }
    if (config->valid) {
        sprintf(text, "%s %s", cwEnabled ? "CW" : "ID", config->callSign);
        UI_PrintStringSmallBold(text, 2, 72, 3);
    } else {
        UI_PrintStringSmallBold("ID NOT SET", 2, 72, 3);
    }
    sprintf(text, "B%u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
    UI_PrintStringSmallBold(text, 86, 127, 3);

    // DCS numbers use octal digits. DCS_Options stores their numeric value, so
    // 0x13 (decimal 19) must be rendered as octal 023.
    sprintf(text, "DCS %03oN", DCS_Options[dcsCode]);
    UI_PrintStringSmallBold(text, 2, 60, 4);
    if (seen) {
        sprintf(text, "%d dBm", lastDbm);
        UI_PrintStringSmallBold(text, 68, 127, 4);
        sprintf(text, "T%us", ageTicks / 100);
        UI_PrintStringSmallBold(text, 2, 58, 6);
    }
    sprintf(text, "PWR P%u", powerLevel);
    UI_PrintStringSmallBold(text, 2, 52, 5);
    if (autoTx) {
        sprintf(text, "N%u I%u %c", (txCountdown + 99) / 100,
                txInterval, alertSound ? 'B' : 'M');
    } else {
        sprintf(text, "I%u %c", txInterval, alertSound ? 'B' : 'M');
    }
    UI_PrintStringSmallBold(text, 60, 127, 5);
    UI_PrintStringSmallBold(gEeprom.KEY_LOCK ? "LOCK EXIT" : "EXIT",
                            gEeprom.KEY_LOCK ? 74 : 96, 127, 6);
    ST7565_BlitFullScreen();
}

static void TEAM_BeginTransmit(uint32_t frequency, uint8_t txBias)
{
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
    BK4819_SetFrequency(frequency);
    BK4819_SetFilterBandwidth(gTxVfo->CHANNEL_BANDWIDTH, false);
    BK4819_PrepareTransmit();
    SYSTEM_DelayMs(10);
    BK4819_PickRXFilterPathBasedOnFrequency(frequency);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true);
    SYSTEM_DelayMs(5);
    BK4819_SetupPowerAmplifier(txBias, frequency);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
}

static void TEAM_EndTransmit(uint8_t dcsCode)
{
    BK4819_EnterTxMute();
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    TEAM_ConfigureReceiver(dcsCode);
}

static bool TEAM_DelayCanExit(uint16_t delayMs)
{
    while (delayMs) {
        if (KEYBOARD_Poll() == KEY_EXIT)
            return false;
        SYSTEM_DelayMs(10);
        delayMs = delayMs > 10 ? delayMs - 10 : 0;
    }
    return true;
}

bool TEAM_TransmitCarrier(uint8_t dcsCode, uint32_t frequency,
                          uint8_t txBias, uint16_t durationTicks)
{
    TEAM_BeginTransmit(frequency, txBias);
    BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CODE_TYPE_DIGITAL, dcsCode));

    for (uint16_t ticks = 0; ticks < durationTicks; ticks++) {
        if (KEYBOARD_Poll() == KEY_EXIT) {
            TEAM_EndTransmit(dcsCode);
            return false;
        }
        SYSTEM_DelayMs(10);
    }

    TEAM_EndTransmit(dcsCode);
    return true;
}

static bool TEAM_TransmitVoice(uint8_t dcsCode, uint32_t frequency,
                               uint8_t txBias)
{
    uint16_t timeoutTicks = TEAM_PTT_TIMEOUT_TICKS;

    TEAM_BeginTransmit(frequency, txBias);
    BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CODE_TYPE_DIGITAL, dcsCode));

    while (!GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT) && timeoutTicks--) {
        if (KEYBOARD_Poll() == KEY_EXIT) {
            TEAM_EndTransmit(dcsCode);
            return false;
        }
        SYSTEM_DelayMs(10);
    }

    TEAM_EndTransmit(dcsCode);
    return true;
}

static const char *TEAM_MorseCode(char c)
{
    static const char *const letters[] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..",
        ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.",
        "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."
    };
    static const char *const digits[] = {
        "-----", ".----", "..---", "...--", "....-",
        ".....", "-....", "--...", "---..", "----."
    };

    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    return 0;
}

bool TEAM_TransmitCwId(uint8_t dcsCode, uint32_t frequency,
                       uint8_t txBias, const char *id)
{
    TEAM_BeginTransmit(frequency, txBias);
    BK4819_TransmitTone(false, 700);
    BK4819_EnterTxMute();
    if (!TEAM_DelayCanExit(TEAM_CW_DOT_MS * 3))
        goto aborted;

    while (*id) {
        const char *code = TEAM_MorseCode(*id++);

        if (!code)
            continue;
        while (*code) {
            BK4819_ExitTxMute();
            if (!TEAM_DelayCanExit(TEAM_CW_DOT_MS * (*code++ == '-' ? 3 : 1)))
                goto aborted;
            BK4819_EnterTxMute();
            if (*code && !TEAM_DelayCanExit(TEAM_CW_DOT_MS))
                goto aborted;
        }
        if (*id && !TEAM_DelayCanExit(TEAM_CW_DOT_MS * 3))
            goto aborted;
    }

    TEAM_EndTransmit(dcsCode);
    return true;

aborted:
    TEAM_EndTransmit(dcsCode);
    return false;
}

bool TEAM_GetConfiguredCallSign(char callSign[7])
{
    const TEAM_Config_t config = TEAM_LoadConfig();
    if (!config.valid)
        return false;
    memcpy(callSign, config.callSign, sizeof(config.callSign));
    return true;
}

void TEAM_Run(void)
{
    TEAM_Config_t config = TEAM_LoadConfig();
    bool seen = false;
    uint8_t dcsCode = gTxVfo->pRX->CodeType == CODE_TYPE_DIGITAL
        ? gTxVfo->pRX->Code : 0; // DCS_Options[0] is D023N (octal 023).
    uint32_t frequency = gTxVfo->pRX->Frequency;
    uint16_t ageTicks = 0;
    uint16_t carrierTicks = 0xFFFF;
    uint8_t renderTicks = 0;
    bool lostAlerted = false;
    bool autoTx = false;
    bool transmitting = false;
    bool cwEnabled = config.valid && config.cwDefault;
    bool cwTransmitting = false;
    bool rxAudioOn = false;
    bool alertSound = config.alertSound;
    bool txAllowed = gTxVfo->Modulation == MODULATION_FM &&
                     TX_freq_check(frequency) == 0;
    uint8_t txInterval = config.txInterval;
    uint16_t txCountdown = 0;
    uint16_t cwCountdown = 0;
    uint16_t lostRepeatCountdown = 0;
    // P3 matches the capped low-power bias used by the t0.7 build that was
    // verified with both VX-6R and VX-8R. P1/P2 are optional close-range modes.
    uint8_t powerLevel = config.powerLevel;
    uint8_t txBias = powerLevel * 5;
    uint8_t fHoldTicks = 0;
    bool fHoldHandled = false;
    uint8_t key3HoldTicks = 0;
    bool key3HoldHandled = false;
    bool pttTimedOut = false;
    uint8_t batteryTicks = 0;
    KEY_Code_t previousKey = KEY_INVALID;
    int lastDbm = -160;
    uint16_t oldInterruptMask = BK4819_ReadRegister(BK4819_REG_3F);
    VFO_Info_t *oldRxVfo = gRxVfo;
    VFO_Info_t *oldCurrentVfo = gCurrentVfo;

    AUDIO_AudioPathOff();
    for (uint8_t i = 0; i < 4; i++)
        TEAM_UpdateBattery();
    txAllowed = txAllowed && TEAM_BatteryAllowsTx();
    gRxVfo = gTxVfo;
    gCurrentVfo = gTxVfo;
    TEAM_ConfigureReceiver(dcsCode);
    TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode, frequency,
                autoTx, transmitting, txAllowed, txCountdown, txInterval,
                powerLevel, alertSound, cwEnabled, cwTransmitting, &config);

    while (1) {
        KEY_Code_t key = KEYBOARD_Poll();

        if (key == KEY_EXIT && previousKey != KEY_EXIT)
            break;

        // TEAM has its own event loop, so reproduce the normal long-F keypad
        // lock gesture here. PTT and EXIT deliberately remain available for
        // field safety even while the configuration keys are locked.
        if (key == KEY_F) {
            if (!fHoldHandled && fHoldTicks < 100 && ++fHoldTicks >= 100) {
                gEeprom.KEY_LOCK = !gEeprom.KEY_LOCK;
                gRequestSaveSettings = true;
                fHoldHandled = true;
                TEAM_SetFeedback(gEeprom.KEY_LOCK ? "KEY LOCKED" : "KEY UNLOCKED");
                renderTicks = 10;
            }
        } else {
            fHoldTicks = 0;
            fHoldHandled = false;
        }

        // TEAM LINK is a normal voice operating mode with ARTS supervision in
        // the background. Manual PTT always takes priority over scheduled
        // polls; DCS remains present so Yaesu ARTS continues to recognize us.
        if (!GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT)) {
            if (!pttTimedOut && txAllowed) {
                if (rxAudioOn) {
                    TEAM_SetReceiveAudio(false);
                    rxAudioOn = false;
                }
                TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode,
                            frequency, autoTx, true, txAllowed, txCountdown,
                            txInterval, powerLevel, alertSound, cwEnabled,
                            cwTransmitting, &config);
                if (!TEAM_TransmitVoice(dcsCode, frequency, txBias))
                    break;
                if (!GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT)) {
                    TEAM_SetFeedback("TX TIMEOUT 60S");
                    pttTimedOut = true;
                }
                carrierTicks = 0xFFFF;
                if (autoTx)
                    txCountdown = (uint16_t)txInterval * 100;
            } else if (!pttTimedOut && previousKey != KEY_PTT) {
                TEAM_SetFeedback(TEAM_BatteryAllowsTx()
                    ? "TX BLOCKED" : "LOW BAT TX OFF");
            }
            previousKey = KEY_PTT;
            SYSTEM_DelayMs(10);
            continue;
        }
        pttTimedOut = false;

        // AUTO TX is deliberately a long-press action. A brief accidental
        // press must never arm an unattended transmitter in the field.
        if (!gEeprom.KEY_LOCK && key == KEY_3) {
            if (!key3HoldHandled && key3HoldTicks < 100 && ++key3HoldTicks >= 100) {
                autoTx = !autoTx;
                txCountdown = autoTx ? TEAM_TX_ARM_DELAY_TICKS : 0;
                TEAM_SetFeedback(autoTx ? "AUTO TX ARMED" : "AUTO TX OFF");
                key3HoldHandled = true;
            }
        } else {
            key3HoldTicks = 0;
            key3HoldHandled = false;
        }
        if (!gEeprom.KEY_LOCK && key == KEY_2 && previousKey != KEY_2) {
            txInterval = txInterval == 25 ? 15 : 25;
            if (autoTx)
                txCountdown = (uint16_t)txInterval * 100;
            TEAM_SetFeedback(txInterval == 25 ? "POLL 25 SEC" : "POLL 15 SEC");
        }
        if (!gEeprom.KEY_LOCK && key == KEY_1 && previousKey != KEY_1) {
            alertSound = !alertSound;
            TEAM_SetFeedback(alertSound ? "BEEP ON" : "BEEP OFF");
        }
        if (!gEeprom.KEY_LOCK && key == KEY_4 && previousKey != KEY_4) {
            powerLevel = powerLevel == 3 ? 1 : powerLevel + 1;
            txBias = powerLevel * 5;
            TEAM_SetFeedback(powerLevel == 1 ? "POWER P1" :
                             (powerLevel == 2 ? "POWER P2" : "POWER P3"));
        }
        if (!gEeprom.KEY_LOCK && key == KEY_5 && previousKey != KEY_5) {
            if (config.valid) {
                cwEnabled = !cwEnabled;
                cwCountdown = cwEnabled ? TEAM_TX_ARM_DELAY_TICKS : 0;
                TEAM_SetFeedback(cwEnabled ? "CW ON" : "CW OFF");
            } else {
                TEAM_SetFeedback("ID NOT SET");
            }
        }
        previousKey = key;

        if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
            uint16_t interrupts;

            BK4819_WriteRegister(BK4819_REG_02, 0);
            interrupts = BK4819_ReadRegister(BK4819_REG_02);
            if (interrupts & BK4819_REG_02_CDCSS_FOUND) {
                uint16_t rssi = BK4819_GetRSSI();
                bool recovered = lostAlerted;

                seen = true;
                ageTicks = 0;
                lostAlerted = false;
                lostRepeatCountdown = 0;
                lastDbm = (rssi / 2) - 160 + dBmCorrTable[gTxVfo->Band];
                if (recovered && alertSound && !TEAM_PlayAlert(true, dcsCode, true))
                    break;
            }
            // Voice follows carrier squelch, while DCS independently updates
            // ARTS link state. This avoids clipping or suppressing speech when
            // the DCS detector needs extra time to synchronize.
            if (interrupts & BK4819_REG_02_SQUELCH_LOST) {
                if (!rxAudioOn) {
                    TEAM_SetReceiveAudio(true);
                    rxAudioOn = true;
                }
            }
            if (interrupts & BK4819_REG_02_SQUELCH_FOUND) {
                if (rxAudioOn) {
                    TEAM_SetReceiveAudio(false);
                    rxAudioOn = false;
                }
            }
            if (interrupts & BK4819_REG_02_SQUELCH_LOST) {
                uint16_t rssi = BK4819_GetRSSI();

                carrierTicks = 0;
                lastDbm = (rssi / 2) - 160 + dBmCorrTable[gTxVfo->Band];
            }
        }

        if (seen && ageTicks < 65000)
            ageTicks++;
        if (seen && !lostAlerted && ageTicks >= TEAM_LOST_TICKS) {
            lostAlerted = true;
            lostRepeatCountdown = TEAM_LOST_REPEAT_TICKS;
            if (!TEAM_PlayAlert(false, dcsCode, alertSound))
                break;
        }
        if (lostAlerted) {
            if (lostRepeatCountdown)
                lostRepeatCountdown--;
            else {
                lostRepeatCountdown = TEAM_LOST_REPEAT_TICKS;
                if (!TEAM_PlayAlert(false, dcsCode, alertSound))
                    break;
            }
        }
        if (carrierTicks < 65000)
            carrierTicks++;
        if (autoTx && txAllowed) {
            if (txCountdown)
                txCountdown--;
            else if (carrierTicks < 200) {
                // Avoid transmitting over a poll which has just been received.
                txCountdown = 100;
            } else {
                if (rxAudioOn) {
                    TEAM_SetReceiveAudio(false);
                    rxAudioOn = false;
                }
                transmitting = true;
                TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode,
                            frequency, autoTx, transmitting, txAllowed,
                            txCountdown, txInterval, powerLevel, alertSound,
                            cwEnabled, cwTransmitting, &config);
                if (!TEAM_TransmitCarrier(dcsCode, frequency, txBias,
                                          TEAM_TX_DURATION_TICKS))
                    break;
                transmitting = false;
                carrierTicks = 0xFFFF;
                txCountdown = (uint16_t)txInterval * 100;
                if (cwEnabled && !cwCountdown)
                    cwCountdown = TEAM_TX_ARM_DELAY_TICKS;
            }
        }
        if (autoTx && cwEnabled && txAllowed) {
            if (cwCountdown)
                cwCountdown--;
            else if (carrierTicks >= 200) {
                if (rxAudioOn) {
                    TEAM_SetReceiveAudio(false);
                    rxAudioOn = false;
                }
                cwTransmitting = true;
                TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode,
                            frequency, autoTx, transmitting, txAllowed,
                            txCountdown, txInterval, powerLevel, alertSound,
                            cwEnabled, cwTransmitting, &config);
                if (!TEAM_TransmitCwId(dcsCode, frequency, txBias,
                                       config.callSign))
                    break;
                cwTransmitting = false;
                carrierTicks = 0xFFFF;
                cwCountdown = TEAM_CW_INTERVAL_TICKS;
            }
        }
        if (++renderTicks >= 10) {
            renderTicks = 0;
            if (TEAM_FeedbackTicks)
                TEAM_FeedbackTicks = TEAM_FeedbackTicks > 10
                    ? TEAM_FeedbackTicks - 10 : 0;
            TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode,
                        frequency, autoTx, transmitting, txAllowed,
                        txCountdown, txInterval, powerLevel, alertSound,
                        cwEnabled, cwTransmitting, &config);
        }
        if (++batteryTicks >= 50) {
            batteryTicks = 0;
            TEAM_UpdateBattery();
            txAllowed = gTxVfo->Modulation == MODULATION_FM &&
                        TX_freq_check(frequency) == 0 && TEAM_BatteryAllowsTx();
            if (!txAllowed && autoTx) {
                autoTx = false;
                txCountdown = 0;
                TEAM_SetFeedback(TEAM_BatteryAllowsTx()
                    ? "TX BLOCKED" : "LOW BAT TX OFF");
            }
        }
        SYSTEM_DelayMs(10);
    }

    TEAM_SetReceiveAudio(false);
    BK4819_WriteRegister(BK4819_REG_3F, oldInterruptMask);
    gRxVfo = oldRxVfo;
    gCurrentVfo = oldCurrentVfo;
    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

#endif
