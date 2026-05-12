#ifndef PORTAL_H
#define PORTAL_H

extern const char portal_ssid[];
extern const char portal_pwd[];

void showPortalScreen();
void startFileServer();
void handleFiles();
void handleDownload();
void handleDeleteFile();
void handleDeleteAll();

#endif