#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include <Arduino.h>
#include <SD_MMC.h>
#include <display.h>       // ваш класс дисплея
#include "buttons.h"    // глобальные кнопки и updateButtons()

class SDCardManager {
public:
    /**
     * @param tft       ссылка на объект дисплея
     * @param width     ширина экрана в пикселях
     * @param height    высота экрана в пикселях
     */
    SDCardManager(LGFX& tft, uint16_t width, uint16_t height);

    /**
     * Инициализация SD-карты с заданными пинами.
     * @return true, если карта успешно обнаружена и смонтирована.
     */
    bool init();

    /// Проверка наличия карты
    bool isCardPresent() const { return _sdCardPresent; }

    /// Запуск файлового менеджера (блокирующий цикл)
    void handleFileManager();

private:
    LGFX*              _tft;
    uint16_t           _screenWidth;
    uint16_t           _screenHeight;
    float              _scaleX;       // scaleX = screenWidth / 240.0
    float              _scaleY;       // scaleY = screenHeight / 320.0
    bool               _sdCardPresent;

    // Вспомогательные методы масштабирования
    int16_t scX(int16_t x) const { return (int16_t)(x * _scaleX); }
    int16_t scY(int16_t y) const { return (int16_t)(y * _scaleY); }

    // Внутренние структуры состояния
    struct FileBrowserState {
        String currentPath = "/";
        int selectedIndex = 0;
        int fileCount = 0;
        int currentPage = 1;
        int totalPages = 1;
        bool needsRedraw = true;
        unsigned long lastRedrawTime = 0;
        bool isDirectoryEmpty = false;
        String lastPath;
        int lastSelectedIndex = 0;
    };

    struct FileReaderState {
        File file;
        String fileName;
        long fileSize = 0;
        int currentPage = 1;
        int totalPages = 1;
        int linesPerPage = 15;
        bool isReading = false;
        unsigned long lastRedrawTime = 0;
        bool needsRedraw = true;
    };

    FileBrowserState _browser;
    FileReaderState  _reader;

    // Приватные методы работы с файловой системой и UI
    void updateFileList();
    void initFileBrowser();
    File getFileByIndex(int index);
    void showFileInfo(File file);
    void initFileReader(File file);
    void handleFileReading();
    void drawFileManagerPartial(bool forceRedraw = false);
    void drawFileInfoScreen(bool forceRedraw = false);
    void drawFileContentScreen(bool forceRedraw = false);
};

#endif