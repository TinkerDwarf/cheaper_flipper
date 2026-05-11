#ifndef WIFI_FUNC_H
#define WIFI_FUNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Bounce2.h>
#include "display.h"
#include "definitions.h"
#include "deauth.h"
#include "types.h"
#include "esp_wifi.h"
#include "../SdUsb/SDCardManager.h"   // чтобы был доступен SDCardManager::isCardPresent()

#define MAX_APS 20

class WiFiAttackManager {
public:
    WiFiAttackManager(LGFX& display, uint16_t displayWidth, uint16_t displayHeight,
                      Bounce& up, Bounce& down, Bounce& left, Bounce& right, Bounce& ok,
                      const String& logFilePath = "/logs/attack_log.txt",
                      bool incognitoMode = false);

    void runWiFiMenu();
    bool isAttackRunning() const { return _isAttacking; }

private:
    LGFX& tft;
    uint16_t _w, _h;
    Bounce& btnUp;
    Bounce& btnDown;
    Bounce& btnLeft;
    Bounce& btnRight;
    Bounce& btnOK;

    String _logFile;
    bool _incognitoMode;

    int _numNetworks;
    WiFiAP _aps[MAX_APS];
    int _selectedAPIndex;
    int _attackMenuPos;
    WiFiAP _currentTarget;
    bool _isAttacking;
    String _currentAttack;

    int16_t x(int16_t v) const { return (int16_t)(((long)v * _w) / 320); }
    int16_t y(int16_t v) const { return (int16_t)(((long)v * _h) / 240); }

    void handlePMKIDSingle();
    bool saveHC22000(const String& hc22000, const String& ssid, const uint8_t* bssid);

    void updateButtons();
    void logAttack(const String& type, const String& target, const String& status);

    void drawWiFiList();
    void drawAttackMenu();
    void drawDeauthScreen(const String& target, bool all = false);
    void drawEvilTwinScreen();
    void drawSubmenu_I(int pos, const char* items[], int count);

    void scanWiFiNetworks();
    void saveScanResults();
    void initAttackMode();

    void handleDeauthSingle();
    void handleDeauthAll();
    void handleEvilTwin();
};

#endif