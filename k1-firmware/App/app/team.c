#include "team.h"

#ifdef ENABLE_TEAM_MODE

#include "audio.h"
#include "board.h"
#include "dcs.h"
#include "driver/bk4819-regs.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEAM_WEAK_TICKS        3500u
#define TEAM_LOST_TICKS        6000u
#define TEAM_ALERT_REPEAT      6000u
#define TEAM_POLL_INTERVAL     2500u
#define TEAM_POLL_DURATION     100u
#define TEAM_AUTO_ARM_DELAY    300u
#define TEAM_PTT_TIMEOUT       6000u
#define TEAM_CONFIG_ADDRESS    0xD000u
#define TEAM_CONFIG_TAG        0xA0u
#define TEAM_CONFIG_TAG_MASK   0xE0u
#define TEAM_CONFIG_INTERVAL15 (1u << 0)
#define TEAM_CONFIG_ALERT      (1u << 3)
#define TEAM_CONFIG_CW         (1u << 4)
#define TEAM_CALLSIGN_LEN      6u
#define TEAM_CW_INTERVAL       60000u
#define TEAM_CW_DOT_MS         60u

static const char *gTeamFeedback;
static uint8_t gTeamFeedbackTicks;

static void TEAM_SetFeedback(const char *text)
{
    gTeamFeedback = text;
    gTeamFeedbackTicks = 100;
}

typedef struct {
    char callSign[TEAM_CALLSIGN_LEN + 1];
    uint16_t txInterval;
    bool alertSound;
    bool cwDefault;
    bool valid;
} TEAM_Config_t;

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
    TEAM_Config_t config = {{0}, TEAM_POLL_INTERVAL, true, false, false};
    uint8_t length = 0;

    EEPROM_ReadBuffer(TEAM_CONFIG_ADDRESS, raw, sizeof(raw));
    if ((raw[6] & TEAM_CONFIG_TAG_MASK) != TEAM_CONFIG_TAG ||
        raw[7] != TEAM_ConfigCrc(raw))
        return config;

    for (uint8_t i = 0; i < TEAM_CALLSIGN_LEN; i++) {
        const char c = (char)raw[i];
        if (c == ' ' || c == '\0' || c == (char)0xff)
            break;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return config;
        config.callSign[length++] = c;
    }
    if (length < 3)
        return config;

    config.callSign[length] = '\0';
    config.txInterval = (raw[6] & TEAM_CONFIG_INTERVAL15) ? 1500u : 2500u;
    config.alertSound = (raw[6] & TEAM_CONFIG_ALERT) != 0;
    config.cwDefault = (raw[6] & TEAM_CONFIG_CW) != 0;
    config.valid = true;
    return config;
}

static bool TEAM_BatteryAllowsTx(void)
{
    // Keep the final battery segment for reception and local alarms.
    return gBatteryDisplayLevel > 1 && gBatteryDisplayLevel <= 6;
}

static bool TEAM_TxAllowed(const uint32_t frequency)
{
    return gTxVfo->Modulation == MODULATION_FM &&
           TX_freq_check(gTxVfo->pTX->Frequency) == 0 &&
           frequency == gTxVfo->pRX->Frequency &&
           TEAM_BatteryAllowsTx();
}

static void TEAM_ConfigureReceiver(const uint8_t dcsCode)
{
    const DCS_CodeType_t oldType = gTxVfo->pRX->CodeType;
    const uint8_t oldCode = gTxVfo->pRX->Code;

    gTxVfo->pRX->CodeType = CODE_TYPE_DIGITAL;
    gTxVfo->pRX->Code = dcsCode;
    RADIO_SetupRegisters(true);
    gTxVfo->pRX->CodeType = oldType;
    gTxVfo->pRX->Code = oldCode;

    BK4819_WriteRegister(BK4819_REG_3F,
        BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST |
        BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

static void TEAM_SetReceiveAudio(const bool enabled)
{
    if (enabled) {
        RADIO_SetModulation(gTxVfo->Modulation);
        AUDIO_AudioPathOn();
    } else {
        BK4819_SetAF(BK4819_AF_MUTE);
        AUDIO_AudioPathOff();
    }
}

static void TEAM_Render(const bool seen, const uint16_t ageTicks,
                        const int16_t lastDbm, const uint8_t dcsCode,
                        const uint32_t frequency, const bool autoTx,
                        const bool transmitting, const uint16_t txCountdown,
                        const uint16_t txInterval, const bool cwEnabled,
                        const TEAM_Config_t *config)
{
    char text[24];
    const char *state = !TEAM_BatteryAllowsTx() ? "LOW BAT TX OFF" :
        (transmitting ? "TRANSMITTING" : (!seen ? "NO LINK" :
        (ageTicks < TEAM_WEAK_TICKS ? "LINK OK" :
        (ageTicks < TEAM_LOST_TICKS ? "LINK WEAK" : "LINK LOST"))));

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    sprintf(text, "%u.%05u", frequency / 100000, frequency % 100000);
    UI_PrintStringSmallBold(text, 0, 127, 0);
    UI_PrintStringSmallBold(autoTx ? "TEAM AUTO TX" : "TEAM RX ONLY",
                            18, 110, 1);
    UI_PrintStringSmallBold(gTeamFeedbackTicks ? gTeamFeedback : state,
                            18, 112, 2);
    if (cwEnabled && config->valid) {
        sprintf(text, "CW %s", config->callSign);
        UI_PrintStringSmallBold(text, 30, 105, 3);
    } else if (!config->valid) {
        UI_PrintStringSmallBold("ID NOT SET", 30, 105, 3);
    }
    sprintf(text, "DCS %03oN", DCS_Options[dcsCode]);
    UI_PrintStringSmallBold(text, 8, 72, 4);
    if (seen) {
        sprintf(text, "%d dBm", lastDbm);
        UI_PrintStringSmallBold(text, 72, 127, 4);
        sprintf(text, "LAST %us", ageTicks / 100);
        UI_PrintStringSmallBold(text, 8, 92, 5);
    }
    if (autoTx)
        sprintf(text, "N%us I%us", (txCountdown + 99) / 100,
                txInterval / 100);
    else
        sprintf(text, "BAT %u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
    UI_PrintStringSmallBold(text, 8, 72, 6);
    UI_PrintStringSmallBold(gEeprom.KEY_LOCK ? "LOCK EXIT" : "EXIT",
                            gEeprom.KEY_LOCK ? 74 : 96, 127, 6);
    ST7565_BlitFullScreen();
}

static void TEAM_BeginTransmit(const uint8_t dcsCode)
{
    RADIO_SetTxParameters();
    // ARTS requires a DCS word even when the selected channel has no RX code.
    BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CODE_TYPE_DIGITAL, dcsCode));
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
}

static void TEAM_EndTransmit(const uint8_t dcsCode)
{
    BK4819_EnterTxMute();
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    TEAM_ConfigureReceiver(dcsCode);
}

static bool TEAM_TransmitVoice(const uint8_t dcsCode)
{
    uint16_t timeout = TEAM_PTT_TIMEOUT;

    TEAM_BeginTransmit(dcsCode);
    while (GPIO_IsPttPressed() && timeout--) {
        SYSTEM_DelayMs(10);
    }
    TEAM_EndTransmit(dcsCode);
    return !GPIO_IsPttPressed();
}

static void TEAM_TransmitPoll(const uint8_t dcsCode)
{
    TEAM_BeginTransmit(dcsCode);
    for (uint8_t ticks = 0; ticks < TEAM_POLL_DURATION; ticks++)
        SYSTEM_DelayMs(10);
    TEAM_EndTransmit(dcsCode);
}

static const char *TEAM_MorseCode(const char c)
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

static void TEAM_TransmitCwId(const uint8_t dcsCode, const char *id)
{
    TEAM_BeginTransmit(dcsCode);
    BK4819_TransmitTone(false, 700);
    BK4819_EnterTxMute();
    SYSTEM_DelayMs(TEAM_CW_DOT_MS * 3);

    while (*id) {
        const char *code = TEAM_MorseCode(*id++);
        if (!code)
            continue;
        while (*code) {
            BK4819_ExitTxMute();
            SYSTEM_DelayMs(TEAM_CW_DOT_MS * (*code++ == '-' ? 3 : 1));
            BK4819_EnterTxMute();
            if (*code)
                SYSTEM_DelayMs(TEAM_CW_DOT_MS);
        }
        if (*id)
            SYSTEM_DelayMs(TEAM_CW_DOT_MS * 3);
    }
    TEAM_EndTransmit(dcsCode);
}

static void TEAM_LostAlert(bool playSound)
{
    // Three synchronized light pulses remain visible when audio alerts are
    // muted or the operator cannot hear the radio outdoors.
    for (uint8_t i = 0; i < 3; i++) {
        GPIO_SetOutputPin(GPIO_PIN_FLASHLIGHT);
        if (playSound)
            AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
        SYSTEM_DelayMs(180);
        GPIO_ResetOutputPin(GPIO_PIN_FLASHLIGHT);
        if (i < 2)
            SYSTEM_DelayMs(120);
    }
    GPIO_ResetOutputPin(GPIO_PIN_FLASHLIGHT);
    TEAM_ConfigureReceiver(gTxVfo->pRX->CodeType == CODE_TYPE_DIGITAL
        ? gTxVfo->pRX->Code : 0);
}

void TEAM_Run(void)
{
    const TEAM_Config_t config = TEAM_LoadConfig();
    const uint8_t dcsCode = gTxVfo->pRX->CodeType == CODE_TYPE_DIGITAL
        ? gTxVfo->pRX->Code : 0;
    const uint32_t frequency = gTxVfo->pRX->Frequency;
    const uint16_t oldInterruptMask = BK4819_ReadRegister(BK4819_REG_3F);
    VFO_Info_t *const oldRxVfo = gRxVfo;
    VFO_Info_t *const oldCurrentVfo = gCurrentVfo;
    bool seen = false;
    bool lostAlerted = false;
    bool rxAudioOn = false;
    bool autoTx = false;
    bool pttTimedOut = false;
    bool alertSound = config.alertSound;
    bool cwEnabled = config.valid && config.cwDefault;
    uint16_t ageTicks = 0;
    uint16_t repeatTicks = 0;
    uint8_t renderTicks = 0;
    uint8_t key3Ticks = 0;
    bool key3Handled = false;
    uint16_t txCountdown = 0;
    uint16_t txInterval = config.txInterval;
    uint16_t cwCountdown = cwEnabled ? TEAM_AUTO_ARM_DELAY : 0;
    uint8_t batteryTicks = 0;
    uint8_t fKeyTicks = 0;
    bool fKeyHandled = false;
    KEY_Code_t previousKey = KEY_INVALID;
    int16_t lastDbm = -160;

    // Pin TEAM mode to the user-selected TX VFO. Restore both pointers on
    // exit so dual-watch state and EEPROM channel selection are untouched.
    gRxVfo = gTxVfo;
    gCurrentVfo = gTxVfo;
    TEAM_ConfigureReceiver(dcsCode);
    TEAM_Render(seen, ageTicks, lastDbm, dcsCode, frequency,
                autoTx, false, txCountdown, txInterval, cwEnabled, &config);

    while (true) {
        const KEY_Code_t key = KEYBOARD_Poll();

        if (key == KEY_EXIT)
            break;

        if (GPIO_IsPttPressed()) {
            if (!pttTimedOut && TEAM_TxAllowed(frequency)) {
                TEAM_SetReceiveAudio(false);
                rxAudioOn = false;
                TEAM_Render(seen, ageTicks, lastDbm, dcsCode, frequency,
                            autoTx, true, txCountdown, txInterval, cwEnabled,
                            &config);
                if (!TEAM_TransmitVoice(dcsCode))
                    pttTimedOut = true;
                if (autoTx)
                    txCountdown = txInterval;
            }
            SYSTEM_DelayMs(10);
            continue;
        }
        pttTimedOut = false;

        // A full one-second hold is required; AUTO TX is always off on entry.
        if (!gEeprom.KEY_LOCK && key == KEY_3) {
            if (!key3Handled && key3Ticks < 100 && ++key3Ticks >= 100) {
                autoTx = !autoTx;
                txCountdown = autoTx ? TEAM_AUTO_ARM_DELAY : 0;
                key3Handled = true;
                renderTicks = 10;
                TEAM_SetFeedback(autoTx ? "AUTO TX ARMED" : "AUTO TX OFF");
                AUDIO_PlayBeep(autoTx ? BEEP_1KHZ_60MS_OPTIONAL :
                                         BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
            }
        } else {
            key3Ticks = 0;
            key3Handled = false;
        }

        if (key == KEY_F) {
            if (!fKeyHandled && fKeyTicks < 100 && ++fKeyTicks >= 100) {
                gEeprom.KEY_LOCK = !gEeprom.KEY_LOCK;
                gRequestSaveSettings = true;
                fKeyHandled = true;
                TEAM_SetFeedback(gEeprom.KEY_LOCK ? "KEY LOCKED" : "KEY UNLOCKED");
                renderTicks = 10;
            }
        } else {
            fKeyTicks = 0;
            fKeyHandled = false;
        }

        if (!gEeprom.KEY_LOCK && key == KEY_1 && previousKey != KEY_1) {
            alertSound = !alertSound;
            TEAM_SetFeedback(alertSound ? "BEEP ON" : "BEEP OFF");
            renderTicks = 10;
        }
        if (!gEeprom.KEY_LOCK && key == KEY_2 && previousKey != KEY_2) {
            txInterval = txInterval == 2500u ? 1500u : 2500u;
            if (autoTx)
                txCountdown = txInterval;
            TEAM_SetFeedback(txInterval == 2500u ? "POLL 25 SEC" : "POLL 15 SEC");
            renderTicks = 10;
        }
        if (!gEeprom.KEY_LOCK && key == KEY_5 && previousKey != KEY_5) {
            if (config.valid) {
                cwEnabled = !cwEnabled;
                cwCountdown = cwEnabled ? TEAM_AUTO_ARM_DELAY : 0;
                TEAM_SetFeedback(cwEnabled ? "CW ON" : "CW OFF");
            } else {
                TEAM_SetFeedback("ID NOT SET");
            }
            renderTicks = 10;
        }
        previousKey = key;

        if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
            // BK4829 latches the event word only after the IRQ-clear write.
            // Reading REG_02 first returns stale/inverted-looking state and
            // can open audio on noise while muting a real carrier.
            BK4819_WriteRegister(BK4819_REG_02, 0);
            const uint16_t irq = BK4819_ReadRegister(BK4819_REG_02);
            if (irq & BK4819_REG_02_CDCSS_FOUND) {
                const uint16_t rssi = BK4819_GetRSSI();
                const bool recovered = lostAlerted;
                seen = true;
                lostAlerted = false;
                ageTicks = 0;
                repeatTicks = 0;
                lastDbm = (int16_t)(rssi / 2) - 160 + dBmCorrTable[gTxVfo->Band];
                renderTicks = 10;
                if (recovered && alertSound)
                    AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
            }
            // Speech follows carrier squelch. DCS is used independently for
            // ARTS link supervision so synchronization cannot clip speech.
            if ((irq & BK4819_REG_02_SQUELCH_LOST) && !rxAudioOn) {
                TEAM_SetReceiveAudio(true);
                rxAudioOn = true;
            }
            if ((irq & BK4819_REG_02_SQUELCH_FOUND) && rxAudioOn) {
                TEAM_SetReceiveAudio(false);
                rxAudioOn = false;
            }
        }

        if (seen && ageTicks < 65000)
            ageTicks++;
        if (seen && ageTicks >= TEAM_LOST_TICKS && !lostAlerted) {
            lostAlerted = true;
            repeatTicks = TEAM_ALERT_REPEAT;
            TEAM_SetReceiveAudio(false);
            rxAudioOn = false;
            TEAM_LostAlert(alertSound);
            renderTicks = 10;
        } else if (lostAlerted && repeatTicks > 0) {
            repeatTicks--;
        } else if (lostAlerted) {
            repeatTicks = TEAM_ALERT_REPEAT;
            TEAM_SetReceiveAudio(false);
            rxAudioOn = false;
            TEAM_LostAlert(alertSound);
        }

        if (autoTx) {
            if (!TEAM_TxAllowed(frequency) ||
                gTxVfo->OUTPUT_POWER > OUTPUT_POWER_LOW5) {
                // Scheduled unattended polls are deliberately limited to the
                // five calibrated low-power settings.
                autoTx = false;
                txCountdown = 0;
                AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
            } else if (txCountdown > 0) {
                txCountdown--;
            } else {
                TEAM_SetReceiveAudio(false);
                rxAudioOn = false;
                TEAM_Render(seen, ageTicks, lastDbm, dcsCode, frequency,
                            autoTx, true, txCountdown, txInterval, cwEnabled,
                            &config);
                TEAM_TransmitPoll(dcsCode);
                txCountdown = txInterval;
            }
        }

        if (cwEnabled && config.valid && TEAM_TxAllowed(frequency) &&
            gTxVfo->OUTPUT_POWER <= OUTPUT_POWER_LOW5) {
            if (cwCountdown > 0) {
                cwCountdown--;
            } else {
                TEAM_SetReceiveAudio(false);
                rxAudioOn = false;
                TEAM_SetFeedback("CW ID TX");
                TEAM_TransmitCwId(dcsCode, config.callSign);
                cwCountdown = TEAM_CW_INTERVAL;
            }
        }

        if (++batteryTicks >= 50) {
            batteryTicks = 0;
            BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4],
                                     &gBatteryCurrent);
            BATTERY_GetReadings(false);
        }

        if (++renderTicks >= 10) {
            renderTicks = 0;
            if (gTeamFeedbackTicks)
                gTeamFeedbackTicks = gTeamFeedbackTicks > 10
                    ? gTeamFeedbackTicks - 10 : 0;
            TEAM_Render(seen, ageTicks, lastDbm, dcsCode, frequency,
                        autoTx, false, txCountdown, txInterval, cwEnabled,
                        &config);
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
