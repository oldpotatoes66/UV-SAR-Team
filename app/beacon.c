#include "beacon.h"

#ifdef ENABLE_TEAM_MODE

#include "team.h"
#include "../audio.h"
#include "../board.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
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
#define BEACON_TX_BIAS             5u

static bool gBeaconCarrierPresent;

static bool BEACON_BatteryAllowsTx(void)
{
    return gBatteryDisplayLevel > 1 && gBatteryDisplayLevel <= 6;
}

static bool BEACON_TxAllowed(uint32_t frequency)
{
    return gTxVfo->Modulation == MODULATION_FM &&
           TX_freq_check(frequency) == 0 && BEACON_BatteryAllowsTx();
}

static bool BEACON_ChannelBusy(void)
{
    // Use the receiver's calibrated squelch decision instead of a fixed RSSI
    // number. RSSI offsets vary enough between units that a raw threshold
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

static void BEACON_Render(const bool running, const bool transmitting,
                          const bool busy, const bool idValid,
                          const char *callSign, const uint16_t countdown,
                          const uint32_t sessionLeft)
{
    char text[24];
    const char *state = !BEACON_BatteryAllowsTx() ? "LOW BAT" :
        (!idValid ? "NO ID" :
        (transmitting ? "TX" : (busy ? "BUSY" : (running ? "ARMED" : "READY"))));

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    UI_PrintStringSmallBold("BEACON", 3, 125, 0);
    sprintf(text, "%u.%05u MHz", gTxVfo->pTX->Frequency / 100000,
            gTxVfo->pTX->Frequency % 100000);
    UI_PrintStringSmallBold(text, 2, 126, 1);
    UI_PrintStringSmallBold(state, 2, 126, 2);

    sprintf(text, "ID %s", idValid ? callSign : "------");
    UI_PrintStringSmallBold(text, 2, 75, 3);
    UI_PrintStringSmallBold("P1", 106, 127, 3);

    sprintf(text, "N%us", (countdown + 99) / 100);
    UI_PrintStringSmallBold(text, 2, 68, 4);
    sprintf(text, "S%um", (unsigned)((sessionLeft + 5999) / 6000));
    UI_PrintStringSmallBold(text, 72, 127, 4);
    GUI_DisplaySmallest(running ? "M:STOP" : "M:START",
                        2, 50, false, true);
    GUI_DisplaySmallest("E:EXIT", 100, 50, false, true);
    ST7565_BlitFullScreen();
}

void BEACON_Run(void)
{
    char callSign[7] = {0};
    const bool idValid = TEAM_GetConfiguredCallSign(callSign);
    const uint8_t dcsCode = gTxVfo->pTX->CodeType == CODE_TYPE_DIGITAL
        ? gTxVfo->pTX->Code : 0;
    const uint16_t oldInterruptMask = BK4819_ReadRegister(BK4819_REG_3F);
    bool running = false;
    bool busy = false;
    bool previousMenu = false;
    uint16_t countdown = BEACON_INTERVAL_TICKS;
    uint32_t sessionLeft = BEACON_SESSION_TICKS;
    uint8_t renderTicks = 0;
    uint8_t batteryTicks = 0;

    // K6 P1 is applied only to this app's transmissions and is never saved.
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
                  sessionLeft);

    while (true) {
        const KEY_Code_t key = KEYBOARD_Poll();
        if (key == KEY_EXIT)
            break;

        const bool menu = key == KEY_MENU;
        if (menu && !previousMenu) {
            if (!running) {
                if (idValid && BEACON_TxAllowed(gTxVfo->pTX->Frequency)) {
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
        if (!GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT)) {
            running = false;
            countdown = BEACON_INTERVAL_TICKS;
            AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
            while (!GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT))
                SYSTEM_DelayMs(10);
            renderTicks = 10;
        }

        if (running) {
            if (!BEACON_TxAllowed(gTxVfo->pTX->Frequency)) {
                running = false;
                AUDIO_PlayBeep(BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL);
                renderTicks = 10;
            } else if (sessionLeft == 0) {
                running = false;
                AUDIO_PlayBeep(BEEP_880HZ_60MS_DOUBLE_BEEP);
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
                                  sessionLeft);
                    // Identify every training transmission, then provide a
                    // steady three-second DCS carrier for easy measurement.
                    if (!TEAM_TransmitCwId(dcsCode, gTxVfo->pTX->Frequency,
                                           BEACON_TX_BIAS, callSign) ||
                        !TEAM_TransmitCarrier(dcsCode, gTxVfo->pTX->Frequency,
                                              BEACON_TX_BIAS,
                                              BEACON_DURATION_TICKS))
                        break;
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
                          sessionLeft);
        }
        SYSTEM_DelayMs(10);
    }

    RADIO_SetupRegisters(true);
    BK4819_WriteRegister(BK4819_REG_3F, oldInterruptMask);
    gUpdateDisplay = true;
}

#endif
