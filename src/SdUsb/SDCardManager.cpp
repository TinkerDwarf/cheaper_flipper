#include "SDCardManager.h"

// Пины для SD_MMC (можно оставить глобальными или передавать)
#define SDMMC_CLK 46
#define SDMMC_CMD 9
#define SDMMC_D0  4
#define SDMMC_D1  5
#define SDMMC_D2  6
#define SDMMC_D3  7

// ---- Конструктор ----------------------------------------------------------
SDCardManager::SDCardManager(LGFX& tft, uint16_t width, uint16_t height)
    : _tft(&tft)
    , _screenWidth(width)
    , _screenHeight(height)
    , _scaleX(width / 240.0f)
    , _scaleY(height / 320.0f)
    , _sdCardPresent(false)
{}

// ---- Инициализация SD -----------------------------------------------------
bool SDCardManager::init() {
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0, SDMMC_D1, SDMMC_D2, SDMMC_D3);
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 4)) {
        Serial.println(F("SD CARD FAILED, OR NOT PRESENT!"));
        _sdCardPresent = false;
        return false;
    }

    Serial.println(F("SD CARD INITIALIZED."));
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println(F("No SD card attached"));
        _sdCardPresent = false;
        return false;
    }

    _sdCardPresent = true;
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    return true;
}

// ---- Файловый читатель ----------------------------------------------------
void SDCardManager::initFileReader(File file) {
    _reader.file = file;
    _reader.fileName = file.name();
    _reader.fileSize = file.size();
    _reader.currentPage = 1;
    _reader.linesPerPage = 15;
    _reader.isReading = true;
    _reader.needsRedraw = true;
    _reader.lastRedrawTime = 0;

    int lineCount = 0;
    file.seek(0);
    while (file.available()) {
        file.readStringUntil('\n');
        lineCount++;
    }
    _reader.totalPages = (lineCount + _reader.linesPerPage - 1) / _reader.linesPerPage;
    if (_reader.totalPages == 0) _reader.totalPages = 1;
    file.seek(0);
}

void SDCardManager::drawFileInfoScreen(bool forceRedraw) {
    if (!forceRedraw && millis() - _reader.lastRedrawTime < 300) return;

    _tft->fillScreen(BLACK);
    _tft->setTextColor(WHITE);
    _tft->setTextSize(2);
    _tft->setCursor(scX(10), scY(10));
    _tft->print("File Info");

    _tft->setTextSize(1);
    _tft->setCursor(scX(10), scY(40));
    _tft->print("Name: ");
    String fileName = _reader.fileName;
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash != -1) fileName = fileName.substring(lastSlash + 1);
    if (fileName.length() > 25) fileName = fileName.substring(0, 22) + "...";
    _tft->print(fileName);

    _tft->setCursor(scX(10), scY(60));
    _tft->print("Size: ");
    if (_reader.fileSize < 1024) {
        _tft->print(_reader.fileSize);
        _tft->print(" bytes");
    } else if (_reader.fileSize < 1024 * 1024) {
        _tft->print(_reader.fileSize / 1024.0, 1);
        _tft->print(" KB");
    } else {
        _tft->print(_reader.fileSize / (1024.0 * 1024.0), 1);
        _tft->print(" MB");
    }

    _tft->setCursor(scX(10), scY(80));
    _tft->print("Pages: ");
    _tft->print(_reader.totalPages);

    _tft->setCursor(scX(10), scY(100));
    _tft->print("Type: ");
    String fileExt = _reader.fileName.substring(_reader.fileName.lastIndexOf('.') + 1);
    fileExt.toUpperCase();
    _tft->print(fileExt);
    _tft->print(" file");

    _tft->setTextColor(LIGHT_GREY);
    _tft->setCursor(scX(10), scY(130));
    _tft->print("OK - Read file");
    _tft->setCursor(scX(10), scY(150));
    _tft->print("<- - Back to browser");

    _tft->setCursor(scX(10), scY(280));
    _tft->print("<- Back     OK Read");

    _reader.lastRedrawTime = millis();
    _reader.needsRedraw = false;
}

void SDCardManager::drawFileContentScreen(bool forceRedraw) {
    if (!forceRedraw && millis() - _reader.lastRedrawTime < 300) return;

    _tft->fillScreen(BLACK);
    _tft->setTextColor(WHITE);
    _tft->setTextSize(1);
    _tft->setCursor(scX(5), scY(5));

    String displayName = _reader.fileName;
    int lastSlash = displayName.lastIndexOf('/');
    if (lastSlash != -1) displayName = displayName.substring(lastSlash + 1);
    if (displayName.length() > 25) displayName = displayName.substring(0, 22) + "...";
    _tft->print(displayName);

    _tft->setCursor(scX(180), scY(5));
    _tft->printf("Page %d/%d", _reader.currentPage, _reader.totalPages);

    _tft->drawLine(0, scY(15), scX(240), scY(15), WHITE);

    _reader.file.seek(0);
    int targetLine = (_reader.currentPage - 1) * _reader.linesPerPage;
    int currentLine = 0;
    int yPos = scY(25);
    const int maxY = scY(260);

    while (_reader.file.available() && yPos < maxY) {
        String line = _reader.file.readStringUntil('\n');
        line.trim();
        if (currentLine >= targetLine && currentLine < targetLine + _reader.linesPerPage) {
            if (line.length() > 35) line = line.substring(0, 32) + "...";
            _tft->setCursor(scX(5), yPos);
            _tft->print(line);
            yPos += scY(15);
        }
        currentLine++;
        if (currentLine >= targetLine + _reader.linesPerPage) break;
    }

    _tft->setTextColor(LIGHT_GREY);
    _tft->setCursor(scX(10), scY(280));
    _tft->print("^v Scroll");
    _tft->setCursor(scX(100), scY(280));
    _tft->print("<- Back");
    _tft->setCursor(scX(180), scY(280));
    _tft->print("OK Info");

    _reader.lastRedrawTime = millis();
    _reader.needsRedraw = false;
}

void SDCardManager::handleFileReading() {
    bool inFileViewer = true;
    bool showInfoScreen = true;
    unsigned long lastButtonPress = 0;
    const unsigned long debounceDelay = 300;

    drawFileInfoScreen(true);

    while (inFileViewer) {
        unsigned long now = millis();
        if (now - lastButtonPress < debounceDelay) { delay(50); continue; }

        updateButtons();

        if (btnOK.fell()) {
            lastButtonPress = now;
            showInfoScreen = !showInfoScreen;
            if (showInfoScreen) drawFileInfoScreen(true);
            else drawFileContentScreen(true);
        }

        if (btnUp.fell()) {
            lastButtonPress = now;
            if (!showInfoScreen && _reader.currentPage > 1) {
                _reader.currentPage--;
                _reader.needsRedraw = true;
                drawFileContentScreen(true);
            }
        }

        if (btnDown.fell()) {
            lastButtonPress = now;
            if (!showInfoScreen && _reader.currentPage < _reader.totalPages) {
                _reader.currentPage++;
                _reader.needsRedraw = true;
                drawFileContentScreen(true);
            }
        }

        if (btnLeft.fell()) {
            lastButtonPress = now;
            if (showInfoScreen) inFileViewer = false;
            else { showInfoScreen = true; drawFileInfoScreen(true); }
        }

        if (_reader.needsRedraw) {
            if (showInfoScreen) drawFileInfoScreen();
            else drawFileContentScreen();
        }
        delay(50);
    }

    if (_reader.file) _reader.file.close();
}

// ---- Файловый браузер -----------------------------------------------------
void SDCardManager::updateFileList() {
    File dir = SD_MMC.open(_browser.currentPath);
    if (!dir || !dir.isDirectory()) {
        _browser.fileCount = 0;
        _browser.totalPages = 1;
        _browser.isDirectoryEmpty = true;
        if (dir) dir.close();
        return;
    }

    _browser.fileCount = 0;
    dir.rewindDirectory();
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        _browser.fileCount++;
        entry.close();
    }
    dir.close();

    _browser.isDirectoryEmpty = (_browser.fileCount == 0);
    _browser.totalPages = (_browser.fileCount + 6) / 7;
    if (_browser.totalPages == 0) _browser.totalPages = 1;

    if (_browser.selectedIndex >= _browser.fileCount)
        _browser.selectedIndex = max(0, _browser.fileCount - 1);
    if (_browser.selectedIndex < 0 && _browser.fileCount > 0)
        _browser.selectedIndex = 0;

    _browser.currentPage = (_browser.fileCount > 0) ? (_browser.selectedIndex / 7) + 1 : 1;
    _browser.needsRedraw = true;
}

void SDCardManager::initFileBrowser() {
    if (_browser.currentPath == "") _browser.currentPath = "/";

    if (_browser.currentPath != "/" && _browser.lastPath == _browser.currentPath) {
        _browser.selectedIndex = _browser.lastSelectedIndex;
    } else {
        _browser.selectedIndex = 0;
        _browser.lastPath = "";
        _browser.lastSelectedIndex = 0;
    }

    _browser.currentPage = 1;
    _browser.needsRedraw = true;
    _browser.lastRedrawTime = 0;
    _browser.isDirectoryEmpty = false;

    updateFileList();
    if (_browser.fileCount > 0)
        _browser.currentPage = (_browser.selectedIndex / 7) + 1;
}

File SDCardManager::getFileByIndex(int index) {
    File dir = SD_MMC.open(_browser.currentPath);
    if (!dir) return File();

    for (int i = 0; i <= index; i++) {
        File entry = dir.openNextFile();
        if (!entry) { dir.close(); return File(); }
        if (i == index) { dir.close(); return entry; }
        entry.close();
    }
    dir.close();
    return File();
}

void SDCardManager::showFileInfo(File file) {
    initFileReader(file);
    handleFileReading();
    _browser.needsRedraw = true;
}

void SDCardManager::drawFileManagerPartial(bool forceRedraw) {
    if (!_browser.needsRedraw && !forceRedraw) return;
    unsigned long now = millis();
    if (now - _browser.lastRedrawTime < 200 && !forceRedraw) return;

    _tft->fillScreen(BLACK);

    // Заголовок
    _tft->setTextColor(WHITE);
    _tft->setTextSize(2);
    _tft->setCursor(scX(10), scY(10));
    _tft->print("File Manager");

    if (!_sdCardPresent) {
        _tft->setTextColor(RED);
        _tft->setCursor(scX(10), scY(50));
        _tft->print("SD Card not found!");
        _tft->setTextSize(1);
        _tft->setCursor(scX(10), scY(280));
        _tft->print("<- Back");
        _browser.lastRedrawTime = now;
        _browser.needsRedraw = false;
        return;
    }

    // Путь
    _tft->setTextSize(1);
    _tft->setCursor(scX(10), scY(35));
    _tft->print("Path: ");
    String displayPath = _browser.currentPath;
    if (displayPath.length() > 25) displayPath = "..." + displayPath.substring(displayPath.length() - 22);
    _tft->print(displayPath);

    _tft->drawLine(0, scY(45), scX(240), scY(45), WHITE);

    if (_browser.isDirectoryEmpty) {
        _tft->setTextColor(LIGHT_GREY);
        _tft->setCursor(scX(10), scY(80));
        _tft->print("This folder is empty");
        _tft->setCursor(scX(10), scY(100));
        _tft->print("Press <- to go back");
        _tft->setCursor(scX(10), scY(280));
        _tft->print("<- Back");
        _browser.lastRedrawTime = now;
        _browser.needsRedraw = false;
        return;
    }

    // Список файлов
    int startIndex = (_browser.currentPage - 1) * 7;
    int endIndex = min(startIndex + 7, _browser.fileCount);
    int yPos = scY(55);

    File dir = SD_MMC.open(_browser.currentPath);
    if (!dir || !dir.isDirectory()) {
        _tft->setCursor(scX(10), scY(60));
        _tft->print("Error opening dir!");
        if (dir) dir.close();
        _browser.lastRedrawTime = now;
        _browser.needsRedraw = false;
        return;
    }

    for (int i = 0; i < startIndex; i++) {
        File entry = dir.openNextFile();
        if (entry) entry.close();
    }

    for (int i = startIndex; i < endIndex; i++) {
        File entry = dir.openNextFile();
        if (!entry) break;

        // Выделение
        if (i == _browser.selectedIndex) {
            _tft->fillRect(0, yPos - scY(2), scX(240), scY(20) + scY(4), BLUE);
            _tft->setTextColor(BLACK);
        } else {
            _tft->setTextColor(WHITE);
        }

        _tft->setCursor(scX(5), yPos);
        if (entry.isDirectory()) {
            _tft->print("[D] ");
            _tft->setTextColor(CYAN);
        } else {
            _tft->print("[F] ");
            _tft->setTextColor(WHITE);
        }

        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) fileName = fileName.substring(lastSlash + 1);
        if (fileName.length() > 20) fileName = fileName.substring(0, 17) + "...";
        _tft->print(fileName);

        if (!entry.isDirectory()) {
            _tft->setCursor(scX(180), yPos);
            String sizeStr;
            if (entry.size() < 1024) sizeStr = String(entry.size()) + "B";
            else if (entry.size() < 1024 * 1024) sizeStr = String(entry.size() / 1024.0, 1) + "K";
            else sizeStr = String(entry.size() / (1024.0 * 1024.0), 1) + "M";
            _tft->print(sizeStr);
        }

        yPos += scY(20);
        entry.close();
    }
    dir.close();

    // Информация о позиции
    _tft->setTextSize(1);
    _tft->setTextColor(LIGHT_GREY);
    _tft->setCursor(scX(10), scY(200));
    _tft->printf("Item: %d/%d", _browser.selectedIndex + 1, _browser.fileCount);
    _tft->setCursor(scX(120), scY(200));
    _tft->printf("Page: %d/%d", _browser.currentPage, _browser.totalPages);

    _tft->setCursor(scX(10), scY(280));
    _tft->print("OK-Open");
    _tft->setCursor(scX(80), scY(280));
    _tft->print("^v-Nav");
    _tft->setCursor(scX(140), scY(280));
    _tft->print("<-Back");

    _browser.needsRedraw = false;
    _browser.lastRedrawTime = now;
}

void SDCardManager::handleFileManager() {
    if (!_sdCardPresent) { delay(1000); return; }

    initFileBrowser();
    bool inFileManager = true;
    unsigned long lastButtonPress = 0;
    const unsigned long debounceDelay = 250;

    while (inFileManager) {
        unsigned long now = millis();
        drawFileManagerPartial();
        updateButtons();

        if (now - lastButtonPress < debounceDelay) { delay(50); continue; }

        if (btnUp.fell()) {
            lastButtonPress = now;
            if (!_browser.isDirectoryEmpty && _browser.selectedIndex > 0) {
                _browser.selectedIndex--;
                _browser.currentPage = (_browser.selectedIndex / 7) + 1;
                _browser.needsRedraw = true;
            }
        }

        if (btnDown.fell()) {
            lastButtonPress = now;
            if (!_browser.isDirectoryEmpty && _browser.selectedIndex < _browser.fileCount - 1) {
                _browser.selectedIndex++;
                _browser.currentPage = (_browser.selectedIndex / 7) + 1;
                _browser.needsRedraw = true;
            }
        }

        if (btnOK.fell()) {
            lastButtonPress = now;
            if (!_browser.isDirectoryEmpty && _browser.fileCount > 0) {
                File entry = getFileByIndex(_browser.selectedIndex);
                if (entry) {
                    if (entry.isDirectory()) {
                        _browser.lastPath = _browser.currentPath;
                        _browser.lastSelectedIndex = _browser.selectedIndex;
                        String newPath = _browser.currentPath;
                        if (!newPath.endsWith("/")) newPath += "/";
                        String folderName = entry.name();
                        int lastSlash = folderName.lastIndexOf('/');
                        if (lastSlash != -1) folderName = folderName.substring(lastSlash + 1);
                        newPath += folderName;
                        _browser.currentPath = newPath;
                        _browser.selectedIndex = 0;
                        _browser.currentPage = 1;
                        updateFileList();
                    } else {
                        showFileInfo(entry);
                        _browser.needsRedraw = true;
                    }
                    entry.close();
                }
            }
        }

        if (btnLeft.fell()) {
            lastButtonPress = now;
            if (_browser.currentPath != "/") {
                int lastSlash = _browser.currentPath.lastIndexOf('/');
                if (lastSlash == 0) _browser.currentPath = "/";
                else _browser.currentPath = _browser.currentPath.substring(0, lastSlash);

                if (_browser.lastPath == _browser.currentPath)
                    _browser.selectedIndex = _browser.lastSelectedIndex;
                else
                    _browser.selectedIndex = 0;

                _browser.currentPage = 1;
                updateFileList();
                if (_browser.fileCount > 0) _browser.currentPage = (_browser.selectedIndex / 7) + 1;
            } else {
                inFileManager = false;
            }
        }

        if (btnRight.fell()) {
            lastButtonPress = now;
            if (!_browser.isDirectoryEmpty && _browser.currentPage < _browser.totalPages) {
                _browser.currentPage++;
                _browser.selectedIndex = (_browser.currentPage - 1) * 7;
                _browser.needsRedraw = true;
            }
        }
        delay(30);
    }
}