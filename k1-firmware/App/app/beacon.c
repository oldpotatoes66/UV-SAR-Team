#include "beacon.h"

#ifdef ENABLE_TEAM_MODE

#include "team.h"
#include "audio.h"
#include "board.h"
#include "dcs.h"
#include "driver/bk4819-regs.h"
#include "driver/bk4819.h"
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

// All timings are 10 ms ticks.  The deliberately conservative defaults make
// the mode useful for one-person training without turning it into an
// unattended general-purpose transmitter.
#define BEACON_INTERVAL_TICKS  3000u
#define BEACON_DURATION_TICKS   300u
#define BEACON_BUSY_RETRY_TICKS  500u
#define BEACON_SESSION_TICKS  180000u

static bool gBeaconCarrierPresent;

static bool BEACON_BatteryAllowsTx(void)
{
    return gBatteryDisplayLevel > 1 && gBatteryDisplayLevel <= 6;
}

static bool BEACON_TxAllowed(void)
{
    return gTxVfo->Modulation == MODULATION_FM &&
           TX_freq_check(gTxVfo->pTX->Frequency) == 0 &&
           BEACON_BatteryAllowsTx();
}

static bool BEACON_ChannelBusy(void)
{
    // Use the receiver's calibrated squelch decision instead of a fixed RSSI
    // number. K1 RSSI offsets vary enough between units that a raw threshold
    // can classify ordinary noise as a permanently busy channel.
    if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
        BK4819_WriteRegister(BK4819_REG_02, 0);
        const uint16_t irq = BK4819_ReadRegister(BK4819_REG_02);
        if (irq & BK4819_REG_02_SQUELCH_LOST)
            gBeaconCarrierPresent = true;
        if (irq & BK4819_REG_02_SQUELCH_FOUND)
            gBeaconCarrierPresent = false;
    }
    return gBeaconCarrierPresent;
}

static void BEACON_BeginTransmit(uint8_t dcsCode)
{
    RADIO_SetTxParameters();
    BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CODE_TYPE_DIGITAL, dcsCode));
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
}

static void BEACON_EndTransmit(void)
{
    BK4819_EnterTxMute();
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    RADIO_SetupRegisters(true);
}

static bool BEACON_TransmitCarrier(uint8_t dcsCode)
{
    BEACON_BeginTransmit(dcsCode);
    for (uint16_t ticks = 0; ticks < BEACON_DURATION_TICKS; ticks++) {
        if (KEYBOARD_Poll() == KEY_EXIT) {
            BEACON_EndTransmit();
            return false;
        }
        SYSTEM_DelayMs(10);
    }
    BEACON_EndTransmit();
    return true;
}

static void BEACON_Render(const bool running, const bool transmitting,
                          const bool busy, const bool idValid,
                          const char *callSign, const uint16_t countdown,
                          const uint32_t sessionLeft, const uint16_t sent)
{
    char text[24];
    const char *state = !BEACON_BatteryAllowsTx() ? "LOW BAT TX OFF" :
        (!idValid ? "ID NOT SET" :
        (transmitting ? "TRANSMITTING" :
        (busy ? "CHANNEL BUSY" : (running ? "BEACON ARMED" : "READY"))));

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    UI_PrintStringSmallBold("DF TRAINING BEACON", 3, 125, 0);
    sprintf(text, "%u.%05u MHz", gTxVfo->pTX->Frequency / 100000,
            gTxVfo->pTX->Frequency % 100000);
    UI_PrintStringSmallBold(text, 2, 126, 1);
    UI_PrintStringSmallBold(state, 2, 126, 2);

    sprintf(text, "ID %s", idValid ? callSign : "------");
    UI_PrintStringSmallBold(text, 2, 75, 3);
    UI_PrintStringSmallBold("PWR L1", 82, 127, 3);

    if (running) {
        sprintf(text, "NEXT %us", (countdown + 99) / 100);
        UI_PrintStringSmallBold(text, 2, 68, 4);
        sprintf(text, "STOP %um", (unsigned)((sessionLeft + 5999) / 6000));
        UI_PrintStringSmallBold(text, 72, 127, 4);
    } else {
        UI_PrintStringSmallBold("30S / TX 3S", 2, 126, 4);
    }
    sprintf(text, "SENT %u", sent);
    UI_PrintStringSmallBold(text, 2, 70, 5);
    GUI_DisplaySmallest(running ? "MENU:STOP" : "MENU:START",
                        2, 50, false, true);
    GUI_DisplaySmallest("EXIT", 104, 50, false, true);
    ST7565_BlitFullScreen();
}

void BEACON_Run(void)
{
    char callSign[7] = {0};
    const bool idValid = TEAM_GetConfiguredCallSign(callSign);
    const uint8_t dcsCode = gTxVfo->pTX->CodeType == CODE_TYPE_DIGITAL
        ? gTxVfo->pTX->Code : 0;
    const uint8_t originalPower = gTxVfo->OUTPUT_POWER;
    const uint16_t oldInterruptMask = BK4819_ReadRegister(BK4819_REG_3F);
    bool running = false;
    bool busy = false;
    bool previousMenu = false;
    uint16_t countdown = BEACON_INTERVAL_TICKS;
    uint32_t sessionLeft = BEACON_SESSION_TICKS;
    uint16_t sent = 0;
    uint8_t renderTicks = 0;
    uint8_t batteryTicks = 0;

    // Training always starts at the lowest calibrated power and never saves
    // that temporary setting back to the channel.
    gTxVfo->OUTPUT_POWER = OUTPUT_POWER_LOW1;
    RADIO_ConfigureSquelchAndOutputPower(gTxVfo);
    RADIO_SetupRegisters(true);
    gBeaconCarrierPresent = g_SquelchLost;
    BK4819_WriteRegister(BK4819_REG_3F,
        BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);
    BK4819_WriteRegister(BK4819_REG_02, 0);

    for (uint8_t i = 0; i < 4; i++) {
        BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4],
                                 &gBatteryCurrent);
        BATTERY_GetReadings(false);
    }
    BEACON_Render(false, false, false, idValid, callSign, countdown,
                  sessionLeft, sent);

    while (true) {
        const KEY_Code_t key = KEYBOARD_Poll();
        if (key == KEY_EXIT)
            break;

        const bool menu = key == KEY_MENU;
        if (menu && !previousMenu) {
            if (!running) {
                if (idValid && BEACON_TxAllowed()) {
                    running = true;
                    busy = false;
                    countdown = 300u; // three-second walk-away warning
                    sessionLeft = BEACON_SESSION_TICKS;
                    AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
                } else {
                    AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                }
            } else {
                running = false;
                AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
            }
            renderTicks = 10;
        }
        previousMenu = menu;

        // PTT is intentionally disabled inside this automatic transmitter.
        if (GPIO_IsPttPressed()) {
            running = false;
            countdown = BEACON_INTERVAL_TICKS;
            AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
            while (GPIO_IsPttPressed())
                SYSTEM_DelayMs(10);
            renderTicks = 10;
        }

        if (running) {
            if (!BEACON_TxAllowed()) {
                running = false;
                AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                renderTicks = 10;
            } else if (sessionLeft == 0) {
                running = false;
                AUDIO_PlayBeep(BEEP_880HZ_60MS_TRIPLE_BEEP);
                renderTicks = 10;
            } else {
                sessionLeft--;
                if (countdown > 0) {
                    countdown--;
                } else if (BEACON_ChannelBusy()) {
                    busy = true;
                    countdown = BEACON_BUSY_RETRY_TICKS;
                    renderTicks = 10;
                } else {
                    busy = false;
                    BEACON_Render(true, true, false, idValid, callSign, 0,
                                  sessionLeft, sent);
                    // Identify every training transmission, then provide a
                    // steady three-second DCS carrier for easy measurement.
                    if (!TEAM_TransmitCwId(dcsCode, callSign) ||
                        !BEACON_TransmitCarrier(dcsCode))
                        break;
                    sent++;
                    countdown = BEACON_INTERVAL_TICKS;
                    renderTicks = 10;
                }
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
            BEACON_Render(running, false, busy, idValid, callSign, countdown,
                          sessionLeft, sent);
        }
        SYSTEM_DelayMs(10);
    }

    gTxVfo->OUTPUT_POWER = originalPower;
    RADIO_ConfigureSquelchAndOutputPower(gTxVfo);
    RADIO_SetupRegisters(true);
    BK4819_WriteRegister(BK4819_REG_3F, oldInterruptMask);
    gUpdateDisplay = true;
}

#endif
