#include "team.h"

#ifdef ENABLE_TEAM_MODE

#include "../audio.h"
#include "../dcs.h"
#include "../driver/bk4819-regs.h"
#include "../driver/bk4819.h"
#include "../driver/keyboard.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../external/printf/printf.h"
#include "../misc.h"
#include "../radio.h"
#include "../ui/helper.h"
#include "../ui/main.h"
#include <stdint.h>
#include <string.h>

static void TEAM_Render(bool seen, uint16_t ageTicks, uint16_t carrierTicks,
                        int lastDbm, uint8_t dcsCode)
{
    char text[24];
    const char *state = !seen ? (carrierTicks < 300 ? "RF NO DCS" : "NO LINK") :
        (ageTicks < 3500 ? "LINK OK" :
        (ageTicks < 6000 ? "LINK WEAK" : "LINK LOST"));
    uint32_t frequency = gTxVfo->pRX->Frequency;

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
    sprintf(text, "%u.%05u", frequency / 100000, frequency % 100000);
    UI_PrintStringSmallBold(text, 0, 127, 0);
    UI_PrintStringSmallBold("TEAM RX ONLY", 18, 110, 1);
    UI_PrintStringSmallBold(state, 24, 112, 2);

    // DCS numbers use octal digits. DCS_Options stores their numeric value, so
    // 0x13 (decimal 19) must be rendered as octal 023.
    sprintf(text, "DCS %03oN", DCS_Options[dcsCode]);
    UI_PrintStringSmallBold(text, 8, 72, 4);
    if (seen) {
        sprintf(text, "%d dBm", lastDbm);
        UI_PrintStringSmallBold(text, 73, 127, 4);
        sprintf(text, "LAST %us", ageTicks / 100);
        UI_PrintStringSmallBold(text, 8, 92, 6);
    }
    UI_PrintStringSmallBold("EXIT", 96, 127, 6);
    ST7565_BlitFullScreen();
}

void TEAM_Run(void)
{
    bool seen = false;
    uint8_t dcsCode = gTxVfo->pRX->CodeType == CODE_TYPE_DIGITAL
        ? gTxVfo->pRX->Code : 0; // DCS_Options[0] is D023N (octal 023).
    uint16_t ageTicks = 0;
    uint16_t carrierTicks = 0xFFFF;
    uint8_t renderTicks = 0;
    KEY_Code_t previousKey = KEY_INVALID;
    int lastDbm = -160;
    uint16_t oldInterruptMask = BK4819_ReadRegister(BK4819_REG_3F);
    VFO_Info_t *oldRxVfo = gRxVfo;
    VFO_Info_t *oldCurrentVfo = gCurrentVfo;
    DCS_CodeType_t oldCodeType = gTxVfo->pRX->CodeType;
    uint8_t oldCode = gTxVfo->pRX->Code;

    AUDIO_AudioPathOff();
    // Use the normal radio setup path so every BK4819 DCS detector register is
    // initialized exactly as it is during ordinary coded reception. Restore the
    // channel data immediately: TEAM mode must not alter EEPROM configuration.
    gRxVfo = gTxVfo;
    gCurrentVfo = gTxVfo;
    gTxVfo->pRX->CodeType = CODE_TYPE_DIGITAL;
    gTxVfo->pRX->Code = dcsCode;
    RADIO_SetupRegisters(true);
    gTxVfo->pRX->CodeType = oldCodeType;
    gTxVfo->pRX->Code = oldCode;
    BK4819_WriteRegister(BK4819_REG_3F,
        BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST |
        BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);
    BK4819_WriteRegister(BK4819_REG_02, 0);
    TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode);

    while (1) {
        KEY_Code_t key = KEYBOARD_Poll();

        if (key == KEY_EXIT && previousKey != KEY_EXIT)
            break;
        previousKey = key;

        if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
            uint16_t interrupts;

            BK4819_WriteRegister(BK4819_REG_02, 0);
            interrupts = BK4819_ReadRegister(BK4819_REG_02);
            if (interrupts & BK4819_REG_02_CDCSS_FOUND) {
                uint16_t rssi = BK4819_GetRSSI();

                seen = true;
                ageTicks = 0;
                lastDbm = (rssi / 2) - 160 + dBmCorrTable[gTxVfo->Band];
            }
            if (interrupts & BK4819_REG_02_SQUELCH_LOST) {
                uint16_t rssi = BK4819_GetRSSI();

                carrierTicks = 0;
                lastDbm = (rssi / 2) - 160 + dBmCorrTable[gTxVfo->Band];
            }
        }

        if (seen && ageTicks < 65000)
            ageTicks++;
        if (carrierTicks < 65000)
            carrierTicks++;
        if (++renderTicks >= 10) {
            renderTicks = 0;
            TEAM_Render(seen, ageTicks, carrierTicks, lastDbm, dcsCode);
        }
        SYSTEM_DelayMs(10);
    }

    BK4819_WriteRegister(BK4819_REG_3F, oldInterruptMask);
    gRxVfo = oldRxVfo;
    gCurrentVfo = oldCurrentVfo;
    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

#endif
