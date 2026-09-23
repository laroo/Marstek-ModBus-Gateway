#ifndef MARSTEK_WEBMANAGER_H
#define MARSTEK_WEBMANAGER_H

#include <WebServer.h>

extern WebServer webServer;

void setupWebServer(const char* clientId);
void loopWebServer();

#endif // MARSTEK_WEBMANAGER_H
