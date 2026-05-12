#ifndef MASS_STORAGE_MANAGER_H
#define MASS_STORAGE_MANAGER_H

#include <Arduino.h>
#include <display.h>   // for LGFX
#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "webstorrage.h"   // HTML page definition (expected to provide fileManagerHTML)
#include "SDCardManager.h"

class MassStorageManager {
public:
    enum StorageMode {
        USB_MSC,
        WIFI_STORAGE
    };

    /**
     * @param tft          Reference to the display
     * @param screenWidth  Actual display width in pixels
     * @param screenHeight Actual display height in pixels
     */
    MassStorageManager(LGFX& tft, uint16_t screenWidth, uint16_t screenHeight);

    ~MassStorageManager();

    /// Run the blocking Mass Storage menu (buttons, mode selection, start/stop)
    void handleMenu();

    bool isUSBMActive() const { return _usbActive; }
    bool isWiFiStorageActive() const { return _wifiActive; }

private:
    // --------------- Scaling helpers (base 240 x 320) -----------------------
    int16_t scX(int16_t x) const { return static_cast<int16_t>(x * _scaleX); }
    int16_t scY(int16_t y) const { return static_cast<int16_t>(y * _scaleY); }

    // --------------- USB MSC callbacks (static, access SD_MMC directly) -----
    static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
    static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    static bool    onStartStop(uint8_t power_condition, bool start, bool load_eject);

    // --------------- Internal control methods --------------------------------
    void startUSB();
    void stopUSB();
    bool startWiFi();
    void stopWiFi();

    // --------------- WiFi Storage helpers ------------------------------------
    void readWiFiStorageConfig();
    void setupWebServer();
    void handleWiFiClient();        // call server.handleClient()
    uint64_t calculateUsedSpace();
    void createDirectories(const String& path);
    static String getFilesList(const String& path);

    // --------------- Drawing -------------------------------------------------
    void drawMenu(bool forceRedraw = false);

    // --------------- Members -------------------------------------------------
    LGFX& _tft;
    uint16_t _screenWidth, _screenHeight;
    float    _scaleX, _scaleY;

    StorageMode _mode;
    bool _usbActive;
    bool _wifiActive;

    // WiFi Storage configuration
    String _wifiSSID;
    String _wifiPassword;
    bool   _useLocalNetwork;
    String _localIP;

    // Web server (created only when WiFi storage is active)
    WebServer* _server = nullptr;

    // USB MSC object (always present, but only active when started)
    USBMSC _msc;
};

#endif