#ifndef APP_TEAM_H
#define APP_TEAM_H

#ifdef ENABLE_TEAM_MODE
#include <stdbool.h>
#include <stdint.h>

void TEAM_Run(void);
bool TEAM_GetConfiguredCallSign(char callSign[7]);
bool TEAM_TransmitCwId(uint8_t dcsCode, const char *id);
#endif

#endif
