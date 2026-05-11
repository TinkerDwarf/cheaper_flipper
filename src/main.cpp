
#include <Arduino.h>
#include "display.h"
#include "buttons.h"

#include <SPI.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include <SPIFFS.h>

#include "wifi/definitions.h"
#include "wifi/deauth.h"
#include "wifi/types.h"

#include "RF24.h"
#include "esp_wifi.h"
#include "esp_bt.h"

#include <SD_MMC.h>

LGFX tft;
#define TFT_BL 45
#define TFT_WIDTH 320

// WebServer server(80);
int num_networks;


int JAMCH = 10; // Для Selective Jam

// Добавим определение min/max если они отсутствуют
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif



#define BEACON_FRAME 0x80
#define MAX_APS 20


bool isJammingActive = false;

// Состояние системы
bool darkTheme = true;
uint8_t currentMenu = 0;
uint8_t menuPosition = 0;
uint8_t currentPage = 1;
const uint8_t itemsPerPage = 5;
const uint8_t totalPages = 2;
const char* LOG_FILE = "/logs/attack_log.txt";
bool incognitoMode = false; // Режим инкогнито


// Структура меню
struct MenuItem {
  const char* name;
  uint16_t color;
  uint8_t submenuCount;
  const char** submenuItems;
};

// Подменю
const char* subghzSubmenu[] = {"Read Signal", "Emulate Signal","Spectrum Analyzer", "Auto Calibrate", "Jammer"};
const char* ghz24Submenu[] = {"Jamming all", "BT Jamming", "Selective Jamming", "Spectum Analyser"};
const char* irSubmenu[] = {"Read Signal", "Save Signal", "Emulate", "Bruteforce"};
const char* rfidSubmenu[] = {"Read Tag", "Write Tag", "Emulate", "Dictionary"};
const char* nfcSubmenu[] = {"Read Tag", "Write Tag", "Emulate", "Dictionary"};
const char* wifiSubmenu[] = {"Scan Networks", "Deauth ALL", "Beacon Spam", "WPS Attack", "ON/OFF"};
const char* bleSubmenu[] = {"Scan Devices", "Spoofing", "Jamming", "ON/OFF"};
const char* sdusbSubmenu[] = {"BadUSB", "File Manager", "Mass Storage", "USB Settings"};
const char* settingsSubmenu[] = {"Theme", "Sound", "Vibration", "Password", "Power Save", "SD Card Info", "View Logs", "Incognito Mode"};
const char* WIFI_AP_SSID = "FLOPA";
const char* BLE_DEVICE_NAME = "FLOPA";
const char* wifi_ssid = nullptr;
const char* wifi_password = nullptr;

// Главное меню
MenuItem mainMenu[] = {
  {"Sub-GHz", CYAN, 5, subghzSubmenu},
  {"2.4 GHz", DARK_BLUE, 4, ghz24Submenu},
  {"RFID", GREEN, 4, rfidSubmenu},
  {"NFC", YELLOW, 4, nfcSubmenu},
  {"WiFi", BLUE, 5, wifiSubmenu},
  {"BLE", MAGENTA, 4, bleSubmenu},
  {"IR", RED, 4, irSubmenu},
  {"USB-SD", WHITE, 4, sdusbSubmenu},
  {"Settings", DARK_GREY, 8, settingsSubmenu}
};

//=======================TIME SYNC FUNCTIONS======================
#include <time.h>
// Переменные для времени
bool timeSynced = false;
unsigned long lastTimeSync = 0;
const unsigned long TIME_SYNC_INTERVAL = 3600000; // Синхронизация каждый час

// Конфигурация NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3 * 3600; // GMT+3 для Москвы
const int daylightOffset_sec = 0; // Летнее время

void syncTime() {
    if (WiFi.status() == WL_CONNECTED) {
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        
        // Ждем получения времени
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 5000)) { // Таймаут 5 секунд
            timeSynced = true;
            lastTimeSync = millis();
            Serial.println("Time synchronized successfully");
        } else {
            Serial.println("Failed to get time from NTP");
        }
    }
}

String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--:--";
    }
    
    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
}

String getCurrentDate() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--/--/----";
    }
    
    char dateStr[11];
    strftime(dateStr, sizeof(dateStr), "%d/%m/%Y", &timeinfo);
    return String(dateStr);
}

//=======================SYSTEM======================
void dravstatusbar(){
    // Очищаем область статусной строки
    tft.fillRect(245, 0, 320, 210, BLACK);
    
    // Температура
    int temperature = temperatureRead();
    tft.setTextSize(1);
    tft.setTextColor(darkTheme ? LIGHT_GREY : DARK_GREY);
    tft.setCursor(250, 200);
    tft.printf("Temp:%dC", temperature);
    
    // Время (если синхронизировано)
    if (timeSynced) {
        tft.setCursor(250, 5);
        tft.setTextColor(GREEN);
        tft.print(getCurrentTime());
    } else {
        tft.setCursor(250, 5);
        tft.setTextColor(RED);
        tft.print("NO TIME");
    }
    
    // Статус WiFi
    tft.setCursor(250, 15);
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(GREEN);
        tft.print("WiFi");
    } else {
        tft.setTextColor(RED);
        tft.print("No WiFi");
    }
}
//====================MENU & BUTTONS=================
uint8_t lastMainMenuPosition = 0;
uint8_t lastMainMenuPage = 1;
uint8_t currentSubmenuPage = 1;
const uint8_t itemsPerSubpage = 5;

void drawMenu() {
  tft.fillScreen(darkTheme ? BLACK : WHITE);
  
  // Заголовок
  tft.setTextSize(2);
  tft.setTextColor(darkTheme ? WHITE : BLACK);
  tft.setCursor(10, 10);
  tft.print("Main Menu");
  
  // Элементы меню
  uint8_t startItem = (currentPage - 1) * itemsPerPage;
  uint8_t totalItems = sizeof(mainMenu)/sizeof(mainMenu[0]);
  uint8_t endItem = min(startItem + itemsPerPage, totalItems);
  
  for (uint8_t i = startItem; i < endItem; i++) {
    uint8_t displayPos = i - startItem;
    
    if (displayPos == menuPosition) {
      tft.fillRoundRect(5, 40 + displayPos * 40, 230, 35, 5, mainMenu[i].color);
      tft.setTextColor(BLACK);
    } else {
      tft.drawRoundRect(5, 40 + displayPos * 40, 230, 35, 5, mainMenu[i].color);
      tft.setTextColor(darkTheme ? WHITE : BLACK);
    }
    
    tft.setCursor(20, 50 + displayPos * 40);
    tft.setTextSize(2);
    tft.print(mainMenu[i].name);
  }
  
  // Номер страницы
  tft.setTextSize(1);
  tft.setTextColor(darkTheme ? LIGHT_GREY : DARK_GREY);
  tft.setCursor(250, 220);
  tft.printf("page %d/%d", currentPage, totalPages);

  dravstatusbar();
}
void drawSubmenu() {
  tft.fillScreen(darkTheme ? BLACK : WHITE);
  
  // Статусная строка
  dravstatusbar();
  
  uint8_t parentMenu = currentMenu - 1;
  
  // Заголовок (вернуть на оригинальную позицию)
  tft.setTextSize(2);
  tft.setTextColor(mainMenu[parentMenu].color);
  tft.setCursor(10, 10); // ← ВЕРНУТЬ НА 10
  tft.print(mainMenu[parentMenu].name);
  
  // Определяем количество элементов и страниц подменю
  uint8_t submenuCount = mainMenu[parentMenu].submenuCount;
  uint8_t totalSubpages = (submenuCount + itemsPerSubpage - 1) / itemsPerSubpage;
  
  // Элементы подменю для текущей страницы (вернуть на оригинальные позиции)
  uint8_t startItem = (currentSubmenuPage - 1) * itemsPerSubpage;
  uint8_t endItem = min(startItem + itemsPerSubpage, submenuCount);
  
  for (uint8_t i = startItem; i < endItem; i++) {
    uint8_t displayPos = i - startItem;
    
    if (i == menuPosition) {
      tft.fillRoundRect(5, 40 + displayPos * 40, 230, 35, 5, mainMenu[parentMenu].color); // ← ВЕРНУТЬ НА 40
      tft.setTextColor(BLACK);
    } else {
      tft.drawRoundRect(5, 40 + displayPos * 40, 230, 35, 5, mainMenu[parentMenu].color); // ← ВЕРНУТЬ НА 40
      tft.setTextColor(darkTheme ? WHITE : BLACK);
    }
    
    tft.setCursor(20, 50 + displayPos * 40); // ← ВЕРНУТЬ НА 50
    tft.setTextSize(2);
    
    // Особое отображение для режима инкогнито
    if (parentMenu == 8 && i == 7) { // Settings -> Incognito Mode
      tft.print(incognitoMode ? "Incognito: ON" : "Incognito: OFF");
    } else {
      tft.print(mainMenu[parentMenu].submenuItems[i]);
    }
  }
  
  // Номер страницы подменю (если больше одной страницы)
  if (totalSubpages > 1) {
    tft.setTextSize(1);
    tft.setTextColor(darkTheme ? LIGHT_GREY : DARK_GREY);
    tft.setCursor(180, 15);
    tft.printf("page %d/%d", currentSubmenuPage, totalSubpages);
  }
  
  // Кнопка назад (вернуть на оригинальную позицию)
  tft.setTextSize(1);
  tft.setTextColor(RED);
  tft.setCursor(250, 220); // ← ВЕРНУТЬ НА 220
  tft.print("<- Back");
}
//===========SD-CARD AND FILE BROWSING-READING=======
#include "SdUsb/SDCardManager.h"

SDCardManager sdManager(tft, 480, 320);  // размеры нового дисплея



//=======================LOGS========================
void createWiFiConfig() {
    if (!SDCardManager::isCardPresent()) {
        Serial.println("SD card not present for config creation");
        return;
    }
    
    tft.fillScreen(BLACK);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("WiFi Setup");
    
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.print("Creating config file...");
    
    File configFile = SD_MMC.open("/config.txt", FILE_WRITE);
    if (!configFile) {
        tft.setTextColor(RED);
        tft.setCursor(10, 60);
        tft.print("Error creating config!");
        delay(2000);
        return;
    }
    
    // Записываем стандартные настройки
    configFile.println("SSID=ASUSko");
    configFile.println("PASSWORD=1720410316");
    configFile.println("INCOGNITO=0");
    configFile.close();
    
    tft.setTextColor(GREEN);
    tft.setCursor(10, 60);
    tft.print("Config created successfully!");
    
    tft.setTextColor(WHITE);
    tft.setCursor(10, 80);
    tft.print("SSID: ASUSko");
    tft.setCursor(10, 100);
    tft.print("PASS: 1720410316");
    
    tft.setCursor(10, 130);
    tft.print("Edit /config.txt on SD");
    tft.setCursor(10, 150);
    tft.print("to change WiFi settings");
    
    delay(3000);
}

// Обновленная функция чтения конфига
void readWiFiConfig() {
    if (!SDCardManager::isCardPresent()) return;
    
    // Проверяем существование конфиг-файла
    if (!SD_MMC.exists("/config.txt")) {
        createWiFiConfig(); // Создаем если нет
        return;
    }
    
    File configFile = SD_MMC.open("/config.txt");
    if (!configFile) {
        Serial.println("Failed to open config.txt");
        return;
    }
    
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("SSID=")) {
            static String ssid;
            ssid = line.substring(5);
            wifi_ssid = ssid.c_str();
            Serial.println("SSID from config: " + ssid);
        } else if (line.startsWith("PASSWORD=")) {
            static String password;
            password = line.substring(9);
            wifi_password = password.c_str();
            Serial.println("Password from config: " + password);
        } else if (line.startsWith("INCOGNITO=")) {
            incognitoMode = (line.substring(10).toInt() == 1);
            Serial.println("Incognito mode: " + String(incognitoMode ? "ON" : "OFF"));
        }
    }
    configFile.close();
}
void saveConfig() {
    if (!SDCardManager::isCardPresent()) return;
    
    File configFile = SD_MMC.open("/config.txt", FILE_WRITE);
    if (!configFile) {
        Serial.println("Failed to create config.txt");
        return;
    }
    
    configFile.println("SSID=ASUSko");
    configFile.println("PASSWORD=1720410316");
    configFile.println("INCOGNITO=0");
    configFile.close();
    
    Serial.println("Config saved");
}
void logAttack(const String& attackType, const String& target, const String& status) {
    if (!SDCardManager::isCardPresent() || incognitoMode) return;
    
    File logFile = SD_MMC.open(LOG_FILE, FILE_APPEND);
    if (!logFile) {
        Serial.println("Failed to open log file");
        return;
    }
    
    struct tm timeinfo;
    String timestamp = "--:--:--";
    if (getLocalTime(&timeinfo)) {
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        timestamp = String(timeStr);
    }
    
    logFile.println("[" + timestamp + "] " + attackType + " | Target: " + target + " | Status: " + status);
    logFile.close();
    
    Serial.println("Attack logged: " + attackType + " - " + target + " - " + status);
}
void viewAttackLogs() {
    if (!SDCardManager::isCardPresent()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("SD Card required!");
        delay(2000);
        return;
    }
    
    File logFile = SD_MMC.open(LOG_FILE);
    if (!logFile) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("No logs found");
        delay(2000);
        return;
    }
    
    bool inLogViewer = true;
    int currentPage = 1;
    int linesPerPage = 15;
    int totalLines = 0;
    
    // Считаем общее количество строк
    while (logFile.available()) {
        logFile.readStringUntil('\n');
        totalLines++;
    }
    logFile.seek(0);
    
    int totalPages = (totalLines + linesPerPage - 1) / linesPerPage;
    if (totalPages == 0) totalPages = 1;
    
    while (inLogViewer) {
        tft.fillScreen(BLACK);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        
        // Заголовок
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.print("Attack Logs");
        tft.setTextSize(1);
        tft.setCursor(200, 15);
        tft.printf("Page %d/%d", currentPage, totalPages);
        
        // Разделительная линия
        tft.drawLine(0, 25, 240, 25, WHITE);
        
        // Пропускаем строки до нужной страницы
        int startLine = (currentPage - 1) * linesPerPage;
        for (int i = 0; i < startLine; i++) {
            if (!logFile.available()) break;
            logFile.readStringUntil('\n');
        }
        
        // Отображаем строки текущей страницы
        int yPos = 35;
        for (int i = 0; i < linesPerPage && logFile.available(); i++) {
            String line = logFile.readStringUntil('\n');
            line.trim();
            
            if (line.length() > 35) {
                line = line.substring(0, 32) + "...";
            }
            
            tft.setCursor(5, yPos);
            tft.print(line);
            yPos += 15;
        }
        
        // Подсказки
        tft.setTextColor(LIGHT_GREY);
        tft.setCursor(10, 280);
        tft.print("<- Back  ^v Scroll");
        
        // Обработка кнопок
        bool buttonPressed = false;
        while (!buttonPressed) {
            updateButtons();
            
            if (btnLeft.fell()) {
                inLogViewer = false;
                buttonPressed = true;
            }
            else if (btnUp.fell() && currentPage > 1) {
                currentPage--;
                buttonPressed = true;
            }
            else if (btnDown.fell() && currentPage < totalPages) {
                currentPage++;
                buttonPressed = true;
            }
            
            delay(50);
        }
        
        logFile.seek(0); // Возвращаемся к началу файла для следующей страницы
    }
    
    logFile.close();
}

//======================BAD USB======================
#include <USB.h>
#include <USBMSC.h>
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;
// Добавляем в глобальные переменные
bool isBadUSBActive = false;
String selectedScript = "";

uint8_t lastBadUSBPosition = 0; // Для сохранения позиции в BadUSB

void listBadUSBScripts(bool forceRedraw = false) {
  static unsigned long lastRedraw = 0;
  static int lastFileCount = -1;
  static int lastMenuPosition = -1;
  
  // Проверяем необходимость перерисовки
  bool needsRedraw = forceRedraw || 
                    (millis() - lastRedraw > 300) ||
                    (menuPosition != lastMenuPosition);
  
  if (!needsRedraw) return;
  
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("BadUSB Scripts");
  
  if (!SDCardManager::isCardPresent()) {
    tft.setTextColor(RED);
    tft.setCursor(10, 50);
    tft.print("SD Card not found!");
    lastRedraw = millis();
    lastMenuPosition = menuPosition;
    return;
  }
  
  // Создаем папку badusb если её нет
  if (!SD_MMC.exists("/badusb")) {
    SD_MMC.mkdir("/badusb");
    tft.setTextColor(YELLOW);
    tft.setCursor(10, 50);
    tft.print("Created /badusb folder");
    lastRedraw = millis();
    lastMenuPosition = menuPosition;
    return;
  }
  
  File dir = SD_MMC.open("/badusb");
  if (!dir || !dir.isDirectory()) {
    tft.setTextColor(RED);
    tft.setCursor(10, 50);
    tft.print("Error opening /badusb");
    lastRedraw = millis();
    lastMenuPosition = menuPosition;
    return;
  }
  
  int yPos = 50;
  int fileCount = 0;
  int totalFiles = 0;
  int startIndex = 0;
  
  // Считаем общее количество файлов
  dir.rewindDirectory();
  while (File entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      String fileName = entry.name();
      if (fileName.endsWith(".txt") || fileName.endsWith(".TXT")) {
        totalFiles++;
      }
    }
    entry.close();
  }
  
  // Определяем начальный индекс для отображения (для скроллинга)
  if (totalFiles > 7) {
    if (menuPosition > 6) {
      startIndex = menuPosition - 6;
    }
  }
  
  // Отображаем файлы
  dir.rewindDirectory();
  int displayIndex = 0;
  int actualIndex = 0;
  
  while (File entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      String fileName = entry.name();
      if (fileName.endsWith(".txt") || fileName.endsWith(".TXT")) {
        if (actualIndex >= startIndex && displayIndex < 7) {
          // Выделение текущего элемента
          if (actualIndex == menuPosition) {
            tft.fillRect(0, yPos - 2, 240, 20, BLUE);
            tft.setTextColor(BLACK);
          } else {
            tft.setTextColor(WHITE);
          }
          
          tft.setCursor(5, yPos);
          // Обрезаем длинные имена
          if (fileName.length() > 20) {
            tft.print(fileName.substring(0, 17) + "...");
          } else {
            tft.print(fileName);
          }
          yPos += 20;
          displayIndex++;
        }
        actualIndex++;
        fileCount++;
      }
    }
    entry.close();
  }
  dir.close();
  
  if (fileCount == 0) {
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 50);
    tft.print("No scripts found");
    tft.setCursor(10, 70);
    tft.print("Create .txt files in");
    tft.setCursor(10, 90);
    tft.print("/badusb folder");
  }
  
  // Индикатор скроллинга
  if (totalFiles > 7) {
    tft.setTextSize(1);
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(220, 10);
    tft.printf("%d/%d", min(menuPosition + 1, totalFiles), totalFiles);
  }
  
  // Информация о количестве файлов
  tft.setTextSize(1);
  tft.setTextColor(LIGHT_GREY);
  tft.setCursor(10, 200);
  tft.printf("Files: %d/%d", menuPosition + 1, totalFiles);
  
  // Подсказки управления
  tft.setCursor(10, 280);
  tft.print("OK-Run");
  tft.setCursor(80, 280);
  tft.print("^v-Scroll");
  tft.setCursor(160, 280);
  tft.print("<-Back");
  
  lastRedraw = millis();
  lastMenuPosition = menuPosition;
  lastFileCount = totalFiles;
}
void executeBadUSBScript(String scriptPath) {
  File file = SD_MMC.open(scriptPath);
  if (!file) {
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    tft.setCursor(10, 50);
    tft.print("Error opening script");
    delay(2000);
    return;
  }
  
  // Очищаем экран и показываем информацию о скрипте
  tft.fillScreen(BLACK);
  tft.setTextColor(GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Executing:");
  
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  String fileName = scriptPath.substring(scriptPath.lastIndexOf('/') + 1);
  if (fileName.length() > 25) {
    fileName = fileName.substring(0, 22) + "...";
  }
  tft.print(fileName);
  
  // Инициализируем USB HID
  Keyboard.begin();
  USB.begin();
  delay(2000); // Даем время для инициализации USB
  
  tft.setTextColor(WHITE);
  tft.setCursor(10, 70);
  tft.print("Status: Running...");
  
  tft.setTextColor(LIGHT_GREY);
  tft.setCursor(10, 280);
  tft.print("Press <- to stop");
  
  isBadUSBActive = true;
  unsigned long lastActionTime = millis();
  int lineNumber = 0;
  
  while (file.available() && isBadUSBActive) {
    String line = file.readStringUntil('\n');
    line.trim();
    lineNumber++;
    
    // Пропускаем пустые строки и комментарии
    if (line.length() == 0 || line.startsWith("//")) {
      continue;
    }
    
    // Очищаем область отображения команды
    tft.fillRect(0, 90, 240, 60, BLACK);
    tft.setTextColor(CYAN);
    tft.setCursor(10, 90);
    tft.print("Line: ");
    tft.print(lineNumber);
    
    tft.setCursor(10, 110);
    tft.print("Command: ");
    if (line.length() > 20) {
      tft.print(line.substring(0, 17) + "...");
    } else {
      tft.print(line);
    }
    
    // Обрабатываем команды
    if (line.startsWith("DELAY ")) {
      int delayTime = line.substring(6).toInt();
      tft.setCursor(10, 130);
      tft.print("Delay: ");
      tft.print(delayTime);
      tft.print("ms");
      delay(delayTime);
    }
    else if (line.startsWith("STRING ")) {
      String text = line.substring(7);
      Keyboard.print(text);
      delay(100);
    }
    else if (line.equals("ENTER")) {
      Keyboard.write(KEY_RETURN);
      delay(100);
    }
    else if (line.equals("TAB")) {
      Keyboard.write(KEY_TAB);
      delay(100);
    }
    else if (line.equals("BACKSPACE")) {
      Keyboard.write(KEY_BACKSPACE);
      delay(100);
    }
    else if (line.equals("DELETE")) {
      Keyboard.write(KEY_DELETE);
      delay(100);
    }
    else if (line.equals("UP")) {
      Keyboard.write(KEY_UP_ARROW);
      delay(100);
    }
    else if (line.equals("DOWN")) {
      Keyboard.write(KEY_DOWN_ARROW);
      delay(100);
    }
    else if (line.equals("LEFT")) {
      Keyboard.write(KEY_LEFT_ARROW);
      delay(100);
    }
    else if (line.equals("RIGHT")) {
      Keyboard.write(KEY_RIGHT_ARROW);
      delay(100);
    }
    else if (line.startsWith("CTRL+")) {
      String key = line.substring(5);
      Keyboard.press(KEY_LEFT_CTRL);
      if (key == "c") Keyboard.press('c');
      else if (key == "v") Keyboard.press('v');
      else if (key == "x") Keyboard.press('x');
      else if (key == "a") Keyboard.press('a');
      else if (key == "z") Keyboard.press('z');
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("ALT+")) {
      String key = line.substring(4);
      Keyboard.press(KEY_LEFT_ALT);
      if (key == "f4") {
        Keyboard.press(KEY_F4);
      } else if (key == "tab") {
        Keyboard.press(KEY_TAB);
      }
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("GUI+") || line.startsWith("WINDOWS+")) {
      String key = line.substring(line.startsWith("GUI+") ? 4 : 8);
      Keyboard.press(KEY_LEFT_GUI);
      if (key == "r") Keyboard.press('r');
      else if (key == "d") Keyboard.press('d');
      else if (key == "l") Keyboard.press('l');
      else if (key == "e") Keyboard.press('e');
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("SHIFT ")) {
      String key = line.substring(6);
      Keyboard.press(KEY_LEFT_SHIFT);
      if (key.length() == 1) {
        Keyboard.press(key[0]);
      }
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("CTRL ")) {
      String key = line.substring(5);
      Keyboard.press(KEY_LEFT_CTRL);
      if (key == "c") Keyboard.press('c');
      else if (key == "v") Keyboard.press('v');
      else if (key == "x") Keyboard.press('x');
      else if (key == "a") Keyboard.press('a');
      else if (key == "z") Keyboard.press('z');
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("ALT ")) {
      String key = line.substring(4);
      Keyboard.press(KEY_LEFT_ALT);
      if (key == "f4") {
        Keyboard.press(KEY_F4);
      } else if (key == "tab") {
        Keyboard.press(KEY_TAB);
      }
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("GUI ") || line.startsWith("WINDOWS ")) {
      String key = line.substring(line.startsWith("GUI ") ? 4 : 8);
      Keyboard.press(KEY_LEFT_GUI);
      if (key == "r") Keyboard.press('r');
      else if (key == "d") Keyboard.press('d');
      else if (key == "l") Keyboard.press('l');
      else if (key == "e") Keyboard.press('e');
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else if (line.startsWith("SHIFT ")) {
      String key = line.substring(6);
      Keyboard.press(KEY_LEFT_SHIFT);
      if (key.length() == 1) {
        Keyboard.press(key[0]);
      }
      delay(100);
      Keyboard.releaseAll();
      delay(100);
    }
    else {
      // Простая текстовая строка
      Keyboard.print(line);
      Keyboard.write(KEY_RETURN);
      delay(500);
    }
    
    // Проверяем кнопку остановки
    updateButtons();
    if (btnLeft.fell()) {
      isBadUSBActive = false;
      tft.fillRect(0, 130, 240, 20, BLACK);
      tft.setTextColor(RED);
      tft.setCursor(10, 130);
      tft.print("Stopped by user!");
      break;
    }
    
    // Защита от зависания
    if (millis() - lastActionTime > 60000) { // 60 секунд таймаут
      isBadUSBActive = false;
      tft.fillRect(0, 130, 240, 20, BLACK);
      tft.setTextColor(RED);
      tft.setCursor(10, 130);
      tft.print("Timeout!");
      break;
    }
    
    delay(50); // Небольшая задержка между командами
  }
  
  file.close();
  Keyboard.releaseAll();
  delay(100);
  Keyboard.end();
  
  tft.fillScreen(BLACK);
  tft.setTextColor(GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 50);
  tft.print("Script finished");
  delay(2000);
  
  isBadUSBActive = false;
}
void handleBadUSB() {
  if (!SDCardManager::isCardPresent()) {
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    tft.setCursor(10, 50);
    tft.print("SD Card required!");
    delay(2000);
    return;
  }
  
  // Создаем папку badusb если её нет
  if (!SD_MMC.exists("/badusb")) {
    SD_MMC.mkdir("/badusb");
  }
  
  // Восстанавливаем сохраненную позицию
  menuPosition = lastBadUSBPosition;
  
  // Получаем общее количество файлов
  int totalFiles = 0;
  File dir = SD_MMC.open("/badusb");
  if (dir) {
    dir.rewindDirectory();
    while (File entry = dir.openNextFile()) {
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        if (fileName.endsWith(".txt") || fileName.endsWith(".TXT")) {
          totalFiles++;
        }
      }
      entry.close();
    }
    dir.close();
  }
  
  // Ограничиваем menuPosition если файлов стало меньше
  if (menuPosition >= totalFiles && totalFiles > 0) {
    menuPosition = totalFiles - 1;
  }
  
  bool inBadUSB = true;
  
  // Первоначальная отрисовка
  listBadUSBScripts(true);
  
  while (inBadUSB) {
    updateButtons(); // Всегда обновляем состояние кнопок
    
    if (btnUp.fell()) {
      if (menuPosition > 0) {
        menuPosition--;
        listBadUSBScripts(true); // Обновляем только при изменении
      }
    }
    
    if (btnDown.fell()) {
      if (menuPosition < totalFiles - 1) {
        menuPosition++;
        listBadUSBScripts(true); // Обновляем только при изменении
      }
    }
    
    if (btnOK.fell()) {
      // Находим выбранный файл
      File dir = SD_MMC.open("/badusb");
      if (dir) {
        int currentFile = 0;
        dir.rewindDirectory();
        while (File entry = dir.openNextFile()) {
          if (!entry.isDirectory()) {
            String fileName = entry.name();
            if (fileName.endsWith(".txt") || fileName.endsWith(".TXT")) {
              if (currentFile == menuPosition) {
                selectedScript = "/badusb/" + fileName;
                entry.close();
                dir.close();
                
                // Сохраняем текущую позицию перед выполнением
                lastBadUSBPosition = menuPosition;
                
                // Запускаем скрипт
                executeBadUSBScript(selectedScript);
                // После выполнения перерисовываем список
                listBadUSBScripts(true);
                break;
              }
              currentFile++;
            }
          }
          entry.close();
        }
        dir.close();
      }
    }
    
    if (btnLeft.fell()) {
      // Сохраняем позицию перед выходом
      lastBadUSBPosition = menuPosition;
      inBadUSB = false;
    }
    
    delay(10); // Небольшая задержка для стабильности
  }

}
void showBadUSB() {  
  handleBadUSB();
}

//======================WIFI MASS STORAGE======================
#include <WebServer.h>
#include <ESPmDNS.h>
#include "webstorrage.h"

WebServer server(80);
bool isWiFiStorageActive = false;
String wifiStorageSSID = "Storage";
String wifiStoragePassword = "12345678";
bool useLocalNetwork = false;
String localIP = "";

// Недостающие функции
void createDefaultConfig() {
    if (!SDCardManager::isCardPresent()) return;
    
    File configFile = SD_MMC.open("/config.txt", FILE_WRITE);
    if (!configFile) return;
    
    configFile.println("STORAGE_SSID=FLOPA-Storage");
    configFile.println("STORAGE_PASS=12345678");
    configFile.println("USE_LOCAL_NETWORK=0");
    configFile.println("HOME_SSID=your_home_wifi");
    configFile.println("HOME_PASS=your_password");
    configFile.close();
}
void createDirectories(String path) {
    if (path == "" || path == "/") return;
    
    String currentPath = "";
    int start = 0;
    
    while (start < path.length()) {
        int end = path.indexOf('/', start);
        if (end == -1) end = path.length();
        
        String segment = path.substring(start, end);
        if (segment != "") {
            currentPath += "/" + segment;
            if (!SD_MMC.exists(currentPath)) {
                SD_MMC.mkdir(currentPath);
            }
        }
        
        start = end + 1;
    }
}
// Функция для рекурсивного расчета размера директории
uint64_t calculateDirectorySize(const char* directoryPath) {
    uint64_t totalSize = 0;
    File dir = SD_MMC.open(directoryPath);
    
    if (!dir) return 0;
    if (!dir.isDirectory()) {
        dir.close();
        return 0;
    }
    
    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            // Рекурсивно обходим поддиректории
            String subDirPath = String(directoryPath);
            if (!subDirPath.endsWith("/")) {
                subDirPath += "/";
            }
            subDirPath += file.name();
            totalSize += calculateDirectorySize(subDirPath.c_str());
        } else {
            // Добавляем размер файла
            totalSize += file.size();
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    return totalSize;
}
uint64_t calculateUsedSpace() {
    if (!SDCardManager::isCardPresent()) return 0;
    return calculateDirectorySize("/");
}
// Чтение конфигурации WiFi из config.txt
void readWiFiStorageConfig() {
    if (!SDCardManager::isCardPresent()) return;
    
    if (!SD_MMC.exists("/config.txt")) {
        createDefaultConfig();
        return;
    }
    
    File configFile = SD_MMC.open("/config.txt");
    if (!configFile) return;
    
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("STORAGE_SSID=")) {
            wifiStorageSSID = line.substring(13);
        } else if (line.startsWith("STORAGE_PASS=")) {
            wifiStoragePassword = line.substring(13);
        } else if (line.startsWith("USE_LOCAL_NETWORK=")) {
            useLocalNetwork = (line.substring(18).toInt() == 1);
        }
    }
    configFile.close();
}

// Функции для работы с файловой системой
String getFilesList(String path) {
    String json = "{\"files\":[";
    bool first = true;
    
    File dir = SD_MMC.open(path);
    if (!dir) {
        return "{\"files\":[], \"error\":\"Cannot open directory\"}";
    }
    
    if (!dir.isDirectory()) {
        dir.close();
        return "{\"files\":[], \"error\":\"Not a directory\"}";
    }
    
    File file = dir.openNextFile();
    while (file) {
        if (!first) json += ",";
        first = false;
        
        String fileName = file.name();
        // Убираем полный путь, оставляем только имя файла/папки
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
            fileName = fileName.substring(lastSlash + 1);
        }
        
        json += "{";
        json += "\"name\":\"" + fileName + "\",";
        json += "\"size\":" + String(file.size()) + ",";
        json += "\"isDirectory\":" + String(file.isDirectory() ? "true" : "false") + ",";
        json += "\"path\":\"" + String(file.path()) + "\"";
        json += "}";
        
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    json += "]}";
    return json;
}
File uploadFile;

// Обработчики HTTP запросов
void handleRoot() {
    String html = fileManagerHTML;
    html.replace("%IP%", localIP);
    
    // Получаем информацию о свободном месте
    if (SDCardManager::isCardPresent()) {
        uint64_t totalSize = SD_MMC.cardSize();
        uint64_t usedSpace = calculateUsedSpace();
        uint64_t freeSpace = totalSize - usedSpace;
        
        String freeSpaceStr;
        if (freeSpace < 1024 * 1024) {
            freeSpaceStr = String(freeSpace / 1024.0, 1) + " KB";
        } else if (freeSpace < 1024 * 1024 * 1024) {
            freeSpaceStr = String(freeSpace / (1024.0 * 1024.0), 1) + " MB";
        } else {
            freeSpaceStr = String(freeSpace / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
        }
        
        html.replace("%FREE_SPACE%", freeSpaceStr);
    } else {
        html.replace("%FREE_SPACE%", "No SD Card");
    }
    
    server.send(200, "text/html", html);
}
void handleList() {
    String path = server.arg("path");
    if (path == "") path = "/";
    
    Serial.println("Listing directory: " + path);
    
    String fileList = getFilesList(path);
    server.send(200, "application/json", fileList);
}
void handleDownload() {
    String path = server.arg("path");
    Serial.println("Download request: " + path);
    
    if (path == "" || !SD_MMC.exists(path)) {
        server.send(404, "text/plain", "File not found: " + path);
        return;
    }
    
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    
    String filename = path.substring(path.lastIndexOf('/') + 1);
    server.sendHeader("Content-Type", "application/octet-stream");
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    server.sendHeader("Connection", "close");
    server.streamFile(file, "application/octet-stream");
    file.close();
}
void handleRead() {
    String path = server.arg("path");
    Serial.println("Read request: " + path);
    
    if (path == "" || !SD_MMC.exists(path)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    
    String content;
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    
    server.send(200, "text/plain", content);
}
void handleWrite() {
    String path = server.arg("path");
    String content = server.arg("content");
    
    Serial.println("Write request: " + path);
    
    if (path == "") {
        server.send(400, "text/plain", "Path is required");
        return;
    }
    
    // Создаем директории если нужно
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
        String dirPath = path.substring(0, lastSlash);
        createDirectories(dirPath);
    }
    
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        server.send(500, "text/plain", "Error creating file");
        return;
    }
    
    if (file.print(content)) {
        file.close();
        server.send(200, "text/plain", "File saved successfully");
    } else {
        file.close();
        server.send(500, "text/plain", "Error writing to file");
    }
}
void handleDelete() {
    String path = server.arg("path");
    Serial.println("Delete request: " + path);
    
    if (path == "" || !SD_MMC.exists(path)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    
    File file = SD_MMC.open(path);
    if (!file) {
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    
    if (file.isDirectory()) {
        file.close();
        // TODO: Рекурсивное удаление директорий
        server.send(501, "text/plain", "Directory deletion not implemented");
    } else {
        file.close();
        if (SD_MMC.remove(path)) {
            server.send(200, "text/plain", "File deleted successfully");
        } else {
            server.send(500, "text/plain", "Error deleting file");
        }
    }
}
void handleUpload() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        String path = server.arg("path");
        if (path == "" || path == "/") path = "";
        else if (!path.endsWith("/")) path += "/";
        String fullPath = path + filename;
        
        Serial.println("Upload start: " + fullPath);
        
        // Создаем директории если нужно
        if (path != "") {
            createDirectories(path);
        }
        
        uploadFile = SD_MMC.open(fullPath, FILE_WRITE);
        if (!uploadFile) {
            Serial.println("Error creating file: " + fullPath);
        }
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            Serial.println("Upload complete");
        }
    }
}
// Инициализация WiFi Storage
bool initWiFiStorage() {
    readWiFiStorageConfig();
    
    // Пытаемся подключиться к домашней сети если настроено
    if (useLocalNetwork) {
        WiFi.begin();
        unsigned long startTime = millis();
        
        Serial.println("Connecting to local network...");
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            localIP = WiFi.localIP().toString();
            Serial.println("Connected to local network! IP: " + localIP);
        } else {
            useLocalNetwork = false;
            Serial.println("Failed to connect to local network, starting AP mode");
        }
    }
    
    // Если не подключились к локальной сети, запускаем точку доступа
    if (!useLocalNetwork) {
        WiFi.softAP(wifiStorageSSID.c_str(), wifiStoragePassword.c_str());
        localIP = WiFi.softAPIP().toString();
        Serial.println("AP started: " + wifiStorageSSID + " IP: " + localIP);
    }
    
    // Настройка HTTP сервера
    server.on("/", handleRoot);
    server.on("/list", handleList);
    server.on("/download", handleDownload);
    server.on("/read", handleRead);
    server.on("/write", HTTP_POST, handleWrite);
    server.on("/delete", HTTP_DELETE, handleDelete);
    server.on("/upload", HTTP_POST, []() {
        server.send(200, "text/plain", "Upload complete");
    }, handleUpload);
    
    // Обработчик для favicon (чтобы избежать ошибок 404)
    server.on("/favicon.ico", []() {
        server.send(404, "text/plain", "No favicon");
    });
    
    server.begin();
    Serial.println("HTTP server started");
    
    // Запуск mDNS для удобного доступа
    if (!MDNS.begin("flopa")) {
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.println("mDNS responder started: flopa.local");
    }
    
    isWiFiStorageActive = true;
    return true;
}
void stopWiFiStorage() {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect();
    MDNS.end();
    isWiFiStorageActive = false;
    Serial.println("WiFi Storage stopped");
}
// Интерфейс для меню
void drawWiFiStorageScreen(bool forceRedraw = false) {
    static unsigned long lastRedraw = 0;
    static bool lastState = isWiFiStorageActive;
    
    if (!forceRedraw && millis() - lastRedraw < 500 && lastState == isWiFiStorageActive) {
        return;
    }
    
    tft.fillScreen(BLACK);
    
    // Заголовок
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(30, 10);
    tft.print("WiFi Storage");
    tft.drawLine(0, 35, 240, 35, WHITE);
    
    if (!SDCardManager::isCardPresent()) {
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(20, 60);
        tft.print("SD Card Not Found!");
        lastRedraw = millis();
        return;
    }
    
    // Информация о подключении
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    
    tft.setCursor(10, 50);
    tft.print("Mode: ");
    tft.setTextColor(useLocalNetwork ? GREEN : YELLOW);
    tft.print(useLocalNetwork ? "Local Network" : "Access Point");
    
    tft.setTextColor(WHITE);
    tft.setCursor(10, 70);
    tft.print("SSID: ");
    tft.print(wifiStorageSSID);
    
    tft.setCursor(10, 90);
    tft.print("Status: ");
    tft.setTextColor(isWiFiStorageActive ? GREEN : RED);
    tft.print(isWiFiStorageActive ? "ACTIVE" : "INACTIVE");
    
    if (isWiFiStorageActive) {
        tft.setTextColor(WHITE);
        tft.setCursor(10, 110);
        tft.print("IP: ");
        tft.print(localIP);
        
        tft.setCursor(10, 130);
        tft.print("URL: http://");
        tft.print(localIP);
        
        // Информация о SD карте
        uint64_t cardSize = SD_MMC.cardSize();
        uint64_t usedSpace = calculateUsedSpace();
        
        tft.setCursor(10, 160);
        tft.print("SD Card: ");
        if (cardSize < 1024 * 1024 * 1024) {
            tft.print(cardSize / (1024.0 * 1024.0), 1);
            tft.print(" MB");
        } else {
            tft.print(cardSize / (1024.0 * 1024.0 * 1024.0), 1);
            tft.print(" GB");
        }
        
        tft.setCursor(10, 180);
        tft.print("Used: ");
        if (usedSpace < 1024 * 1024 * 1024) {
            tft.print(usedSpace / (1024.0 * 1024.0), 1);
            tft.print(" MB");
        } else {
            tft.print(usedSpace / (1024.0 * 1024.0 * 1024.0), 1);
            tft.print(" GB");
        }
    }
    
    // Подсказки управления
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 210);
    tft.print("OK - ");
    tft.print(isWiFiStorageActive ? "Stop Server" : "Start Server");
    
    tft.setCursor(10, 230);
    tft.print("<- - Back to menu");
    
    tft.setCursor(10, 250);
    tft.print("^ - Refresh");
    
    lastRedraw = millis();
    lastState = isWiFiStorageActive;
}
void handleWiFiStorage() {
    bool inWiFiStorageMenu = true;
    bool needsRedraw = true;
    unsigned long lastButtonPress = 0;
    const unsigned long debounceDelay = 300;
    
    while (inWiFiStorageMenu) {
        unsigned long currentTime = millis();
        
        if (needsRedraw) {
            drawWiFiStorageScreen(true);
            needsRedraw = false;
        }
        
        // Обработка HTTP запросов если сервер активен
        if (isWiFiStorageActive) {
            server.handleClient();
        }
        
        updateButtons();
        
        if (currentTime - lastButtonPress < debounceDelay) {
            delay(50);
            continue;
        }
        
        if (btnOK.fell()) {
            lastButtonPress = currentTime;
            
            if (!isWiFiStorageActive) {
                if (initWiFiStorage()) {
                    Serial.println("WiFi Storage started successfully");
                } else {
                    tft.fillRect(0, 160, 240, 20, BLACK);
                    tft.setTextColor(RED);
                    tft.setCursor(10, 160);
                    tft.print("Startup failed!");
                    delay(1000);
                }
            } else {
                stopWiFiStorage();
                Serial.println("WiFi Storage stopped");
            }
            
            needsRedraw = true;
        }
        
        if (btnUp.fell()) {
            lastButtonPress = currentTime;
            // Обновление информации
            needsRedraw = true;
        }
        
        if (btnLeft.fell()) {
            lastButtonPress = currentTime;
            if (isWiFiStorageActive) {
                // Предупреждение перед выходом
                tft.fillRect(0, 200, 240, 60, BLACK);
                tft.setTextColor(RED);
                tft.setTextSize(2);
                tft.setCursor(10, 210);
                tft.print("WARNING!");
                tft.setTextSize(1);
                tft.setCursor(10, 230);
                tft.print("Server will be stopped");
                tft.setCursor(10, 250);
                tft.print("Press <- again to confirm");
                
                unsigned long warningTime = millis();
                bool confirmed = false;
                bool exitWarning = false;
                
                while (millis() - warningTime < 3000 && !confirmed && !exitWarning) {
                    updateButtons();
                    if (btnLeft.fell()) {
                        confirmed = true;
                    }
                    if (btnOK.fell()) {
                        exitWarning = true;
                    }
                    delay(50);
                }
                
                if (confirmed) {
                    stopWiFiStorage();
                    inWiFiStorageMenu = false;
                } else {
                    needsRedraw = true;
                }
            } else {
                inWiFiStorageMenu = false;
            }
        }
        
        delay(50);
    }
    
    drawSubmenu();
}
void showWiFiStorage() {
    handleWiFiStorage();
}

// ==================== MASS STORAGE MENU ====================
enum StorageMode {
    STORAGE_USB_MSC,
    STORAGE_WIFI
};

StorageMode currentStorageMode = STORAGE_USB_MSC;
bool usbMSCActive = false;

#include <USB.h>
#include <USBMSC.h>
#include <SD_MMC.h>

USBMSC msc;

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize){
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize) return false; // disk error
  log_v("Write lba: %ld\toffset: %ld\tbufsize: %ld", lba, offset, bufsize);
  for (int x=0; x< bufsize/secSize; x++) {
    uint8_t blkbuffer[secSize];
    memcpy(blkbuffer, (uint8_t*)buffer + x*secSize, secSize);
    if (!SD_MMC.writeRAW(blkbuffer, lba + x)) return false;
  }
  return bufsize;
}
static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize){
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize) return false; // disk error
  log_v("Read lba: %ld\toffset: %ld\tbufsize: %ld\tsector: %lu", lba, offset, bufsize, secSize);
  for (int x=0; x < bufsize/secSize; x++) {
    //uint8_t blkbuffer[secSize];
    if (!SD_MMC.readRAW((uint8_t*)buffer + (x * secSize), lba + x)) return false; // outside of volume boundary
  }
  return bufsize;
}
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject){
  log_i("Start/Stop power: %u\tstart: %d\teject: %d", power_condition, start, load_eject);
  return true;
}
void stopmsc() {
    if (usbMSCActive) {
        msc.end();
        usbMSCActive = false;
        Serial.println("USB MSC stopped");
    }
}
void drawMassStorageMenu(bool forceRedraw = false) {
    static unsigned long lastRedraw = 0;
    static StorageMode lastMode = STORAGE_USB_MSC;
    static bool lastUSBState = false;
    static bool lastWiFiState = false;
    
    if (!forceRedraw && millis() - lastRedraw < 300 && 
        lastMode == currentStorageMode && 
        lastUSBState == usbMSCActive && 
        lastWiFiState == isWiFiStorageActive) {
        return;
    }
    
    tft.fillScreen(BLACK);
    
    // Заголовок с иконкой
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(40, 10);
    tft.print("Mass Storage");
    
    // Иконка диска
    tft.fillRoundRect(100, 40, 40, 40, 5, BLUE);
    tft.fillCircle(120, 60, 15, WHITE);
    tft.fillCircle(120, 60, 10, BLUE);
    
    // Выбор режима
    tft.setTextSize(1);
    tft.setTextColor(WHITE);
    tft.setCursor(10, 90);
    tft.print("Mode: ");
    
    // Визуальный переключатель режимов
    tft.fillRect(60, 86, 120, 30, DARK_GREY);
    tft.drawRect(60, 86, 60, 30, WHITE);
    tft.drawRect(120, 86, 60, 30, WHITE);
    
    if (currentStorageMode == STORAGE_USB_MSC) {
        tft.fillRect(61, 87, 58, 28, BLUE);
        tft.setTextColor(BLACK);
        tft.setCursor(75, 94);
        tft.print("USB");
        tft.setTextColor(WHITE);
        tft.setCursor(135, 94);
        tft.print("WiFi");
    } else {
        tft.fillRect(121, 87, 58, 28, GREEN);
        tft.setTextColor(WHITE);
        tft.setCursor(75, 94);
        tft.print("USB");
        tft.setTextColor(BLACK);
        tft.setCursor(135, 94);
        tft.print("WiFi");
    }
    
    // Информация о текущем режиме
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 130);
    tft.print("Status: ");
    
    if (currentStorageMode == STORAGE_USB_MSC) {
        if (usbMSCActive) {
            tft.setTextColor(GREEN);
            tft.print("ACTIVE - USB Connected");
        } else {
            tft.setTextColor(YELLOW);
            tft.print("READY - Connect USB cable");
        }
    } else {
        if (isWiFiStorageActive) {
            tft.setTextColor(GREEN);
            tft.print("ACTIVE - IP: " + localIP);
        } else {
            tft.setTextColor(YELLOW);
            tft.print("READY - Press OK to start");
        }
    }
    
    // Детальная информация
    tft.setTextColor(WHITE);
    tft.setCursor(10, 150);
    
    if (currentStorageMode == STORAGE_USB_MSC) {
        tft.print("Connect via USB cable to");
        tft.setCursor(10, 165);
        tft.print("access SD card as drive");
        tft.setCursor(10, 180);
        tft.print("Speed: USB 2.0 (480 Mbps)");
    } else {
        tft.print("Access files wirelessly");
        tft.setCursor(10, 165);
        tft.print("via web browser");
        tft.setCursor(10, 180);
        if (isWiFiStorageActive) {
            tft.print("URL: http://" + localIP);
        } else {
            tft.print("SSID: " + wifiStorageSSID);
        }
    }
    
    // Информация о SD карте
    if (SDCardManager::isCardPresent()) {
        uint64_t cardSize = SD_MMC.cardSize();
        uint64_t usedSpace = calculateUsedSpace();
        uint64_t freeSpace = cardSize - usedSpace;
        
        tft.drawRect(10, 200, 220, 30, WHITE);
        tft.setTextColor(LIGHT_GREY);
        tft.setCursor(15, 207);
        tft.print("SD Card: ");
        
        // Прогресс-бар использования
        int usedPercent = (cardSize > 0) ? (usedSpace * 100 / cardSize) : 0;
        int barWidth = map(usedPercent, 0, 100, 0, 216);
        
        if (usedPercent > 90) {
            tft.fillRect(12, 222, barWidth, 6, RED);
        } else if (usedPercent > 70) {
            tft.fillRect(12, 222, barWidth, 6, YELLOW);
        } else {
            tft.fillRect(12, 222, barWidth, 6, GREEN);
        }
        
        // Текст с размерами
        tft.setCursor(15, 222);
        tft.print("Used: ");
        
        if (usedSpace < 1024 * 1024) {
            tft.print(usedSpace / 1024.0, 1);
            tft.print("KB / ");
        } else if (usedSpace < 1024 * 1024 * 1024) {
            tft.print(usedSpace / (1024.0 * 1024.0), 1);
            tft.print("MB / ");
        } else {
            tft.print(usedSpace / (1024.0 * 1024.0 * 1024.0), 1);
            tft.print("GB / ");
        }
        
        if (cardSize < 1024 * 1024 * 1024) {
            tft.print(cardSize / (1024.0 * 1024.0), 1);
            tft.print("MB");
        } else {
            tft.print(cardSize / (1024.0 * 1024.0 * 1024.0), 1);
            tft.print("GB");
        }
        
        tft.setCursor(180, 207);
        tft.printf("%d%%", usedPercent);
    } else {
        tft.setTextColor(RED);
        tft.setCursor(10, 210);
        tft.print("SD Card not detected!");
    }
    
    // Подсказки управления
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 250);
    tft.print("<- USB");
    
    tft.setCursor(100, 250);
    tft.print("OK Start/Stop");
    
    tft.setCursor(200, 250);
    tft.print("WiFi ->");
    
    tft.setCursor(10, 280);
    if (currentStorageMode == STORAGE_USB_MSC) {
        tft.print("<- Back to Menu");
    } else {
        tft.print("<- Stop & Back");
    }
    
    lastRedraw = millis();
    lastMode = currentStorageMode;
    lastUSBState = usbMSCActive;
    lastWiFiState = isWiFiStorageActive;
}
void startmsc() {
    if (usbMSCActive) {
        return;
    }
    
    if (!SDCardManager::isCardPresent()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("SD Card not found!");
        delay(2000);
        return;
    }
    
    Serial.println("Initializing USB MSC");
    
    // Останавливаем WiFi Storage если активен
    if (isWiFiStorageActive) {
        stopWiFiStorage();
    }
    
    // Инициализируем USB MSC
    msc.vendorID("FLOPA");
    msc.productID("USB_MASS_STORAGE");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    
    if (msc.begin(SD_MMC.numSectors(), SD_MMC.sectorSize())) {
        USB.begin();
        usbMSCActive = true;
        
        Serial.println("USB MSC initialized successfully");
        Serial.printf("SD Size: %lluMB\n", SD_MMC.cardSize() / (1024 * 1024));
        
        // Визуальная обратная связь
        tft.fillScreen(BLACK);
        tft.setTextColor(GREEN);
        tft.setTextSize(2);
        tft.setCursor(50, 100);
        tft.print("USB MSC ACTIVE");
        
        tft.setTextSize(1);
        tft.setTextColor(WHITE);
        tft.setCursor(30, 140);
        tft.print("Connect USB cable to access");
        tft.setCursor(40, 160);
        tft.print("SD card as removable drive");
        
        tft.setTextColor(YELLOW);
        tft.setCursor(60, 200);
        tft.print("Press <- to stop");
        
        delay(1000);
    } else {
        Serial.println("USB MSC initialization failed");
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(30, 100);
        tft.print("USB MSC Failed!");
        delay(2000);
    }
}
void handleMassStorageMenu() {
    bool inMassStorageMenu = true;
    bool needsRedraw = true;
    unsigned long lastButtonPress = 0;
    const unsigned long debounceDelay = 300;
    
    while (inMassStorageMenu) {
        unsigned long currentTime = millis();
        
        // Рисуем меню при необходимости
        if (needsRedraw) {
            drawMassStorageMenu(true);
            needsRedraw = false;
        }
        
        // Обработка HTTP запросов если WiFi Storage активен
        if (isWiFiStorageActive) {
            server.handleClient();
        }
        
        updateButtons();
        
        if (currentTime - lastButtonPress < debounceDelay) {
            delay(50);
            continue;
        }
        
        // Переключение между режимами
        if (btnRight.fell()) {
            lastButtonPress = currentTime;
            currentStorageMode = STORAGE_WIFI;
            needsRedraw = true;
        }
        
        if (btnLeft.fell()) {
            lastButtonPress = currentTime;
            currentStorageMode = STORAGE_USB_MSC;
            needsRedraw = true;
        }
        
        // Запуск/остановка выбранного режима
        if (btnOK.fell()) {
            lastButtonPress = currentTime;
            
            if (currentStorageMode == STORAGE_USB_MSC) {
                if (!usbMSCActive) {
                    startmsc();
                } else {
                    stopmsc();
                }
                needsRedraw = true;
            } else {
                if (!isWiFiStorageActive) {
                    if (initWiFiStorage()) {
                        Serial.println("WiFi Storage started successfully");
                    } else {
                        tft.fillRect(0, 160, 240, 20, BLACK);
                        tft.setTextColor(RED);
                        tft.setCursor(10, 160);
                        tft.print("Startup failed!");
                        delay(1000);
                    }
                } else {
                    stopWiFiStorage();
                    Serial.println("WiFi Storage stopped");
                }
                needsRedraw = true;
            }
        }
        
        // Выход из меню (кнопка LEFT с задержкой для WiFi режима)
        static unsigned long leftPressTime = 0;
        if (btnLeft.read() == LOW) {
            if (leftPressTime == 0) {
                leftPressTime = millis();
            } else if (millis() - leftPressTime > 1000) {
                // Долгое нажатие - выход
                if (currentStorageMode == STORAGE_WIFI && isWiFiStorageActive) {
                    // Предупреждение для WiFi режима
                    tft.fillRect(0, 200, 240, 60, BLACK);
                    tft.setTextColor(RED);
                    tft.setTextSize(2);
                    tft.setCursor(10, 210);
                    tft.print("WARNING!");
                    tft.setTextSize(1);
                    tft.setCursor(10, 230);
                    tft.print("Stop WiFi storage first");
                    tft.setCursor(10, 250);
                    tft.print("Press OK then <-");
                    
                    unsigned long warningTime = millis();
                    bool confirmed = false;
                    
                    while (millis() - warningTime < 3000 && !confirmed) {
                        updateButtons();
                        if (btnOK.fell()) {
                            stopWiFiStorage();
                            confirmed = true;
                        }
                        delay(50);
                    }
                }
                
                // Останавливаем USB MSC если активен
                if (usbMSCActive) {
                    stopmsc();
                }
                
                inMassStorageMenu = false;
                leftPressTime = 0;
            }
        } else {
            leftPressTime = 0;
        }
        
        delay(50);
    }
    
    // Возвращаемся в подменю
    drawSubmenu();
}

//===================JAMMER================+++++==========
bool JAM = false;
byte bluetooth_channels[] = {0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 32, 34, 46, 48, 50, 52, 74, 76, 78, 80};
SPIClass *hp = nullptr;
RF24 radio(17, 18, 16000000);
int ch = 46;
void changeChannel() {
  radio.setChannel(bluetooth_channels[random(21)]);
}
void initHP() {
  esp_bt_controller_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_wifi_disconnect();
  hp = new SPIClass(FSPI);
  hp->begin();
  if (radio.begin(hp)) {
    Serial.println("HP Started !!!");
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.printPrettyDetails();
    radio.startConstCarrier(RF24_PA_MAX, ch);
  } else {
    Serial.println("HP initialization failed!");
  }
}
void restoreAfterHP() {
  radio.stopConstCarrier();
  esp_restart();
}
void BTJamming() {
    initHP();
    radio.setChannel(10);
     // Логирование начала атаки
    logAttack("BT_JAMMING", "CHANNEL_10", "STARTED");
    bool jammingActive = true;
    bool needsRedraw = true;
    bool paused = false;
    
    while (jammingActive) {
        // Обновляем экран только при необходимости
        if (needsRedraw) {
            tft.fillScreen(BLACK);
            
            // Заголовок
            tft.setTextColor(RED);
            tft.setTextSize(2);
            tft.setCursor(80, 20);
            tft.print("BT JAMMING");
            
            // Статус
            tft.setTextColor(paused ? YELLOW : GREEN);
            tft.setTextSize(2);
            tft.setCursor(100, 60);
            tft.print(paused ? "PAUSED" : "ACTIVE");
            
            // Текущий канал
            tft.setTextColor(WHITE);
            tft.setTextSize(1);
            tft.setCursor(50, 100);
            tft.print("Channel: ");
            tft.setTextSize(2);
            tft.print("10");
            
            // Уровень сигнала
            tft.setTextSize(1);
            tft.setCursor(50, 130);
            tft.print("Power: MAX");
            
            // Визуальный индикатор
            tft.drawRect(50, 150, 220, 20, GREEN);
            if (!paused) {
                tft.fillRect(50, 150, 220, 20, RED);
            }
            
            // Подсказки
            tft.setTextColor(LIGHT_GREY);
            tft.setCursor(50, 180);
            tft.print("<- Stop jamming");
            tft.setCursor(50, 200);
            tft.print("OK Pause/Resume");
            
            needsRedraw = false;
        }
        
        updateButtons();
        
        if (btnLeft.fell()) {
            jammingActive = false;
            // Логирование остановки атаки
            logAttack("BT_JAMMING", "CHANNEL_10", "STOPPED_BY_USER");
            radio.stopConstCarrier();
            needsRedraw = true;
        }
        
        if (btnOK.fell()) {
            paused = !paused;
            if (paused) {
                radio.stopConstCarrier();
            } else {
                radio.startConstCarrier(RF24_PA_MAX, 10);
            }
            needsRedraw = true;
        }
        
        delay(50);
    }
    
    radio.stopConstCarrier();
    restoreAfterHP();
    drawSubmenu();
}
void startJamming(){
    JAM = true;
    
    // Логирование начала атаки
    logAttack("JAMMING_2.4GHz", "ALL_CHANNELS", "STARTED");
    
    initHP();
    Serial.println("JAMJAM");
    
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    tft.setCursor(20, 90);
    tft.print("2.4 GHz jamming");
    tft.setCursor(20, 110);
    tft.print("is ACTIVE");
    tft.setTextColor(WHITE);
    tft.setCursor(20, 140);
    tft.print("<- to stop attack");
    
    while(JAM){
        changeChannel();
        btnLeft.update();
        if(btnLeft.fell()){
            JAM = false;
            radio.stopConstCarrier();
            // Логирование остановки атаки
            logAttack("JAMMING_2.4GHz", "ALL_CHANNELS", "STOPPED_BY_USER");
            break;
        }
    }
    restoreAfterHP();
}
void SelectiveJam() {
    // Режимы работы
    enum JamMode { SINGLE_CHANNEL, CHANNEL_RANGE, ALL_CHANNELS };
    JamMode currentMode = SINGLE_CHANNEL;
    
    // Параметры jamming
    int currentChannel = 10;
    int rangeStart = 0;
    int rangeEnd = 20;
    bool isJamming = false;
    bool inMenu = true;
    bool needsFullRedraw = true;
    bool needsPartialRedraw = false;
    unsigned long lastChannelChange = 0;
    const unsigned long channelSwitchInterval = 100;

    // Переменные для визуализации
    int channelPos, startPos, endPos, currentPos, allPos;
    int lastChannelPos = -1;
    
    // Объявляем переменные здесь, чтобы они были видны во всех case
    int drawX, drawWidth, currentDrawX, currentDrawWidth;
    
    logAttack("SELECTIVE_JAM", "CUSTOM_CHANNELS", "STARTED");
    
    while (inMenu) {
        // Полная перерисовка экрана при необходимости
        if (needsFullRedraw) {
            tft.fillScreen(BLACK);
            
            // Заголовок
            tft.setTextColor(MAGENTA);
            tft.setTextSize(2);
            tft.setCursor(70, 10);
            tft.print("SELECTIVE JAM");
            
            // Режим работы
            tft.setTextSize(1);
            tft.setCursor(10, 40);
            tft.print("Mode: ");
            switch(currentMode) {
                case SINGLE_CHANNEL:
                    tft.setTextColor(CYAN);
                    tft.print("Single Channel");
                    break;
                case CHANNEL_RANGE:
                    tft.setTextColor(YELLOW);
                    tft.print("Channel Range");
                    break;
                case ALL_CHANNELS:
                    tft.setTextColor(RED);
                    tft.print("All Channels");
                    break;
            }
            
            // Статус jamming
            tft.setTextSize(1);
            tft.setCursor(200, 40);
            tft.print("Status: ");
            tft.setTextColor(isJamming ? GREEN : RED);
            tft.print(isJamming ? "ACTIVE" : "PAUSED");
            
            // Отображение в зависимости от режима
            switch(currentMode) {
                case SINGLE_CHANNEL:
                    // Одиночный канал
                    tft.setTextColor(WHITE);
                    tft.setTextSize(1);
                    tft.setCursor(10, 70);
                    tft.print("Current Channel:");
                    
                    // Область для отображения номера канала
                    tft.setTextSize(3);
                    tft.setCursor(140, 65);
                    tft.setTextColor(CYAN);
                    tft.print(currentChannel);
                    
                    // Визуальный индикатор
                    tft.drawRect(10, 100, 300, 20, WHITE);
                    break;
                    
                case CHANNEL_RANGE:
                    // Диапазон каналов
                    tft.setTextColor(WHITE);
                    tft.setTextSize(1);
                    tft.setCursor(10, 70);
                    tft.print("Range: ");
                    tft.setTextColor(YELLOW);
                    tft.print(rangeStart);
                    tft.print(" - ");
                    tft.print(rangeEnd);
                    
                    tft.setTextSize(1);
                    tft.setCursor(10, 90);
                    tft.setTextColor(WHITE);
                    tft.print("Current: ");
                    tft.setTextColor(YELLOW);
                    tft.print(currentChannel);
                    
                    // Визуальный индикатор диапазона
                    tft.drawRect(10, 110, 300, 20, WHITE);
                    break;
                    
                case ALL_CHANNELS:
                    // Все каналы
                    tft.setTextColor(WHITE);
                    tft.setTextSize(1);
                    tft.setCursor(10, 70);
                    tft.print("Scanning all 84 channels");
                    
                    tft.setTextSize(1);
                    tft.setCursor(10, 90);
                    tft.print("Current: ");
                    tft.setTextColor(RED);
                    tft.print(currentChannel);
                    
                    // Визуальный индикатор
                    tft.drawRect(10, 110, 300, 20, WHITE);
                    break;
            }
            
            // Диапазон каналов (0-83)
            tft.setTextSize(1);
            tft.setTextColor(LIGHT_GREY);
            tft.setCursor(10, 135);
            tft.print("0");
            tft.setCursor(295, 135);
            tft.print("83");
            
            // Подсказки управления
            tft.setTextColor(LIGHT_GREY);
            tft.setTextSize(1);
            
            tft.setCursor(10, 160);
            tft.print("^v Adjust value");
            
            tft.setCursor(10, 180);
            tft.print("-> Change mode");
            
            tft.setCursor(10, 200);
            tft.print("OK Toggle jam");
            
            tft.setCursor(10, 220);
            tft.print("<- Exit");
            
            // Режим-specific подсказки
            if (currentMode == CHANNEL_RANGE) {
                tft.setCursor(150, 160);
                tft.print("L:Start R:End");
            }
            
            needsFullRedraw = false;
            needsPartialRedraw = true;
        }
        
        // Частичная перерисовка (только курсор, номер канала, диапазон и статус)
        if (needsPartialRedraw) {
            // Обновляем статус jamming
            tft.fillRect(245, 40, 70, 10, BLACK);
            tft.setTextSize(1);
            tft.setCursor(200, 40);
            tft.print("Status: ");
            tft.setTextColor(isJamming ? GREEN : RED);
            tft.print(isJamming ? "ACTIVE" : "PAUSED");
            
            switch(currentMode) {
                case SINGLE_CHANNEL:
                    // Очищаем предыдущую позицию курсора (включая крайние случаи)
                    if (lastChannelPos != -1) {
                        // Очищаем область курсора с учетом границ
                        drawX = (lastChannelPos - 2 > 11) ? (lastChannelPos - 2) : 11; // Не выходим за левую границу
                        drawWidth = 4;
                        if (drawX + drawWidth > 309) drawWidth = 309 - drawX; // Не выходим за правую границу
                        tft.fillRect(drawX, 100, drawWidth, 20, BLACK);
                        // Восстанавливаем границу индикатора
                        tft.drawRect(10, 100, 300, 20, WHITE);
                    }
                    
                    // Рисуем новую позицию курсора с учетом границ
                    channelPos = map(currentChannel, 0, 83, 10, 310);
                    drawX = (channelPos - 2 > 11) ? (channelPos - 2) : 11; // Не выходим за левую границу
                    drawWidth = 4;
                    if (drawX + drawWidth > 309) drawWidth = 309 - drawX; // Не выходим за правую границу
                    tft.fillRect(drawX, 100, drawWidth, 20, RED);
                    lastChannelPos = channelPos;
                    
                    // Обновляем номер канала - очищаем всю область номера (сдвинуто влево)
                    tft.fillRect(130, 65, 90, 25, BLACK);
                    tft.setTextSize(3);
                    tft.setCursor(140, 65);
                    tft.setTextColor(CYAN);
                    tft.print(currentChannel);
                    break;
                    
                case CHANNEL_RANGE:
                    // Очищаем весь индикатор диапазона
                    tft.fillRect(11, 111, 298, 18, BLACK);
                    tft.drawRect(10, 110, 300, 20, WHITE);
                    
                    // Рисуем диапазон с учетом границ
                    startPos = map(rangeStart, 0, 83, 10, 310);
                    startPos = (startPos > 11) ? startPos : 11;
                    endPos = map(rangeEnd, 0, 83, 10, 310);
                    endPos = (endPos < 309) ? endPos : 309;
                    if (startPos <= endPos) {
                        tft.fillRect(startPos, 110, endPos - startPos, 20, BLUE);
                    }
                    
                    // Рисуем текущую позицию в диапазоне с учетом границ
                    currentPos = map(currentChannel, 0, 83, 10, 310);
                    currentDrawX = (currentPos - 2 > 11) ? (currentPos - 2) : 11;
                    currentDrawWidth = 4;
                    if (currentDrawX + currentDrawWidth > 309) currentDrawWidth = 309 - currentDrawX;
                    tft.fillRect(currentDrawX, 110, currentDrawWidth, 20, RED);
                    lastChannelPos = currentPos;
                    
                    // Обновляем отображение диапазона - очищаем только числа (сдвинуто влево)
                    tft.fillRect(40, 70, 70, 10, BLACK);
                    tft.setCursor(10, 70);
                    tft.setTextColor(WHITE);
                    tft.print("Range: ");
                    tft.setTextColor(YELLOW);
                    tft.print(rangeStart);
                    tft.print(" - ");
                    tft.print(rangeEnd);
                    
                    // Обновляем отображение текущего канала - очищаем только число (сдвинуто влево)
                    tft.fillRect(60, 90, 45, 10, BLACK);
                    tft.setCursor(10, 90);
                    tft.setTextColor(WHITE);
                    tft.print("Current: ");
                    tft.setTextColor(YELLOW);
                    tft.print(currentChannel);
                    break;
                    
                case ALL_CHANNELS:
                    // Очищаем весь индикатор
                    tft.fillRect(11, 111, 298, 18, BLACK);
                    tft.drawRect(10, 110, 300, 20, WHITE);
                    
                    // Рисуем новую позицию с учетом границ
                    allPos = map(currentChannel, 0, 83, 10, 310);
                    allPos = (allPos > 11) ? ((allPos < 309) ? allPos : 309) : 11; // Ограничиваем в пределах рамки
                    tft.fillRect(10, 110, allPos, 20, GREEN);
                    lastChannelPos = allPos;
                    
                    // Обновляем отображение текущего канала - очищаем только число (сдвинуто влево)
                    tft.fillRect(60, 90, 45, 10, BLACK);
                    tft.setCursor(10, 90);
                    tft.setTextColor(WHITE);
                    tft.print("Current: ");
                    tft.setTextColor(RED);
                    tft.print(currentChannel);
                    break;
            }
            
            needsPartialRedraw = false;
        }
        
        updateButtons();
        
        // Обработка jamming
        if (isJamming) {
            if (millis() - lastChannelChange > channelSwitchInterval) {
                switch(currentMode) {
                    case SINGLE_CHANNEL:
                        radio.setChannel(currentChannel);
                        break;
                    case CHANNEL_RANGE:
                        currentChannel++;
                        if (currentChannel > rangeEnd) currentChannel = rangeStart;
                        radio.setChannel(currentChannel);
                        break;
                    case ALL_CHANNELS:
                        currentChannel = (currentChannel + 1) % 84;
                        radio.setChannel(currentChannel);
                        break;
                }
                lastChannelChange = millis();
                needsPartialRedraw = true;
            }
        }
        
        // Обработка кнопок
        if (btnRight.fell()) {
            currentMode = static_cast<JamMode>((currentMode + 1) % 3);
            if (isJamming) {
                radio.stopConstCarrier();
                isJamming = false;
            }
            needsFullRedraw = true;
            needsPartialRedraw = false;
        }
        
        if (btnOK.fell()) {
            isJamming = !isJamming;
            if (isJamming) {
                radio.startConstCarrier(RF24_PA_MAX, currentChannel);
            } else {
                radio.stopConstCarrier();
            }
            needsPartialRedraw = true;
        }
        
        if (btnUp.fell()) {
            switch(currentMode) {
                case SINGLE_CHANNEL:
                    currentChannel = (currentChannel + 1) % 84;
                    if (isJamming) radio.setChannel(currentChannel);
                    needsPartialRedraw = true;
                    break;
                case CHANNEL_RANGE:
                    if (btnLeft.read() == LOW) {
                        rangeStart = min(rangeStart + 1, rangeEnd);
                    } else if (btnRight.read() == LOW) {
                        rangeEnd = min(rangeEnd + 1, 83);
                    } else {
                        rangeEnd = min(rangeEnd + 1, 83);
                    }
                    needsPartialRedraw = true;
                    break;
            }
        }
        
        if (btnDown.fell()) {
            switch(currentMode) {
                case SINGLE_CHANNEL:
                    currentChannel = (currentChannel - 1 + 84) % 84;
                    if (isJamming) radio.setChannel(currentChannel);
                    needsPartialRedraw = true;
                    break;
                case CHANNEL_RANGE:
                    if (btnLeft.read() == LOW) {
                        rangeStart = max(rangeStart - 1, 0);
                    } else if (btnRight.read() == LOW) {
                        rangeEnd = max(rangeEnd - 1, rangeStart);
                    } else {
                        rangeEnd = max(rangeEnd - 1, rangeStart);
                    }
                    needsPartialRedraw = true;
                    break;
            }
        }
        
        if (btnLeft.fell()) {
            inMenu = false;
            if (isJamming) {
                radio.stopConstCarrier();
            }
        }
        
        delay(50);
    }
    logAttack("SELECTIVE_JAM", "CUSTOM_CHANNELS", "STOPPED_BY_USER");
    radio.stopConstCarrier();
    restoreAfterHP();
    drawSubmenu();
}

// ==================== LOOKING GLASS ====================
#define MAX_CHANNELS 14
#define HISTORY_SIZE 100
#define GRAPH_HEIGHT 140
#define GRAPH_TOP 50
#define GRAPH_BOTTOM (GRAPH_TOP + GRAPH_HEIGHT)
#define CHANNEL_WIDTH 20
#define CHANNEL_SPACING 3


typedef struct {
    int rssi[MAX_CHANNELS][HISTORY_SIZE];
    int historyIndex;
    bool enabledChannels[MAX_CHANNELS];
    int filterLevel; // 0: нет, 1: легкий, 2: средний
    bool isScanning;
    unsigned long lastScanTime;
    unsigned long lastButtonPress;
    bool buttonHeld;
    bool showManual; // Флаг для показа мануала
} LookingGlassState;

LookingGlassState lgState;

uint16_t getSignalColor(int rssi) {
    if (rssi >= -50) return GREEN;
    if (rssi >= -60) return YELLOW;
    if (rssi >= -70) return 0xFD20; // Orange
    if (rssi >= -80) return RED;
    return MAGENTA; // Very weak
}
void initLookingGlass() {
    memset(&lgState, 0, sizeof(lgState));
    
    // Включаем все каналы по умолчанию
    for (int i = 0; i < MAX_CHANNELS; i++) {
        lgState.enabledChannels[i] = true;
    }
    
    lgState.filterLevel = 1; // Легкий фильтр по умолчанию
    lgState.isScanning = false;
    lgState.historyIndex = 0;
    lgState.buttonHeld = false;
    lgState.lastButtonPress = 0;
    lgState.showManual = true; // Показывать манул при первом входе
}
void applyFilter(int channel, int& value) {
    if (lgState.filterLevel == 0 || lgState.historyIndex < 5) return;
    
    // Легкий фильтр: скользящее среднее по 3 samples
    if (lgState.filterLevel == 1) {
        int sum = 0;
        int count = 0;
        for (int i = 0; i < 3; i++) {
            int idx = (lgState.historyIndex - i + HISTORY_SIZE) % HISTORY_SIZE;
            if (lgState.rssi[channel][idx] > -100) {
                sum += lgState.rssi[channel][idx];
                count++;
            }
        }
        if (count > 0) {
            value = (value * 2 + sum/count) / 3;
        }
    }
    // Средний фильтр: скользящее среднее по 5 samples
    else if (lgState.filterLevel == 2) {
        int sum = 0;
        int count = 0;
        for (int i = 0; i < 5; i++) {
            int idx = (lgState.historyIndex - i + HISTORY_SIZE) % HISTORY_SIZE;
            if (lgState.rssi[channel][idx] > -100) {
                sum += lgState.rssi[channel][idx];
                count++;
            }
        }
        if (count > 0) {
            value = (value + sum/count) / 2;
        }
    }
}
void scanAllChannels() {
    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, true, false, 50); // Быстрое сканирование
    
    // Собираем максимальный RSSI для каждого канала
    int channelRssi[MAX_CHANNELS];
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        channelRssi[ch] = -100;
    }
    
    for (int i = 0; i < n; i++) {
        int channel = WiFi.channel(i) - 1;
        if (channel >= 0 && channel < MAX_CHANNELS) {
            channelRssi[channel] = max(channelRssi[channel], WiFi.RSSI(i));
        }
    }
    
    // Заполняем историю
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        int rssi = channelRssi[ch];
        if (rssi == -100) {
            // Нет сетей на канале - добавляем случайный шум
            rssi = random(-95, -75);
        }
        
        // Применяем фильтр
        applyFilter(ch, rssi);
        
        lgState.rssi[ch][lgState.historyIndex] = rssi;
    }
    
    lgState.historyIndex = (lgState.historyIndex + 1) % HISTORY_SIZE;
}
void drawLookingGlassGraph() {
  // Очищаем область графика
  tft.fillRect(0, GRAPH_TOP, TFT_WIDTH, GRAPH_HEIGHT, BLACK);
  
  // Рисуем сетку
  tft.setTextColor(LIGHT_GREY);
  tft.setTextSize(1);
  
  // Горизонтальные линии (уровни RSSI)
  for (int y = GRAPH_TOP; y <= GRAPH_BOTTOM; y += 35) {
    tft.drawLine(0, y, TFT_WIDTH, y, DARK_GREY);
    int dbm = map(y, GRAPH_BOTTOM, GRAPH_TOP, -100, -30);
    tft.setCursor(5, y - 5);
    tft.print(dbm);
    tft.print("dB");
  }
  
  // Рисуем текущее состояние (последний скан)
  int currentIndex = (lgState.historyIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  
  for (int ch = 0; ch < MAX_CHANNELS; ch++) {
    if (!lgState.enabledChannels[ch]) continue;
    
    int rssi = lgState.rssi[ch][currentIndex];
    int x = 10 + ch * (CHANNEL_WIDTH + CHANNEL_SPACING);
    
    // Высота столбца пропорциональна силе сигнала
    int height = map(constrain(rssi, -100, -30), -100, -30, 5, GRAPH_HEIGHT - 10);
    int y = GRAPH_BOTTOM - height;
    
    // Рисуем столбец
    uint16_t color = getSignalColor(rssi);
    tft.fillRect(x, y, CHANNEL_WIDTH, height, color);
    
    // Подпись канала
    tft.setTextColor(WHITE);
    tft.setCursor(x + 7, GRAPH_BOTTOM + 2);
    tft.print(ch + 1);
  }
}
void drawLookingGlassManual() {
    tft.fillScreen(BLACK);
    
    // Заголовок
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(40, 10);
    tft.print("MANUAL");
    
    // Инструкции
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    
    tft.setCursor(10, 40);
    tft.print("OK - Start/Stop scanning");
    
    tft.setCursor(10, 60);
    tft.print("UP - Change filter");
    
    tft.setCursor(10, 80);
    tft.print("RIGHT - Channel selection");
    
    tft.setCursor(10, 100);
    tft.print("LEFT - Quick exit");
    tft.setCursor(10, 115);
    tft.print("(hold for 0.5s)");
    
    tft.setCursor(10, 135);
    tft.print("Graph shows:");
    tft.setCursor(10, 150);
    tft.print("- Bars: current signal");
    tft.setCursor(10, 165);
    tft.print("- Dots: signal history");
    
    // Цветовая легенда
    tft.setCursor(10, 185);
    tft.setTextColor(GREEN);
    tft.print("Green: Strong (-50dBm)");
    
    tft.setCursor(10, 200);
    tft.setTextColor(YELLOW);
    tft.print("Yellow: Good (-60dBm)");
    
    tft.setCursor(10, 215);
    tft.setTextColor(RED);
    tft.print("Red: Weak (-70dBm)");
    
    tft.setCursor(10, 230);
    tft.setTextColor(MAGENTA);
    tft.print("Purple: Noise (<-80dBm)");
    
    // Подсказка для продолжения
    tft.setTextColor(WHITE);
    tft.setCursor(60, 260);
    tft.print("Press OK to continue");
    tft.setCursor(70, 275);
    tft.print("<- to exit");
}
void drawLookingGlassUI() {
  tft.fillScreen(BLACK);
  
  // Заголовок
  tft.setTextColor(CYAN);
  tft.setTextSize(2);
  tft.setCursor(100, 10);
  tft.print("Spectrum Analyzer");
  
  // Статус сканирования
  tft.setTextSize(1);
  tft.setCursor(20, 13);
  tft.print(lgState.isScanning ? "SCAN" : "PAUSE");
  
  // Легенда уровней сигнала
  tft.setTextSize(1);
  tft.setCursor(10, 35);
  tft.setTextColor(GREEN);
  tft.print("Strong");
  tft.setTextColor(YELLOW);
  tft.setCursor(70, 35);
  tft.print("Good");
  tft.setTextColor(RED);
  tft.setCursor(130, 35);
  tft.print("Weak");
  tft.setTextColor(MAGENTA);
  tft.setCursor(190, 35);
  tft.print("Noise");
  
  // Рисуем график
  drawLookingGlassGraph();
  
  // Нижняя панель с информацией
  tft.fillRect(0, GRAPH_BOTTOM + 5, TFT_WIDTH, 30, DARK_GREY);
  
  // Фильтр
  tft.setTextColor(WHITE);
  tft.setCursor(10, GRAPH_BOTTOM + 12);
  tft.print("Filter:");
  const char* filterNames[] = {"None", "Light", "Medium"};
  tft.print(filterNames[lgState.filterLevel]);
  
  // Включенные каналы
  int enabledCount = 0;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (lgState.enabledChannels[i]) enabledCount++;
  }
  tft.setCursor(100, GRAPH_BOTTOM + 12);
  tft.print("Channels:");
  tft.print(enabledCount);
  
  // Подсказки управления
  tft.setCursor(200, GRAPH_BOTTOM + 12);
  tft.print("<- Exit");
}
void handleChannelSelection() {
    bool inChannelSelect = true;
    unsigned long lastRedraw = 0;
    
    while (inChannelSelect) {
        if (millis() - lastRedraw > 300) {
            tft.fillScreen(BLACK);
            tft.setTextColor(YELLOW);
            tft.setTextSize(2);
            tft.setCursor(10, 10);
            tft.print("Channel Selection");
            
            // Рисуем каналы
            for (int ch = 0; ch < MAX_CHANNELS; ch++) {
                int x = (ch % 4) * 60 + 10;
                int y = (ch / 4) * 30 + 50;
                
                if (lgState.enabledChannels[ch]) {
                    tft.fillRoundRect(x, y, 50, 25, 3, getSignalColor(-60));
                    tft.setTextColor(BLACK);
                } else {
                    tft.drawRoundRect(x, y, 50, 25, 3, getSignalColor(-60));
                    tft.setTextColor(WHITE);
                }
                
                tft.setCursor(x + 20, y + 8);
                tft.print(ch + 1);
            }
            
            // Подсказки
            tft.setTextColor(LIGHT_GREY);
            tft.setCursor(10, 200);
            tft.print("OK:Toggle <-:Back");
            tft.setCursor(10, 220);
            tft.print("All:Hold OK+Up");
            tft.setCursor(10, 240);
            tft.print("None:Hold OK+Down");
            
            lastRedraw = millis();
        }
        
        updateButtons();
        
        if (btnOK.fell()) {
            // Переключаем все каналы
            bool allEnabled = true;
            for (int i = 0; i < MAX_CHANNELS; i++) {
                if (!lgState.enabledChannels[i]) {
                    allEnabled = false;
                    break;
                }
            }
            
            for (int i = 0; i < MAX_CHANNELS; i++) {
                lgState.enabledChannels[i] = !allEnabled;
            }
            lastRedraw = 0; // Принудительная перерисовка
        }
        
        if (btnUp.fell()) {
            // Включаем все каналы
            for (int i = 0; i < MAX_CHANNELS; i++) {
                lgState.enabledChannels[i] = true;
            }
            lastRedraw = 0;
        }
        
        if (btnDown.fell()) {
            // Выключаем все каналы
            for (int i = 0; i < MAX_CHANNELS; i++) {
                lgState.enabledChannels[i] = false;
            }
            lastRedraw = 0;
        }
        
        if (btnLeft.fell()) {
            inChannelSelect = false;
        }
        
        delay(50);
    }
}
void handleLookingGlass() {
    bool inLookingGlass = true;
    unsigned long lastScanTime = 0;
    const unsigned long scanInterval = 100;
    const unsigned long exitHoldTime = 500; // 0.5 секунды для выхода

    // Инициализация WiFi для сканирования
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);

    // Показываем мануал при первом входе
    if (lgState.showManual) {
        drawLookingGlassManual();
        lgState.showManual = false;
        
        // Ждем нажатия OK или выхода
        bool inManual = true;
        while (inManual) {
            updateButtons();
            
            if (btnOK.fell()) {
                inManual = false;
                // Первоначальное сканирование
                scanAllChannels();
                drawLookingGlassUI();
            }
            
            if (btnLeft.fell()) {
                inManual = false;
                inLookingGlass = false;
            }
            
            delay(50);
        }
    } else {
        // Первоначальное сканирование
        scanAllChannels();
        drawLookingGlassUI();
    }

    while (inLookingGlass) {
        unsigned long currentTime = millis();
        updateButtons();

        // Автоматическое сканирование
        if (lgState.isScanning && currentTime - lastScanTime > scanInterval) {
            scanAllChannels();
            lastScanTime = currentTime;
            drawLookingGlassUI();
        }

        // Обработка кнопок
        if (btnOK.fell()) {
            lgState.isScanning = !lgState.isScanning;
            lgState.lastButtonPress = currentTime;
            drawLookingGlassUI();
        }

        if (btnRight.fell()) {
            handleChannelSelection();
            drawLookingGlassUI();
        }

        if (btnUp.fell()) {
            lgState.filterLevel = (lgState.filterLevel + 1) % 3;
            lgState.lastButtonPress = currentTime;
            drawLookingGlassUI();
        }

        // Быстрый выход по кнопке LEFT
        if (btnLeft.fell()) {
            lgState.lastButtonPress = currentTime;
            lgState.buttonHeld = true;
        }

        // Проверяем долгое нажатие для выхода
        if (lgState.buttonHeld) {
            if (btnLeft.read() == HIGH) {
                // Кнопка отпущена
                lgState.buttonHeld = false;
                if (currentTime - lgState.lastButtonPress < exitHoldTime) {
                    // Короткое нажатие - меняем фильтр
                    lgState.filterLevel = (lgState.filterLevel + 1) % 3;
                    drawLookingGlassUI();
                }
            } else {
                // Кнопка все еще нажата
                if (currentTime - lgState.lastButtonPress >= exitHoldTime) {
                    // Долгое нажатие - выход
                    inLookingGlass = false;
                    
                    // Визуальная обратная связь
                    tft.fillScreen(BLACK);
                    tft.setTextColor(RED);
                    tft.setTextSize(2);
                    tft.setCursor(80, 140);
                    tft.print("EXIT");
                    delay(300);
                }
            }
        }

        // Обработка кнопки DOWN для повторного показа мануала
        if (btnDown.fell()) {
            drawLookingGlassManual();
            
            // Ждем нажатия для продолжения
            bool showManual = true;
            while (showManual) {
                updateButtons();
                
                if (btnOK.fell() || btnLeft.fell()) {
                    showManual = false;
                    drawLookingGlassUI();
                }
                
                delay(50);
            }
        }

        // Минимальная задержка для стабильности
        delay(10);
    }

    // Восстанавливаем WiFi
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
}

// ==================== NFC FUNCTIONS read and save ==============

// ======================= RFID ARDUINO NANO ========================
#include <HardwareSerial.h>

// Serial коммуникация с Arduino Nano
#define ARDUINO_RX 40
#define ARDUINO_TX 41

HardwareSerial ArduinoRFID(1); // Используем UART1 для общения с Arduino
bool arduinoRFIDInitialized = false;
bool isReadingRFID = false;
String currentRFIDTag = "";
String arduinoRFIDFolder = "/rfid_arduino";

bool initArduinoRFID() {
    Serial.println("Initializing Arduino RFID communication...");
    
    // Инициализируем HardwareSerial на UART1
    ArduinoRFID.begin(9600, SERIAL_8N1, ARDUINO_RX, ARDUINO_TX);
    
    // Проверяем связь
    unsigned long startTime = millis();
    bool connected = false;
    
    while (millis() - startTime < 5000) { // 5 секунд таймаут
        ArduinoRFID.println("PING");
        delay(100);
        
        if (ArduinoRFID.available()) {
            String response = ArduinoRFID.readStringUntil('\n');
            if (response.startsWith("PONG")) {
                connected = true;
                break;
            }
        }
        delay(500);
    }
    
    if (connected) {
        arduinoRFIDInitialized = true;
        Serial.println("Arduino RFID initialized successfully");
        return true;
    } else {
        Serial.println("Arduino RFID initialization failed");
        return false;
    }
}
String sendRFIDCommand(String command, String data = "", unsigned long timeout = 10000) {
    if (!arduinoRFIDInitialized && !initArduinoRFID()) {
        return "ERROR:ARDUINO_NOT_CONNECTED";
    }
    
    String fullCommand = command;
    if (data != "") {
        fullCommand += ":" + data;
    }
    fullCommand += "\n";
    
    ArduinoRFID.print(fullCommand);
    Serial.println("-> Arduino: " + fullCommand);
    
    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        if (ArduinoRFID.available()) {
            String response = ArduinoRFID.readStringUntil('\n');
            response.trim();
            Serial.println("<- Arduino: " + response);
            return response;
        }
        delay(50);
    }
    
    return "ERROR:TIMEOUT";
}
String readArduinoRFIDTag() {
    String response = sendRFIDCommand("READ");
    
    if (response.startsWith("READ_SUCCESS:")) {
        String tagHex = response.substring(13);
        currentRFIDTag = tagHex;
        
        // Логирование обнаружения метки
        if (SDCardManager::isCardPresent() && !incognitoMode) {
            logAttack("RFID_ARDUINO_READ", "TAG_" + tagHex, "DETECTED");
        }
        
        return tagHex;
    } else {
        currentRFIDTag = "";
        return "";
    }
}
bool writeArduinoRFIDTag(String tagHex) {
    if (tagHex.length() != 16) {
        Serial.println("Invalid tag format - must be 16 hex characters");
        return false;
    }
    
    String response = sendRFIDCommand("WRITE", tagHex, 15000); // 15 секунд для записи
    
    if (response.startsWith("WRITE_SUCCESS")) {
        Serial.println("RFID write successful");
        
        // Логирование записи
        if (SDCardManager::isCardPresent() && !incognitoMode) {
            logAttack("RFID_ARDUINO_WRITE", "TAG_" + tagHex, "SUCCESS");
        }
        
        return true;
    } else {
        Serial.println("RFID write failed: " + response);
        return false;
    }
}
void emulateArduinoRFIDTag(String tagHex) {
    if (tagHex.length() != 16) {
        Serial.println("Invalid tag format - must be 16 hex characters");
        return;
    }
    
    String response = sendRFIDCommand("EMULATE", tagHex);
    
    if (response.startsWith("EMULATE_DONE")) {
        Serial.println("RFID emulation completed");
        
        // Логирование эмуляции
        if (SDCardManager::isCardPresent() && !incognitoMode) {
            logAttack("RFID_ARDUINO_EMULATE", "TAG_" + tagHex, "COMPLETED");
        }
    } else {
        Serial.println("RFID emulation failed: " + response);
    }
}
bool saveRFIDTagToSD(const String& tagCode) {
    if (!SDCardManager::isCardPresent()) {
        Serial.println("SD card not present for saving RFID tag");
        return false;
    }
    
    // Создаем папку для RFID меток если её нет
    if (!SD_MMC.exists(arduinoRFIDFolder)) {
        SD_MMC.mkdir(arduinoRFIDFolder);
        Serial.println("Created " + arduinoRFIDFolder + " directory");
    }
    
    // Создаем уникальное имя файла на основе метки
    String filename = arduinoRFIDFolder + "/tag_" + tagCode + ".txt";
    
    Serial.println("Saving RFID tag to: " + filename);
    
    File tagFile = SD_MMC.open(filename, FILE_WRITE);
    if (!tagFile) {
        Serial.println("Failed to create RFID tag file: " + filename);
        return false;
    }
    
    // Сохраняем код метки и дополнительную информацию
    tagFile.println("Arduino RFID Tag");
    tagFile.println("================");
    tagFile.println("Tag HEX: " + tagCode);
    tagFile.println("Capture Time: " + String(millis()));
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        tagFile.println("Date: " + String(timeStr));
    }
    
    tagFile.close();
    
    Serial.println("RFID tag successfully saved to: " + filename);
    return true;
}
void drawArduinoRFIDReaderScreen(bool tagDetected = false, String tagCode = "") {
    tft.fillScreen(BLACK);
    
    // Заголовок
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(50, 10);
    tft.print("Arduino RFID");
    
    // Статус подключения
    tft.setTextSize(1);
    tft.setCursor(10, 35);
    tft.print("Status: ");
    tft.setTextColor(arduinoRFIDInitialized ? GREEN : RED);
    tft.print(arduinoRFIDInitialized ? "CONNECTED" : "DISCONNECTED");
    
    // Инструкция
    tft.setTextColor(WHITE);
    tft.setCursor(10, 50);
    tft.print("Place RFID tag near reader");
    
    // Область статуса
    tft.drawRect(10, 70, 220, 60, WHITE);
    
    if (tagDetected) {
        tft.fillRect(12, 72, 216, 56, GREEN);
        tft.setTextColor(BLACK);
        tft.setCursor(80, 85);
        tft.print("TAG DETECTED");
        
        // Отображение кода метки
        tft.setCursor(20, 105);
        tft.print("HEX: ");
        if (tagCode.length() > 20) {
            tft.print(tagCode.substring(0, 17) + "...");
        } else {
            tft.print(tagCode);
        }
        
    } else {
        // Анимированный индикатор сканирования
        static bool scanIndicator = false;
        if (scanIndicator) {
            tft.fillRect(12, 72, 108, 56, BLUE);
        } else {
            tft.fillRect(120, 72, 108, 56, BLUE);
        }
        
        tft.setTextColor(WHITE);
        tft.setCursor(85, 95);
        tft.print("SCANNING...");
    }
    
    // Информация о модуле
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 140);
    tft.print("Module: Arduino Nano");
    tft.setCursor(10, 155);
    tft.print("Baud: 9600");
    
    // Подсказки управления
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 180);
    tft.print("OK - Save tag to SD");
    tft.setCursor(10, 195);
    tft.print("<- - Back to menu");
    
    tft.setCursor(10, 280);
    tft.print("<- Back  OK Save");
}
void handleArduinoRFIDRead() {
    bool inRFIDReader = true;
    bool tagDetected = false;
    unsigned long lastScanTime = 0;
    const unsigned long scanInterval = 1000;
    unsigned long lastScreenUpdate = 0;
    const unsigned long screenUpdateInterval = 500;
    
    // Инициализация Arduino RFID
    if (!initArduinoRFID()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("Arduino RFID Init Failed!");
        delay(2000);
        return;
    }
    
    // Первоначальная отрисовка
    drawArduinoRFIDReaderScreen(false);
    
    while (inRFIDReader) {
        unsigned long currentTime = millis();
        
        // Сканирование меток
        if (currentTime - lastScanTime > scanInterval) {
            String tag = readArduinoRFIDTag();
            if (tag.length() > 0) {
                currentRFIDTag = tag;
                tagDetected = true;
                drawArduinoRFIDReaderScreen(true, tag);
            }
            lastScanTime = currentTime;
        }
        
        // Обновление индикатора сканирования если метка не обнаружена
        if (!tagDetected && currentTime - lastScreenUpdate > screenUpdateInterval) {
            drawArduinoRFIDReaderScreen(false);
            lastScreenUpdate = currentTime;
        }
        
        updateButtons();
        
        if (btnOK.fell() && tagDetected) {
            // Сохранение метки на SD карту
            if (saveRFIDTagToSD(currentRFIDTag)) {
                tft.fillRect(0, 230, 240, 40, BLACK);
                tft.setTextColor(GREEN);
                tft.setCursor(10, 240);
                tft.print("Tag saved successfully!");
                delay(2000);
                
                // Сброс для чтения следующей метки
                tagDetected = false;
                currentRFIDTag = "";
                drawArduinoRFIDReaderScreen(false);
            } else {
                tft.fillRect(0, 230, 240, 40, BLACK);
                tft.setTextColor(RED);
                tft.setCursor(10, 240);
                tft.print("Save failed!");
                delay(2000);
                drawArduinoRFIDReaderScreen(true, currentRFIDTag);
            }
        }
        
        if (btnLeft.fell()) {
            inRFIDReader = false;
        }
        
        delay(50);
    }
}
// ==================== HELPER FUNCTIONS FOR HEX EDITING ====================
char incrementHexChar(char c) {
    if (c >= '0' && c < '9') return c + 1;
    if (c == '9') return 'A';
    if (c >= 'A' && c < 'F') return c + 1;
    if (c == 'F') return '0';
    return '0';
}
char decrementHexChar(char c) {
    if (c > '0' && c <= '9') return c - 1;
    if (c == '0') return 'F';
    if (c > 'A' && c <= 'F') return c - 1;
    if (c == 'A') return '9';
    return '0';
}
// ==================== ARDUINO RFID WRITE FUNCTION ====================
void drawArduinoRFIDWriteScreen(String tagHex, int cursorPos, bool actionMode) {
    tft.fillScreen(BLACK);
    
    // Заголовок
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(50, 10);
    tft.print("Write RFID Tag");
    
    // HEX строка
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 50);
    
    for (int i = 0; i < 16; i++) {
        if (i < tagHex.length()) {
            if (i == cursorPos && !actionMode) {
                tft.setTextColor(RED); // Курсор
            } else {
                tft.setTextColor(WHITE);
            }
            tft.print(tagHex.charAt(i));
        } else {
            tft.setTextColor(LIGHT_GREY);
            tft.print("_");
        }
        
        // Разделитель каждые 2 символа
        if ((i + 1) % 2 == 0 && i < 15) {
            tft.print(":");
        }
    }
    
    // Подсказки
    tft.setTextSize(1);
    tft.setTextColor(LIGHT_GREY);
    
    if (actionMode) {
        tft.setCursor(10, 90);
        tft.print("OK - Write tag");
        tft.setCursor(10, 105);
        tft.print("<- - Edit tag");
        tft.setCursor(10, 120);
        tft.print("-> - Load last read");
        
        tft.setCursor(10, 280);
        tft.print("<- Edit  OK Write  -> Load");
    } else {
        tft.setCursor(10, 90);
        tft.print("^v - Change character");
        tft.setCursor(10, 105);
        tft.print("<> - Move cursor");
        tft.setCursor(10, 120);
        tft.print("OK - Finish editing");
        
        tft.setCursor(10, 280);
        tft.print("^v Change  <> Move  OK Done");
    }
    
    // Статус валидности
    if (tagHex.length() == 16) {
        tft.setTextColor(GREEN);
        tft.setCursor(180, 80);
        tft.print("VALID");
    } else {
        tft.setTextColor(RED);
        tft.setCursor(180, 80);
        tft.print("INVALID");
    }
}
void handleArduinoRFIDWrite() {
    bool inWriteMenu = true;
    String tagToWrite = "";
    int cursorPos = 0;
    bool editing = true;
    
    // Инициализация Arduino RFID
    if (!initArduinoRFID()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("Arduino RFID Init Failed!");
        delay(2000);
        return;
    }
    
    // Начальный экран ввода
    drawArduinoRFIDWriteScreen("", 0, false);
    
    while (inWriteMenu) {
        updateButtons();
        
        if (editing) {
            // Режим редактирования HEX строки
            if (btnUp.fell()) {
                // Увеличиваем текущий символ
                if (cursorPos < tagToWrite.length()) {
                    char currentChar = tagToWrite.charAt(cursorPos);
                    char newChar = incrementHexChar(currentChar);
                    tagToWrite.setCharAt(cursorPos, newChar);
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, false);
                }
            }
            
            if (btnDown.fell()) {
                // Уменьшаем текущий символ
                if (cursorPos < tagToWrite.length()) {
                    char currentChar = tagToWrite.charAt(cursorPos);
                    char newChar = decrementHexChar(currentChar);
                    tagToWrite.setCharAt(cursorPos, newChar);
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, false);
                }
            }
            
            if (btnRight.fell()) {
                // Перемещаем курсор вправо
                if (cursorPos < 15) {
                    cursorPos++;
                    // Если строка слишком короткая, добавляем символы
                    while (tagToWrite.length() <= cursorPos) {
                        tagToWrite += "0";
                    }
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, false);
                }
            }
            
            if (btnLeft.fell()) {
                // Перемещаем курсор влево
                if (cursorPos > 0) {
                    cursorPos--;
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, false);
                } else {
                    // Выход из режима редактирования
                    editing = false;
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, true);
                }
            }
            
            if (btnOK.fell()) {
                // Переключение между редактированием и действиями
                editing = false;
                drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, true);
            }
        } else {
            // Режим выбора действия
            if (btnOK.fell()) {
                // Запись метки
                if (tagToWrite.length() == 16) {
                    tft.fillScreen(BLACK);
                    tft.setTextColor(YELLOW);
                    tft.setTextSize(2);
                    tft.setCursor(50, 100);
                    tft.print("Writing Tag...");
                    
                    if (writeArduinoRFIDTag(tagToWrite)) {
                        tft.fillScreen(BLACK);
                        tft.setTextColor(GREEN);
                        tft.setTextSize(2);
                        tft.setCursor(50, 100);
                        tft.print("Write Success!");
                        delay(2000);
                    } else {
                        tft.fillScreen(BLACK);
                        tft.setTextColor(RED);
                        tft.setTextSize(2);
                        tft.setCursor(50, 100);
                        tft.print("Write Failed!");
                        delay(2000);
                    }
                    inWriteMenu = false;
                } else {
                    tft.fillScreen(BLACK);
                    tft.setTextColor(RED);
                    tft.setTextSize(2);
                    tft.setCursor(30, 100);
                    tft.print("Invalid tag length!");
                    tft.setCursor(20, 130);
                    tft.print("Must be 16 characters");
                    delay(2000);
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, true);
                }
            }
            
            if (btnLeft.fell()) {
                // Возврат к редактированию
                editing = true;
                drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, false);
            }
            
            if (btnRight.fell()) {
                // Загрузка последней прочитанной метки
                if (currentRFIDTag.length() == 16) {
                    tagToWrite = currentRFIDTag;
                    cursorPos = 0;
                    drawArduinoRFIDWriteScreen(tagToWrite, cursorPos, true);
                }
            }
        }
        
        delay(50);
    }
}
// ==================== ARDUINO RFID EMULATE FUNCTION ====================
void drawArduinoRFIDEmulateScreen(String tagHex, bool emulating) {
    tft.fillScreen(BLACK);
    
    // Заголовок
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.setCursor(50, 10);
    tft.print("Emulate RFID Tag");
    
    // HEX строка
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 50);
    
    for (int i = 0; i < 16; i++) {
        if (i < tagHex.length()) {
            tft.print(tagHex.charAt(i));
        }
        
        // Разделитель каждые 2 символа
        if ((i + 1) % 2 == 0 && i < 15) {
            tft.print(":");
        }
    }
    
    // Статус эмуляции
    tft.setTextSize(2);
    tft.setCursor(80, 90);
    if (emulating) {
        tft.setTextColor(GREEN);
        tft.print("EMULATING...");
    } else {
        tft.setTextColor(YELLOW);
        tft.print("READY");
    }
    
    // Подсказки
    tft.setTextSize(1);
    tft.setTextColor(LIGHT_GREY);
    tft.setCursor(10, 130);
    tft.print("OK - Start emulation");
    tft.setCursor(10, 145);
    tft.print("<- - Back to menu");
    
    tft.setCursor(10, 280);
    tft.print("<- Back  OK Emulate");
}
void handleArduinoRFIDEmulate() {
    bool inEmulateMenu = true;
    String tagToEmulate = currentRFIDTag;
    
    // Инициализация Arduino RFID
    if (!initArduinoRFID()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("Arduino RFID Init Failed!");
        delay(2000);
        return;
    }
    
    if (tagToEmulate.length() != 16) {
        tft.fillScreen(BLACK);
        tft.setTextColor(YELLOW);
        tft.setTextSize(2);
        tft.setCursor(20, 100);
        tft.print("No tag to emulate");
        tft.setTextSize(1);
        tft.setCursor(20, 130);
        tft.print("Read a tag first or enter manually");
        delay(2000);
        // Здесь можно добавить ручной ввод, аналогичный функции записи
        return;
    }
    
    drawArduinoRFIDEmulateScreen(tagToEmulate, false);
    
    while (inEmulateMenu) {
        updateButtons();
        
        if (btnOK.fell()) {
            // Начало эмуляции
            drawArduinoRFIDEmulateScreen(tagToEmulate, true);
            emulateArduinoRFIDTag(tagToEmulate);
            drawArduinoRFIDEmulateScreen(tagToEmulate, false);
        }
        
        if (btnLeft.fell()) {
            inEmulateMenu = false;
        }
        
        delay(50);
    }
}
// ==================== VIEW SAVED RFID TAGS ====================
void viewSavedRFIDTags() {
    if (!SDCardManager::isCardPresent()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("SD Card required!");
        delay(2000);
        return;
    }
    
    if (!SD_MMC.exists(arduinoRFIDFolder)) {
        tft.fillScreen(BLACK);
        tft.setTextColor(YELLOW);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("No RFID tags saved");
        delay(2000);
        return;
    }
    
    File dir = SD_MMC.open(arduinoRFIDFolder);
    if (!dir) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("Error opening folder");
        delay(2000);
        return;
    }
    
    if (!dir.isDirectory()) {
        tft.fillScreen(BLACK);
        tft.setTextColor(RED);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("Not a directory");
        dir.close();
        delay(2000);
        return;
    }
    
    // Собираем список файлов
    String fileList[50];
    int fileCount = 0;
    
    File file = dir.openNextFile();
    while (file && fileCount < 50) {
        if (!file.isDirectory()) {
            String fileName = file.name();
            if (fileName.endsWith(".txt")) {
                fileList[fileCount] = fileName;
                fileCount++;
            }
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    if (fileCount == 0) {
        tft.fillScreen(BLACK);
        tft.setTextColor(YELLOW);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.print("No RFID tags found");
        delay(2000);
        return;
    }
    
    int selectedIndex = 0;
    bool viewing = true;
    
    while (viewing) {
        tft.fillScreen(BLACK);
        
        // Заголовок
        tft.setTextColor(CYAN);
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.print("Saved RFID Tags");
        tft.setTextSize(1);
        tft.setCursor(200, 15);
        tft.printf("%d/%d", selectedIndex + 1, fileCount);
        
        // Отображение списка меток
        int startIndex = (selectedIndex / 7) * 7;
        int endIndex = min(startIndex + 7, fileCount);
        
        for (int i = startIndex; i < endIndex; i++) {
            int displayPos = i - startIndex;
            int yPos = 40 + displayPos * 30;
            
            // Извлекаем код метки из имени файла
            String fileName = fileList[i];
            String tagCode = fileName;
            tagCode.replace("tag_", "");
            tagCode.replace(".txt", "");
            tagCode.replace("/rfid_arduino/", "");
            
            if (i == selectedIndex) {
                tft.fillRoundRect(5, yPos, 230, 25, 3, BLUE);
                tft.setTextColor(BLACK);
            } else {
                tft.drawRoundRect(5, yPos, 230, 25, 3, WHITE);
                tft.setTextColor(WHITE);
            }
            
            tft.setCursor(10, yPos + 5);
            if (tagCode.length() > 25) {
                tft.print(tagCode.substring(0, 22) + "...");
            } else {
                tft.print(tagCode);
            }
        }
        
        // Подсказки
        tft.setTextColor(LIGHT_GREY);
        tft.setCursor(10, 280);
        tft.print("<- Back  ^v Navigate  OK View");
        
        updateButtons();
        
        if (btnUp.fell() && selectedIndex > 0) {
            selectedIndex--;
        }
        
        if (btnDown.fell() && selectedIndex < fileCount - 1) {
            selectedIndex++;
        }
        
        if (btnOK.fell()) {
            // Просмотр содержимого файла
            String fileName = arduinoRFIDFolder + "/" + fileList[selectedIndex];
            File tagFile = SD_MMC.open(fileName);
            if (tagFile) {
                tft.fillScreen(BLACK);
                tft.setTextColor(WHITE);
                tft.setTextSize(1);
                
                tft.setCursor(10, 10);
                tft.print("Tag: " + fileList[selectedIndex]);
                tft.drawLine(0, 25, 240, 25, WHITE);
                
                int yPos = 35;
                while (tagFile.available() && yPos < 280) {
                    String line = tagFile.readStringUntil('\n');
                    line.trim();
                    
                    if (line.length() > 35) {
                        line = line.substring(0, 32) + "...";
                    }
                    
                    tft.setCursor(5, yPos);
                    tft.print(line);
                    yPos += 15;
                }
                tagFile.close();
                
                tft.setTextColor(LIGHT_GREY);
                tft.setCursor(10, 280);
                tft.print("<- Back to list");
                
                // Ждем нажатия кнопки назад
                bool inFileView = true;
                while (inFileView) {
                    updateButtons();
                    if (btnLeft.fell()) {
                        inFileView = false;
                    }
                    delay(50);
                }
            }
        }
        
        if (btnLeft.fell()) {
            viewing = false;
        }
        
        delay(50);
    }
}

// ======================= CC1101 SUB-GHZ =======================
//#include <RadioLib.h>


//==========================Wi-FI============================
#include "wifi/wifi_func.h"

WiFiAttackManager wifiAttack(tft, 480, 320, btnUp, btnDown, btnLeft, btnRight, btnOK, "/logs/attack_log.txt", incognitoMode);


//=====================MAIN===================
void handleSubmenuAction() {
  uint8_t parentMenu = currentMenu - 1;
  switch(parentMenu) {

    case 0: // Sub-GHz
    break;

    case 1: // 2.4 GHz
      if(menuPosition == 0) { // Jamming all diapason
          JAM = true;
          tft.fillScreen(BLACK);
          tft.setTextColor(RED);
          tft.setTextSize(2);
          tft.setCursor(20, 90);
          tft.print("2.4 GHz jamming");
          tft.setCursor(20, 110);
          tft.print("is ACTIVE");
          tft.setTextColor(WHITE);
          tft.setCursor(20, 140);
          tft.print("<- to stop attack");
          startJamming();
          break;
      }
      else if(menuPosition == 1) { // BT Jamming
          BTJamming();
          break;
      }
      else if(menuPosition == 2) { // Selective Jamming
          SelectiveJam();
          break;
      }
      else if(menuPosition == 3) { // Spectrum Analyzer
          handleLookingGlass();
          drawSubmenu();
          break;
      }
      break;
    
    case 2: // RFID
    switch(menuPosition) {
        case 0: // Read Tag
            handleArduinoRFIDRead(); // Используем Arduino вместо RDM6300
            drawSubmenu();
            break;
        case 1: // Write Tag 
            // Интерфейс для записи RFID через Arduino
            handleArduinoRFIDWrite();
            drawSubmenu();
            break;
        case 2: // Emulate Tag
            // Интерфейс для эмуляции RFID через Arduino
            handleArduinoRFIDEmulate();
            drawSubmenu();
            break;
        case 3: // Dictionary (просмотр сохраненных меток)
            viewSavedRFIDTags();
            drawSubmenu();
            break;
    }
    break;




    case 3: //NFC
    switch(menuPosition) {
        case 0: // Read Tag
            drawSubmenu();
            break;
        case 1: // Write Tag
            drawSubmenu();
            break;
        case 2: // Emulate Tag
            drawSubmenu();
            break;
        case 3: // Dictionary
            drawSubmenu();
            break;
    }
    break;

    case 4: // WiFi
    wifiAttack.runWiFiMenu();
    break;
      
    case 5: // BLE
      if (menuPosition == 3) { // ON/OFF
        static bool bleEnabled = false;
        bleEnabled = !bleEnabled;
        
        tft.fillScreen(BLACK);
        tft.setTextColor(WHITE);
        tft.setTextSize(2);
        tft.setCursor(50, 100);
        tft.print("BLE ");
        tft.print(bleEnabled ? "ON" : "OFF");
        
        if (bleEnabled) {
          // Настраиваем BLE advertising
          BLEDevice::init(BLE_DEVICE_NAME);
          BLEServer *pServer = BLEDevice::createServer();
          BLEAdvertising *pAdvertising = pServer->getAdvertising();
          pAdvertising->start();
          
          tft.setCursor(20, 140);
          tft.print("Device: ");
          tft.print(BLE_DEVICE_NAME);
        } else {
          BLEDevice::deinit();
        }
        delay(2000);
        drawSubmenu();
        break;
      }
      break;
    
    case 6: // IR
  break;

    case 7: // SD-USB
      switch(menuPosition) {
    case 0: // BadUSB
        showBadUSB();
        menuPosition = 0;
        drawSubmenu();
        break;
    case 1:
        sdManager.handleFileManager();
        menuPosition = 1;
        drawSubmenu();
        break;  
    case 2: // Mass Storage
        handleMassStorageMenu();
        menuPosition = 2;
        drawSubmenu();
        break;
    case 3: // USB Settings
        // Заглушка для настроек USB
        tft.fillScreen(BLACK);
        tft.setTextColor(YELLOW);
        tft.setTextSize(2);
        tft.setCursor(20, 100);
        tft.print("USB Settings");
        tft.setTextSize(1);
        tft.setCursor(20, 130);
        tft.print("Not implemented yet");
        delay(2000);
        drawSubmenu();
        break;
}
      break;
      
    case 8: // Settings menu
      switch(menuPosition) {
        case 6: // View Logs
          viewAttackLogs();
          drawSubmenu();
          break;
          
        case 7: // Incognito Mode
          incognitoMode = !incognitoMode;
          
          // Сохраняем настройку на SD карту
          if (SDCardManager::isCardPresent()) {
            File configFile = SD_MMC.open("/config.txt", FILE_WRITE);
            if (configFile) {
              configFile.println("SSID=" + String(wifi_ssid));
              configFile.println("PASSWORD=" + String(wifi_password));
              configFile.println("INCOGNITO=" + String(incognitoMode ? "1" : "0"));
              configFile.close();
            }
          }
          
          tft.fillScreen(BLACK);
          tft.setTextColor(incognitoMode ? GREEN : RED);
          tft.setTextSize(2);
          tft.setCursor(50, 100);
          tft.print("Incognito Mode");
          tft.setCursor(80, 140);
          tft.print(incognitoMode ? "ENABLED" : "DISABLED");
          delay(2000);
          drawSubmenu();
          break;
          
        default:
          // Обработка других пунктов настроек
          tft.fillScreen(BLACK);
          tft.setTextColor(WHITE);
          tft.setTextSize(2);
          tft.setCursor(50, 100);
          tft.print("Setting ");
          tft.print(menuPosition + 1);
          delay(1000);
          drawSubmenu();
          break;
      }
      break;
  }
}
void handleInput() {
  if (btnUp.fell()) {
    if (currentMenu == 0) {
      // Главное меню
      if (menuPosition > 0) {
        menuPosition--;
      } else if (currentPage > 1) {
        currentPage--;
        menuPosition = itemsPerPage - 1;
      }
      drawMenu();
    } else {
      // Подменю
      uint8_t parentMenu = currentMenu - 1;
      uint8_t submenuCount = mainMenu[parentMenu].submenuCount;
      
      if (menuPosition > 0) {
        menuPosition--;
        // Проверяем, нужно ли перейти на предыдущую страницу
        if (menuPosition < (currentSubmenuPage - 1) * itemsPerSubpage) {
          currentSubmenuPage--;
        }
        drawSubmenu();
      }
    }
  }
  
  if (btnDown.fell()) {
    if (currentMenu == 0) {
      // Главное меню
      uint8_t totalItems = sizeof(mainMenu)/sizeof(mainMenu[0]);
      uint8_t currentItem = (currentPage - 1) * itemsPerPage + menuPosition;
      
      if (currentItem < totalItems - 1) {
        if (menuPosition < itemsPerPage - 1) {
          menuPosition++;
        } else if (currentPage < totalPages) {
          currentPage++;
          menuPosition = 0;
        }
      }
      drawMenu();
    } else {
      // Подменю
      uint8_t parentMenu = currentMenu - 1;
      uint8_t submenuCount = mainMenu[parentMenu].submenuCount;
      
      if (menuPosition < submenuCount - 1) {
        menuPosition++;
        // Проверяем, нужно ли перейти на следующую страницу
        if (menuPosition >= currentSubmenuPage * itemsPerSubpage) {
          currentSubmenuPage++;
        }
        drawSubmenu();
      }
    }
  }
  
  if (btnLeft.fell()) {
    if (currentMenu != 0) {
      // Возвращаемся в главное меню
      currentMenu = 0;
      currentPage = lastMainMenuPage;
      menuPosition = lastMainMenuPosition;
      currentSubmenuPage = 1; // Сбрасываем страницу подменю
      drawMenu();
    }
  }
  
if (btnOK.fell()) {
    if (currentMenu == 0) {
        lastMainMenuPosition = menuPosition;
        lastMainMenuPage = currentPage;
        
        uint8_t selectedItem = (currentPage - 1) * itemsPerPage + menuPosition;
        if (selectedItem < sizeof(mainMenu)/sizeof(mainMenu[0])) {
            // Если выбрали WiFi (индекс 4), не открываем обычное подменю,
            // а сразу передаём управление handleSubmenuAction
            if (selectedItem == 4) {
                currentMenu = 5;                // формально переключаем в подменю WiFi
                menuPosition = 0;               // не важно, какой пункт
                handleSubmenuAction();          // запускает wifiAttack.runWiFiMenu()
                currentMenu = 0;                // возвращаемся в главное меню
                drawMenu();                     // перерисовываем главный экран
            } else {
                currentMenu = selectedItem + 1;
                menuPosition = 0;
                currentSubmenuPage = 1;
                drawSubmenu();
            }
        }
    } else {
        handleSubmenuAction();   // обычная обработка для других подменю
    }
}
  
  if (btnPower.fell()) {
    tft.fillScreen(BLACK);
    tft.setTextColor(RED);
    tft.setTextSize(2);
    tft.setCursor(50, 140);
    tft.println("POWER OFF");
    delay(1000);
  }
}
void setup() {
  Serial.begin(115200);
  // Инициализация дисплея
  tft.init();
  tft.setRotation(3);
  tft.setBrightness(255);
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  sdManager.init();

  // Создание и чтение конфигурации WiFi
    readWiFiConfig(); // Эта функция сама создаст конфиг если его нет

    // Подключение к WiFi
    if (wifi_ssid && wifi_password) {
        WiFi.begin(wifi_ssid, wifi_password);
        Serial.println("Connecting to WiFi: " + String(wifi_ssid));
        
        tft.fillScreen(BLACK);
        tft.setTextColor(WHITE);
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.print("Connecting to:");
        tft.setCursor(10, 40);
        tft.print(wifi_ssid);
        
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(500);
            Serial.print(".");
            tft.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi connected");
            tft.setTextColor(GREEN);
            tft.setCursor(10, 80);
            tft.print("Connected!");
            
            // Настройка времени после подключения
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            syncTime();
            delay(1000);
        } else {
            Serial.println("WiFi connection failed");
            tft.setTextColor(RED);
            tft.setCursor(10, 80);
            tft.print("Failed!");
            delay(1000);
        }
    }

  // Настройка времени
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  btnUp.attach(BTN_UP, INPUT_PULLUP);
  btnDown.attach(BTN_DOWN, INPUT_PULLUP);
  btnLeft.attach(BTN_LEFT, INPUT_PULLUP);
  btnRight.attach(BTN_RIGHT, INPUT_PULLUP);
  btnOK.attach(BTN_OK, INPUT_PULLUP);
  btnPower.attach(BTN_POWER, INPUT_PULLUP);
  
  btnUp.interval(25);
  btnDown.interval(25);
  btnLeft.interval(25);
  btnRight.interval(25);
  btnOK.interval(25);
  btnPower.interval(25);




// Создание папки для RFID меток если SD карта присутствует
if (SDCardManager::isCardPresent() && !SD_MMC.exists(arduinoRFIDFolder)) {
    SD_MMC.mkdir(arduinoRFIDFolder);
    Serial.println("Created RFID tags directory");
  }

  // Инициализация папки для Sub-GHz сигналов
if (SDCardManager::isCardPresent() && !SD_MMC.exists("/subghz_signals")) {
    SD_MMC.mkdir("/subghz_signals");
    Serial.println("Created Sub-GHz signals directory");
}

  

  // Инициализация массива позиций подменю
  lastMainMenuPosition = 0;
  lastMainMenuPage = 1;


  initLookingGlass();


  Serial.println("WE ARE OKAY");

  // Начальный экран
  drawMenu();



}
void loop() {
  updateButtons();
  
  if (!wifiAttack.isAttackRunning()) {
        handleInput();
    }
  
  
  // Периодическая синхронизация времени
    if (WiFi.status() == WL_CONNECTED && 
        (millis() - lastTimeSync > TIME_SYNC_INTERVAL || !timeSynced)) {
          const char* ssid = "ASUSko";
          const char* password =  "1720410316";
          WiFi.begin(ssid, password);
          delay(1000);
        syncTime();
    }
    
    // Обновляем время на экране каждую секунду
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 1000) {
        dravstatusbar();
        lastTimeUpdate = millis();
    }
  delay(10);
}