#ifndef APP_TEAM_H
#define APP_TEAM_H

#ifdef ENABLE_TEAM_MODE
#include <stdbool.h>
#include <stdint.h>

void TEAM_Run(void);
bool TEAM_GetConfiguredCallSign(char callSign[7]);
bool TEAM_TransmitCwId(uint8_t dcsCode, uint32_t frequency,
                       uint8_t txBias, const char *id);
bool TEAM_TransmitCarrier(uint8_t dcsCode, uint32_t frequency,
                          uint8_t txBias, uint16_t durationTicks);
#endif

#endif
