#define APN_FILENAME "/apn.db"

bool APNInit();
bool APNInit(char ssid[], char pwd[]);
bool APNDump();
bool APNFind(const char target[],char ssid[],char pwd[]);
bool APNAppend(char ssid[], char pwd[]);
bool APNDelete(char ssid[]);
