
#ifdef VERSION_STRING
    #define VER     " "VERSION_STRING
#else
    #define VER     ""
#endif

#ifdef ENABLE_FEAT_F4HWN
    const char Version[]      = AUTHOR_STRING_2 " " VERSION_STRING_2;
    const char Edition[]      = EDITION_STRING;
#else
    const char Version[]      = AUTHOR_STRING VER;
#endif

#ifdef SAR_TEAM_BUILD
const char UART_Version[] = "UV-K5 SAR-TEAM, " AUTHOR_STRING_2 " "
                            VERSION_STRING_2 ", oldpotatoes66@gmail.com\r\n";
#else
const char UART_Version[] = "UV-K5 Firmware, " AUTHOR_STRING VER "\r\n";
#endif
