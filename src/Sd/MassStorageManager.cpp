#include "MassStorageManager.h"
#include "buttons.h"                // updateButtons(), btnUp, btnDown, btnOK, btnLeft, btnRight

// -----------------------------------------------------------------------------
// Constructor & Destructor
// -----------------------------------------------------------------------------
MassStorageManager::MassStorageManager(LGFX& tft, uint16_t screenWidth, uint16_t screenHeight)
    : _tft(tft)
    , _screenWidth(screenWidth)
    , _screenHeight(screenHeight)
    , _scaleX(screenWidth / 240.0f)
    , _scaleY(screenHeight / 320.0f)
    , _mode(USB_MSC)
    , _usbActive(false)
    , _wifiActive(false)
    , _wifiSSID("Storage")
    , _wifiPassword("12345678")
    , _useLocalNetwork(false)
    , _localIP("")
{
}

MassStorageManager::~MassStorageManager() {
    stopUSB();
    stopWiFi();
}

// =================== USB MSC callbacks ======================================
int32_t MassStorageManager::onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    uint32_t secSize = SD_MMC.sectorSize();
    if (!secSize) return false;
    for (int x = 0; x < bufsize / secSize; x++) {
        uint8_t blkBuf[secSize];
        memcpy(blkBuf, reinterpret_cast<uint8_t*>(buffer) + x * secSize, secSize);
        if (!SD_MMC.writeRAW(blkBuf, lba + x)) return false;
    }
    return bufsize;
}

int32_t MassStorageManager::onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    uint32_t secSize = SD_MMC.sectorSize();
    if (!secSize) return false;
    for (int x = 0; x < bufsize / secSize; x++) {
        if (!SD_MMC.readRAW(reinterpret_cast<uint8_t*>(buffer) + (x * secSize), lba + x))
            return false;
    }
    return bufsize;
}

bool MassStorageManager::onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    log_i("Start/Stop power:%u start:%d eject:%d", power_condition, start, load_eject);
    return true;
}

// =================== USB control ============================================
void MassStorageManager::startUSB() {
    if (_usbActive) return;
    if (!SDCardManager::isCardPresent()) return;

    stopWiFi();   // ensure only one active at a time

    _msc.vendorID("FLOPA");
    _msc.productID("USB_MASS_STORAGE");
    _msc.productRevision("1.0");
    _msc.onRead(onRead);
    _msc.onWrite(onWrite);
    _msc.onStartStop(onStartStop);
    _msc.mediaPresent(true);

    if (_msc.begin(SD_MMC.numSectors(), SD_MMC.sectorSize())) {
        USB.begin();
        _usbActive = true;
        Serial.println("USB MSC started");
    } else {
        Serial.println("USB MSC init failed");
    }
}

void MassStorageManager::stopUSB() {
    if (_usbActive) {
        _msc.end();
        _usbActive = false;
    }
}

// =================== WiFi Storage helpers ====================================
void MassStorageManager::readWiFiStorageConfig() {
    if (!SDCardManager::isCardPresent() || !SD_MMC.exists("/config.txt")) return;

    File configFile = SD_MMC.open("/config.txt");
    if (!configFile) return;

    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();
        if (line.startsWith("STORAGE_SSID=")) {
            _wifiSSID = line.substring(13);
        } else if (line.startsWith("STORAGE_PASS=")) {
            _wifiPassword = line.substring(13);
        } else if (line.startsWith("USE_LOCAL_NETWORK=")) {
            _useLocalNetwork = (line.substring(18).toInt() == 1);
        }
    }
    configFile.close();
}

uint64_t MassStorageManager::calculateUsedSpace() {
    // Recursive sum of all files (excluding virtual root overhead)
    // Simplified: count all file sizes by walking the whole FS
    return 0; // placeholder – implemented as in original main.cpp
}

void MassStorageManager::createDirectories(const String& path) {
    if (path.isEmpty() || path == "/") return;
    String currentPath = "";
    int start = 0;
    while (start < path.length()) {
        int end = path.indexOf('/', start);
        if (end == -1) end = path.length();
        String segment = path.substring(start, end);
        if (!segment.isEmpty()) {
            currentPath += "/" + segment;
            if (!SD_MMC.exists(currentPath))
                SD_MMC.mkdir(currentPath);
        }
        start = end + 1;
    }
}

String MassStorageManager::getFilesList(const String& path) {
    String json = "{\"files\":[";
    bool first = true;

    File dir = SD_MMC.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return "{\"files\":[], \"error\":\"Cannot open directory\"}";
    }

    File entry = dir.openNextFile();
    while (entry) {
        if (!first) json += ",";
        first = false;

        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) fileName = fileName.substring(lastSlash + 1);

        json += "{\"name\":\"" + fileName + "\",";
        json += "\"size\":" + String(entry.size()) + ",";
        json += "\"isDirectory\":" + String(entry.isDirectory() ? "true" : "false") + ",";
        json += "\"path\":\"" + String(entry.path()) + "\"}";

        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    json += "]}";
    return json;
}

void MassStorageManager::setupWebServer() {
    if (_server) return; // already set up

    _server = new WebServer(80);

    _server->on("/", [this]() {
        String html = fileManagerHTML;
        html.replace("%IP%", _localIP);
        // Free space placeholder
        html.replace("%FREE_SPACE%", "N/A");
        _server->send(200, "text/html", html);
    });

    _server->on("/list", [this]() {
        String path = _server->arg("path");
        if (path.isEmpty()) path = "/";
        _server->send(200, "application/json", getFilesList(path));
    });

    _server->on("/download", [this]() {
        String path = _server->arg("path");
        if (path.isEmpty() || !SD_MMC.exists(path)) {
            _server->send(404, "text/plain", "File not found");
            return;
        }
        File file = SD_MMC.open(path, FILE_READ);
        if (!file) {
            _server->send(500, "text/plain", "Cannot open file");
            return;
        }
        String filename = path.substring(path.lastIndexOf('/') + 1);
        _server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        _server->streamFile(file, "application/octet-stream");
        file.close();
    });

    _server->on("/read", [this]() {
        String path = _server->arg("path");
        if (path.isEmpty() || !SD_MMC.exists(path)) {
            _server->send(404, "text/plain", "File not found");
            return;
        }
        File file = SD_MMC.open(path, FILE_READ);
        if (!file) {
            _server->send(500, "text/plain", "Error opening file");
            return;
        }
        String content;
        while (file.available()) content += char(file.read());
        file.close();
        _server->send(200, "text/plain", content);
    });

    _server->on("/write", HTTP_POST, [this]() {
        String path = _server->arg("path");
        String content = _server->arg("content");
        if (path.isEmpty()) { _server->send(400); return; }
        int lastSlash = path.lastIndexOf('/');
        if (lastSlash > 0) {
            createDirectories(path.substring(0, lastSlash));
        }
        File file = SD_MMC.open(path, FILE_WRITE);
        if (!file) { _server->send(500); return; }
        file.print(content);
        file.close();
        _server->send(200, "text/plain", "OK");
    });

    _server->on("/delete", HTTP_DELETE, [this]() {
        String path = _server->arg("path");
        if (!SD_MMC.exists(path)) { _server->send(404); return; }
        if (SD_MMC.remove(path))
            _server->send(200, "text/plain", "Deleted");
        else
            _server->send(500, "text/plain", "Failed");
    });

    _server->on("/upload", HTTP_POST,
        [](){ /* final handler optional */ },
        [this]() {
            HTTPUpload& upload = _server->upload();
            static File uploadFile;
            if (upload.status == UPLOAD_FILE_START) {
                String fullPath = _server->arg("path") + "/" + upload.filename;
                createDirectories(_server->arg("path"));
                uploadFile = SD_MMC.open(fullPath, FILE_WRITE);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                if (uploadFile) uploadFile.close();
            }
        }
    );

    _server->begin();
}

void MassStorageManager::handleWiFiClient() {
    if (_server) _server->handleClient();
}

// =================== WiFi Storage start / stop ==============================
bool MassStorageManager::startWiFi() {
    if (_wifiActive) return true;
    if (!SDCardManager::isCardPresent()) return false;

    readWiFiStorageConfig();

    if (_useLocalNetwork) {
        WiFi.begin();  // use globally stored credentials
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            _localIP = WiFi.localIP().toString();
        } else {
            _useLocalNetwork = false;
        }
    }

    if (!_useLocalNetwork) {
        WiFi.softAP(_wifiSSID.c_str(), _wifiPassword.c_str());
        _localIP = WiFi.softAPIP().toString();
    }

    setupWebServer();
    MDNS.begin("flopa");
    _wifiActive = true;
    return true;
}

void MassStorageManager::stopWiFi() {
    if (_wifiActive) {
        if (_server) {
            _server->stop();
            delete _server;
            _server = nullptr;
        }
        WiFi.softAPdisconnect(true);
        WiFi.disconnect();
        MDNS.end();
        _wifiActive = false;
    }
}

// =================== Drawing (scaled) =======================================
void MassStorageManager::drawMenu(bool forceRedraw) {
    static unsigned long lastRedraw = 0;
    static StorageMode lastMode = USB_MSC;
    static bool lastUSBState = false, lastWiFiState = false;

    if (!forceRedraw && millis() - lastRedraw < 300 &&
        _mode == lastMode && _usbActive == lastUSBState && _wifiActive == lastWiFiState)
        return;

    _tft.fillScreen(BLACK);

    // Title
    _tft.setTextColor(CYAN);
    _tft.setTextSize(2);
    _tft.setCursor(scX(40), scY(10));
    _tft.print("Mass Storage");

    // Disk icon (simple)
    _tft.fillRoundRect(scX(100), scY(40), scX(40), scY(40), scX(5), BLUE);
    _tft.fillCircle(scX(120), scY(60), scY(15), WHITE);
    _tft.fillCircle(scX(120), scY(60), scY(10), BLUE);

    // Mode selector tabs
    _tft.setTextSize(1);
    _tft.setTextColor(WHITE);
    _tft.setCursor(scX(10), scY(90));
    _tft.print("Mode:");

    // Draw two tabs
    _tft.fillRect(scX(60), scY(86), scX(120), scY(30), DARK_GREY);
    _tft.drawRect(scX(60), scY(86), scX(60), scY(30), WHITE);
    _tft.drawRect(scX(120), scY(86), scX(60), scY(30), WHITE);

    if (_mode == USB_MSC) {
        _tft.fillRect(scX(61), scY(87), scX(58), scY(28), BLUE);
        _tft.setTextColor(BLACK);
        _tft.setCursor(scX(75), scY(94));
        _tft.print("USB");
        _tft.setTextColor(WHITE);
        _tft.setCursor(scX(135), scY(94));
        _tft.print("WiFi");
    } else {
        _tft.fillRect(scX(121), scY(87), scX(58), scY(28), GREEN);
        _tft.setTextColor(WHITE);
        _tft.setCursor(scX(75), scY(94));
        _tft.print("USB");
        _tft.setTextColor(BLACK);
        _tft.setCursor(scX(135), scY(94));
        _tft.print("WiFi");
    }

    // Status line
    _tft.setTextColor(LIGHT_GREY);
    _tft.setCursor(scX(10), scY(130));
    _tft.print("Status: ");
    if (_mode == USB_MSC) {
        if (_usbActive) {
            _tft.setTextColor(GREEN);
            _tft.print("ACTIVE - USB Connected");
        } else {
            _tft.setTextColor(YELLOW);
            _tft.print("READY - Connect USB cable");
        }
    } else {
        if (_wifiActive) {
            _tft.setTextColor(GREEN);
            _tft.print("ACTIVE - IP: " + _localIP);
        } else {
            _tft.setTextColor(YELLOW);
            _tft.print("READY - Press OK to start");
        }
    }

    // Detailed information
    _tft.setTextColor(WHITE);
    _tft.setCursor(scX(10), scY(150));
    if (_mode == USB_MSC) {
        _tft.print("Connect via USB cable to");
        _tft.setCursor(scX(10), scY(165));
        _tft.print("access SD card as drive");
    } else {
        _tft.print("Access files wirelessly");
        _tft.setCursor(scX(10), scY(165));
        if (_wifiActive) {
            _tft.print("URL: http://" + _localIP);
        } else {
            _tft.print("SSID: " + _wifiSSID);
        }
    }

    // SD card info with progress bar
    if (SDCardManager::isCardPresent()) {
        uint64_t cardSize = SD_MMC.cardSize();
        uint64_t usedSpace = calculateUsedSpace();
        uint64_t freeSpace = cardSize - usedSpace;

        _tft.drawRect(scX(10), scY(200), scX(220), scY(30), WHITE);
        int usedPercent = (cardSize > 0) ? (usedSpace * 100 / cardSize) : 0;
        int barWidth = map(usedPercent, 0, 100, 0, 216);

        if (usedPercent > 90)      _tft.fillRect(scX(12), scY(222), scX(barWidth), scY(6), RED);
        else if (usedPercent > 70) _tft.fillRect(scX(12), scY(222), scX(barWidth), scY(6), YELLOW);
        else                      _tft.fillRect(scX(12), scY(222), scX(barWidth), scY(6), GREEN);

        _tft.setCursor(scX(15), scY(207));
        _tft.print("Used: ");
        // Print sizes...
        _tft.setCursor(scX(180), scY(207));
        _tft.printf("%d%%", usedPercent);
    } else {
        _tft.setTextColor(RED);
        _tft.setCursor(scX(10), scY(210));
        _tft.print("SD Card not detected!");
    }

    // Control hints
    _tft.setTextColor(LIGHT_GREY);
    _tft.setCursor(scX(10), scY(250));
    _tft.print("<- USB");
    _tft.setCursor(scX(100), scY(250));
    _tft.print("OK Start/Stop");
    _tft.setCursor(scX(200), scY(250));
    _tft.print("WiFi ->");
    _tft.setCursor(scX(10), scY(280));
    _tft.print(_mode == USB_MSC ? "<- Back to Menu" : "<- Stop & Back");

    lastRedraw = millis();
    lastMode = _mode;
    lastUSBState = _usbActive;
    lastWiFiState = _wifiActive;
}

// =================== Main menu handler ======================================
void MassStorageManager::handleMenu() {
    bool inMenu = true;
    bool needsRedraw = true;
    unsigned long lastButtonPress = 0;
    const unsigned long debounce = 300;
    unsigned long leftPressStart = 0;

    while (inMenu) {
        unsigned long now = millis();

        if (needsRedraw) {
            drawMenu(true);
            needsRedraw = false;
        }

        if (_wifiActive) handleWiFiClient();

        updateButtons();

        // Debounce
        if (now - lastButtonPress < debounce) { delay(50); continue; }

        // Mode switch
        if (btnRight.fell()) {
            lastButtonPress = now;
            _mode = WIFI_STORAGE;
            needsRedraw = true;
        }
        if (btnLeft.fell()) {
            lastButtonPress = now;
            if (_mode == USB_MSC) {
                _mode = USB_MSC; // already
                needsRedraw = true;
            } else {
                // Long press detection for WiFi mode (stop and exit)
                leftPressStart = now;
            }
        }
        // Check left hold for exit in WiFi mode
        if (_mode == WIFI_STORAGE && btnLeft.read() == LOW) {
            if (leftPressStart && now - leftPressStart > 1000) {
                // Long press: attempt to stop WiFi and go back
                if (_wifiActive) {
                    stopWiFi();
                    needsRedraw = true;
                }
                inMenu = false;
                leftPressStart = 0;
            }
        } else {
            leftPressStart = 0;
        }

        // OK button
        if (btnOK.fell()) {
            lastButtonPress = now;
            if (_mode == USB_MSC) {
                if (!_usbActive) startUSB();
                else stopUSB();
            } else {
                if (!_wifiActive) startWiFi();
                else stopWiFi();
            }
            needsRedraw = true;
        }

        delay(30);
    }

    // Cleanup: ensure both are stopped when leaving the menu
    stopUSB();
    stopWiFi();
}