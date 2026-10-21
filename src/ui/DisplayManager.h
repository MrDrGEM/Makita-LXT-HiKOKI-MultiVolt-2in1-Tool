#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <Arduino_DataBus.h>
#include <Arduino_GFX.h>
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_NV3041A.h>
#include <Wire.h>
#include "config.h"
#include "../makita/MakitaReader.h"
#include "../hikoki/HikokiReader.h"

enum ActiveTab {
    TAB_MAKITA = 0,
    TAB_HIKOKI = 1,
    TAB_AUTO   = 2
};

enum HikokiSubTab {
    HIKOKI_SUB_INFO    = 0,
    HIKOKI_SUB_CELLS   = 1,
    HIKOKI_SUB_ERRORS  = 2,
    HIKOKI_SUB_TOOLS   = 3,
    HIKOKI_SUB_FLAGS   = 4
};

enum UIAction {
    ACTION_NONE = 0,
    ACTION_SCAN_MAKITA,
    ACTION_UNLOCK_MAKITA,
    ACTION_RESET_MAKITA_DA04,
    ACTION_OMEGA_LOCK_MAKITA,
    ACTION_SCAN_HIKOKI,
    ACTION_RESET_HIKOKI_LOCK,
    ACTION_DEEP_SLEEP,
    ACTION_SWITCH_TAB_MAKITA,
    ACTION_SWITCH_TAB_HIKOKI,
    ACTION_SWITCH_TAB_AUTO
};

class DisplayManager {
public:
    DisplayManager();

    bool begin();
    void setBacklight(uint8_t brightness); // 0-255

    void renderHeader(ActiveTab activeTab);
    void renderMakitaScreen(const MakitaData &data, const char *statusMsg = nullptr);
    void renderHikokiScreen(const HikokiData &data, HikokiSubTab subTab, const char *statusMsg = nullptr);
    void renderAutoDetectScreen(const char *statusMsg = nullptr);
    void renderConfirmDialog(const char *title, const char *message);

    UIAction handleTouch(int touchX, int touchY, ActiveTab &currentTab, HikokiSubTab &currentSubTab, bool inConfirmDialog);
    bool getTouchPoint(int &x, int &y);

private:
    Arduino_DataBus *_bus;
    Arduino_GFX *_gfx;

    // Drawing helpers
    void drawButton(int x, int y, int w, int h, const char *label, uint16_t bgColor, uint16_t textColor = WHITE);
    void drawCard(int x, int y, int w, int h, uint16_t bgColor, const char *title = nullptr);
    void drawProgressBar(int x, int y, int w, int h, float percent, uint16_t fillColor, uint16_t bgColor);

    // Color Palette (RGB565)
    static constexpr uint16_t COLOR_BG        = 0x10A2; // Dark Navy #10141A
    static constexpr uint16_t COLOR_CARD      = 0x18E5; // Card #181C25
    static constexpr uint16_t COLOR_CARD_ALT  = 0x2128; // Card Darker
    static constexpr uint16_t COLOR_HEADER    = 0x0861; // Header #080C12
    static constexpr uint16_t COLOR_MAKITA    = 0x0518; // Makita Teal #00A499
    static constexpr uint16_t COLOR_HIKOKI    = 0x2E63; // HiKOKI Green #59CC24
    static constexpr uint16_t COLOR_OK        = 0x07E0; // Green #00E676
    static constexpr uint16_t COLOR_WARN      = 0xFDE0; // Orange #FFA000
    static constexpr uint16_t COLOR_DANGER    = 0xF800; // Red #E53935
    static constexpr uint16_t COLOR_TEXT_MUTED= 0x8410; // Gray
};

#endif // DISPLAY_MANAGER_H
