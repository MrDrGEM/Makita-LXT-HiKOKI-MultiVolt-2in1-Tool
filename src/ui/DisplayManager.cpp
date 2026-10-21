#include "DisplayManager.h"

DisplayManager::DisplayManager() : _bus(nullptr), _gfx(nullptr) {}

bool DisplayManager::begin() {
    // 1. Backlight setup
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    pinMode(GFX_BL_ALT, OUTPUT);
    digitalWrite(GFX_BL_ALT, HIGH);

    // 2. Initialize GT911 Touch
    pinMode(TOUCH_GT911_RST, OUTPUT);
    pinMode(TOUCH_GT911_INT, OUTPUT);
    digitalWrite(TOUCH_GT911_RST, LOW);
    digitalWrite(TOUCH_GT911_INT, LOW);
    delay(10);
    digitalWrite(TOUCH_GT911_RST, HIGH);
    delay(10);
    pinMode(TOUCH_GT911_INT, INPUT);

    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL, 400000);

    // 3. Initialize NV3041A QSPI Display
    _bus = new Arduino_ESP32QSPI(
        GFX_QSPI_CS,
        GFX_QSPI_SCK,
        GFX_QSPI_D0,
        GFX_QSPI_D1,
        GFX_QSPI_D2,
        GFX_QSPI_D3
    );

    _gfx = new Arduino_NV3041A(
        _bus,
        GFX_NOT_DEFINED /* rst */,
        0 /* rotation: 0 = Landscape (480x272) */,
        true /* ips */
    );

    if (!_gfx->begin()) {
        Serial.println("Display begin failed!");
        return false;
    }

    _gfx->fillScreen(COLOR_BG);
    return true;
}

void DisplayManager::setBacklight(uint8_t brightness) {
    analogWrite(GFX_BL, brightness);
    analogWrite(GFX_BL_ALT, brightness);
}

bool DisplayManager::getTouchPoint(int &x, int &y) {
    Wire.beginTransmission(TOUCH_GT911_ADDR);
    Wire.write(0x81);
    Wire.write(0x4E);
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint8_t)TOUCH_GT911_ADDR, (uint8_t)7) != 7) return false;

    uint8_t status = Wire.read();
    uint8_t count = status & 0x0F;

    if ((status & 0x80) && count > 0) {
        Wire.read(); // track id
        uint8_t xL = Wire.read();
        uint8_t xH = Wire.read();
        uint8_t yL = Wire.read();
        uint8_t yH = Wire.read();
        Wire.read(); // size

        int rawX = xL | (xH << 8);
        int rawY = yL | (yH << 8);

        // Clamp to screen
        x = constrain(rawX, 0, SCREEN_WIDTH - 1);
        y = constrain(rawY, 0, SCREEN_HEIGHT - 1);

        // Clear buffer flag
        Wire.beginTransmission(TOUCH_GT911_ADDR);
        Wire.write(0x81);
        Wire.write(0x4E);
        Wire.write(0x00);
        Wire.endTransmission();
        return true;
    }

    if (status & 0x80) {
        Wire.beginTransmission(TOUCH_GT911_ADDR);
        Wire.write(0x81);
        Wire.write(0x4E);
        Wire.write(0x00);
        Wire.endTransmission();
    }
    return false;
}

void DisplayManager::drawButton(int x, int y, int w, int h, const char *label, uint16_t bgColor, uint16_t textColor) {
    _gfx->fillRoundRect(x, y, w, h, 4, bgColor);
    _gfx->drawRoundRect(x, y, w, h, 4, WHITE);
    _gfx->setTextColor(textColor);
    _gfx->setTextSize(1);

    int textX = x + (w - (strlen(label) * 6)) / 2;
    int textY = y + (h - 8) / 2;
    _gfx->setCursor(max(x + 2, textX), max(y + 2, textY));
    _gfx->print(label);
}

void DisplayManager::drawCard(int x, int y, int w, int h, uint16_t bgColor, const char *title) {
    _gfx->fillRoundRect(x, y, w, h, 6, bgColor);
    _gfx->drawRoundRect(x, y, w, h, 6, 0x39E7); // Subtle border
    if (title) {
        _gfx->setTextColor(0xBDD7);
        _gfx->setTextSize(1);
        _gfx->setCursor(x + 8, y + 6);
        _gfx->print(title);
        _gfx->drawFastHLine(x + 4, y + 18, w - 8, 0x39E7);
    }
}

void DisplayManager::drawProgressBar(int x, int y, int w, int h, float percent, uint16_t fillColor, uint16_t bgColor) {
    _gfx->fillRoundRect(x, y, w, h, 2, bgColor);
    int fillW = (int)((w - 2) * constrain(percent, 0.0f, 100.0f) / 100.0f);
    if (fillW > 0) {
        _gfx->fillRoundRect(x + 1, y + 1, fillW, h - 2, 2, fillColor);
    }
}

void DisplayManager::renderHeader(ActiveTab activeTab) {
    _gfx->fillRect(0, 0, SCREEN_WIDTH, 32, COLOR_HEADER);
    _gfx->drawFastHLine(0, 32, SCREEN_WIDTH, 0x4228);

    // Title / Logo
    _gfx->setTextColor(WHITE);
    _gfx->setTextSize(1);
    _gfx->setCursor(8, 12);
    _gfx->print("TOOL BATTERY DIAG");

    // Tab Buttons
    // Tab 0: Makita
    uint16_t makitaColor = (activeTab == TAB_MAKITA) ? COLOR_MAKITA : 0x2124;
    _gfx->fillRoundRect(150, 4, 100, 24, 4, makitaColor);
    if (activeTab == TAB_MAKITA) _gfx->drawRoundRect(150, 4, 100, 24, 4, WHITE);
    _gfx->setTextColor(WHITE);
    _gfx->setCursor(168, 12);
    _gfx->print("MAKITA LXT");

    // Tab 1: Hikoki
    uint16_t hikokiColor = (activeTab == TAB_HIKOKI) ? COLOR_HIKOKI : 0x2124;
    _gfx->fillRoundRect(260, 4, 100, 24, 4, hikokiColor);
    if (activeTab == TAB_HIKOKI) _gfx->drawRoundRect(260, 4, 100, 24, 4, WHITE);
    _gfx->setTextColor(WHITE);
    _gfx->setCursor(278, 12);
    _gfx->print("HIKOKI MV");

    // Tab 2: Auto
    uint16_t autoColor = (activeTab == TAB_AUTO) ? 0x03FF : 0x2124;
    _gfx->fillRoundRect(370, 4, 100, 24, 4, autoColor);
    if (activeTab == TAB_AUTO) _gfx->drawRoundRect(370, 4, 100, 24, 4, WHITE);
    _gfx->setTextColor(WHITE);
    _gfx->setCursor(386, 12);
    _gfx->print("AUTO DETECT");
}

void DisplayManager::renderMakitaScreen(const MakitaData &data, const char *statusMsg) {
    renderHeader(TAB_MAKITA);
    _gfx->fillRect(0, 33, SCREEN_WIDTH, SCREEN_HEIGHT - 33, COLOR_BG);

    if (!data.connected) {
        drawCard(20, 50, 440, 170, COLOR_CARD, "Makita LXT - Standalone Diagnostic");
        _gfx->setTextColor(0xBDD7);
        _gfx->setTextSize(2);
        _gfx->setCursor(60, 95);
        _gfx->print("NO MAKITA BATTERY DETECTED");

        _gfx->setTextSize(1);
        _gfx->setTextColor(COLOR_TEXT_MUTED);
        _gfx->setCursor(60, 130);
        _gfx->print("Insert an 18V/36V Makita pack into the connector.");
        _gfx->setCursor(60, 150);
        _gfx->print("Pins required: GND (-), 1-Wire Data, Bus Enable.");

        drawButton(180, 175, 120, 32, "PROBE / SCAN", COLOR_MAKITA);
        return;
    }

    // --- Left Card: Battery Info & Lock Status ---
    drawCard(8, 38, 230, 192, COLOR_CARD, "Battery Identity & Health");

    _gfx->setTextSize(1);
    _gfx->setTextColor(WHITE);
    _gfx->setCursor(16, 58);
    _gfx->printf("Model: %s", data.model[0] ? data.model : "BL18xx");

    _gfx->setCursor(16, 73);
    if (data.romIdValid) {
        _gfx->printf("ROM ID: %02X%02X%02X%02X%02X%02X%02X%02X",
                     data.romId[0], data.romId[1], data.romId[2], data.romId[3],
                     data.romId[4], data.romId[5], data.romId[6], data.romId[7]);
    } else {
        _gfx->print("ROM ID: (Standard Type)");
    }

    _gfx->setCursor(16, 88);
    _gfx->printf("Capacity: %.1f Ah | Cycles: %u", data.capacityAh, data.cycles);

    _gfx->setCursor(16, 103);
    _gfx->printf("Health: %.1f/4.0 [%s]", data.healthRating, data.healthBar);

    _gfx->setCursor(16, 118);
    _gfx->printf("Temp: Cell %.1f C | FET %.1f C",
                 data.tempCellC > -50.0f ? data.tempCellC : 25.0f,
                 data.tempMosfetC > -50.0f ? data.tempMosfetC : 25.0f);

    // Lock Status Badge
    int badgeY = 138;
    if (data.locked) {
        _gfx->fillRoundRect(16, badgeY, 214, 26, 4, COLOR_DANGER);
        _gfx->setTextColor(WHITE);
        _gfx->setCursor(24, badgeY + 8);
        _gfx->printf("LOCKED: %s", data.failureText);

        _gfx->setCursor(16, badgeY + 32);
        _gfx->setTextColor(COLOR_WARN);
        if (data.lockCauses & MAKITA_LF_N34) _gfx->print("Cause: Nybble 34 (Charger lock)  ");
        if (data.lockCauses & MAKITA_LF_CS0) _gfx->print("CS0 Err ");
        if (data.lockCauses & MAKITA_LF_CS2) _gfx->print("CS2 Err ");
    } else {
        _gfx->fillRoundRect(16, badgeY, 214, 26, 4, COLOR_OK);
        _gfx->setTextColor(0x0000);
        _gfx->setCursor(40, badgeY + 8);
        _gfx->print("UNLOCKED - READY TO CHARGE");

        _gfx->setCursor(16, badgeY + 32);
        _gfx->setTextColor(COLOR_TEXT_MUTED);
        _gfx->print("All BMS checksums valid & clean.");
    }

    // --- Right Card: Cell Voltages & Imbalance ---
    drawCard(244, 38, 228, 192, COLOR_CARD, "Pack & Cell Voltages");

    _gfx->setTextColor(0x07FF);
    _gfx->setTextSize(1);
    _gfx->setCursor(252, 58);
    _gfx->printf("Pack Voltage: %.2f V  (SOC: %.0f%%)", data.packVoltage, data.socPercent);

    int barY = 75;
    for (int i = 0; i < 5; i++) {
        float v = data.cellV[i];
        _gfx->setTextColor(WHITE);
        _gfx->setCursor(252, barY);
        _gfx->printf("C%d: %.3f V", i + 1, v);

        float pct = (v - 3.0f) / (4.2f - 3.0f) * 100.0f;
        uint16_t barCol = (v < 3.2f) ? COLOR_DANGER : (v > 4.25f) ? COLOR_WARN : COLOR_OK;
        drawProgressBar(330, barY, 130, 9, pct, barCol, 0x2965);
        barY += 15;
    }

    // Imbalance spread
    _gfx->setCursor(252, 160);
    if (data.cellImbalance) {
        _gfx->setTextColor(COLOR_WARN);
        _gfx->printf("Delta: %.3f V - IMBALANCE >= 0.3V!", data.cellDiff);
    } else {
        _gfx->setTextColor(COLOR_OK);
        _gfx->printf("Delta: %.3f V (Balanced)", data.cellDiff);
    }

    if (data.cellImbalance) {
        _gfx->setCursor(252, 175);
        _gfx->setTextColor(0xFBE0);
        _gfx->print("Check tabs or manually balance cells!");
    } else {
        _gfx->setCursor(252, 175);
        _gfx->setTextColor(COLOR_TEXT_MUTED);
        _gfx->print("BMS cell health verified.");
    }

    // --- Bottom Action Buttons ---
    int btnY = 236;
    drawButton(8, btnY, 105, 30, "SCAN", 0x2965);
    drawButton(120, btnY, 120, 30, "AUTO-UNLOCK", data.locked ? COLOR_OK : 0x2965);
    drawButton(248, btnY, 105, 30, "RESET ERR", 0x39E7);
    drawButton(360, btnY, 112, 30, "OMEGA LOCK", COLOR_CARD_ALT);

    if (statusMsg) {
        _gfx->setTextColor(COLOR_WARN);
        _gfx->setTextSize(1);
        _gfx->setCursor(10, 222);
        _gfx->print(statusMsg);
    }
}

void DisplayManager::renderHikokiScreen(const HikokiData &data, HikokiSubTab subTab, const char *statusMsg) {
    renderHeader(TAB_HIKOKI);
    _gfx->fillRect(0, 33, SCREEN_WIDTH, SCREEN_HEIGHT - 33, COLOR_BG);

    // Hikoki Subtab Selector Bar
    static const char *subTabNames[] = { "GENERAL", "CELLS & T", "ERRORS", "TOOLS", "FLAGS" };
    int subX = 8;
    for (int i = 0; i < 5; i++) {
        uint16_t tabCol = (subTab == i) ? COLOR_HIKOKI : 0x2125;
        _gfx->fillRoundRect(subX, 36, 88, 20, 3, tabCol);
        if (subTab == i) _gfx->drawRoundRect(subX, 36, 88, 20, 3, WHITE);
        _gfx->setTextColor(WHITE);
        _gfx->setTextSize(1);
        _gfx->setCursor(subX + 12, 42);
        _gfx->print(subTabNames[i]);
        subX += 93;
    }

    if (!data.connected) {
        drawCard(20, 62, 440, 165, COLOR_CARD, "HiKOKI MultiVolt Diagnostic");
        _gfx->setTextColor(0xBDD7);
        _gfx->setTextSize(2);
        _gfx->setCursor(60, 100);
        _gfx->print("NO HIKOKI BATTERY DETECTED");

        _gfx->setTextSize(1);
        _gfx->setTextColor(COLOR_TEXT_MUTED);
        _gfx->setCursor(60, 135);
        _gfx->print("Connect Signal T (GPIO16), LS (GPIO17), V (GPIO18), GND.");
        _gfx->setCursor(60, 155);
        _gfx->print("Press the battery fuel gauge button to wake the BMS!");

        drawButton(180, 180, 120, 32, "WAKE & READ", COLOR_HIKOKI);
        return;
    }

    // Main Card content depending on selected subtab
    drawCard(8, 60, 464, 168, COLOR_CARD);
    _gfx->setTextSize(1);
    _gfx->setTextColor(WHITE);

    if (subTab == HIKOKI_SUB_INFO) {
        _gfx->setCursor(20, 75);
        _gfx->printf("Battery Model    : %s", data.model);
        _gfx->setCursor(20, 92);
        _gfx->printf("Serial Number    : %s", data.serial);
        _gfx->setCursor(20, 109);
        _gfx->printf("Manufacture Date : %s", data.mfgDate);
        _gfx->setCursor(20, 126);
        _gfx->printf("Charge Count     : %u cycles", data.chargeCount);
        _gfx->setCursor(20, 143);
        _gfx->printf("Estimated SOC    : %.1f %%", data.capacityPercent);
        _gfx->setCursor(20, 160);
        _gfx->printf("Wake / Standby   : %s hours", data.wakeTime);
        _gfx->setCursor(20, 177);
        _gfx->printf("Runtime (18V/36V): 18V: %s h  |  36V: %s h", data.work18VTime, data.work36VTime);
    } else if (subTab == HIKOKI_SUB_CELLS) {
        _gfx->setCursor(20, 75);
        _gfx->printf("Bank Voltages    : Lower: %.3f V  |  Upper: %.3f V", data.lowerBankV, data.upperBankV);
        _gfx->setCursor(20, 92);
        _gfx->printf("Bank Imbalances  : Lower: %.3f V  |  Upper: %.3f V", data.unbalanceLowerV, data.unbalanceUpperV);
        _gfx->setCursor(20, 109);
        _gfx->printf("Temperatures     : Lower: %.1f C  |  Upper: %.1f C", data.tempLowerC, data.tempUpperC);

        int cellY = 130;
        for (int i = 0; i < 5; i++) {
            _gfx->setCursor(20, cellY);
            _gfx->printf("Cell L%d: %.3f V", i + 1, data.cellV[i]);
            float pct = (data.cellV[i] - 3.0f) / (4.2f - 3.0f) * 100.0f;
            drawProgressBar(150, cellY, 150, 9, pct, COLOR_OK, 0x2965);
            cellY += 15;
        }
    } else if (subTab == HIKOKI_SUB_ERRORS) {
        _gfx->setCursor(20, 75);
        _gfx->print("Logged BMS Faults (Fatal | Cycle # | Description):");
        if (data.errorCount == 0) {
            _gfx->setTextColor(COLOR_OK);
            _gfx->setCursor(20, 105);
            _gfx->print("No errors stored in BMS memory. Pack is clean!");
        } else {
            int errY = 95;
            for (int i = 0; i < data.errorCount && i < 6; i++) {
                _gfx->setCursor(20, errY);
                _gfx->setTextColor(data.errors[i].fatal ? COLOR_DANGER : COLOR_WARN);
                _gfx->printf("[%s] Cycle %u: %s",
                             data.errors[i].fatal ? "FATAL" : "WARN",
                             data.errors[i].chargeNo,
                             data.errors[i].message);
                errY += 16;
            }
        }
    } else if (subTab == HIKOKI_SUB_TOOLS) {
        _gfx->setCursor(20, 75);
        _gfx->print("HiKOKI Tool Attachment History:");
        if (data.toolCount == 0) {
            _gfx->setTextColor(COLOR_TEXT_MUTED);
            _gfx->setCursor(20, 105);
            _gfx->print("No tool connection history found.");
        } else {
            int toolY = 95;
            for (int i = 0; i < data.toolCount && i < 6; i++) {
                _gfx->setCursor(20, toolY);
                _gfx->printf("%-24s: %u times used", data.tools[i].name, data.tools[i].count);
                toolY += 16;
            }
        }
    } else if (subTab == HIKOKI_SUB_FLAGS) {
        _gfx->setCursor(20, 75);
        _gfx->printf("BMS Error Flag Set  : %s", data.errorFlagSet ? "YES (LOCKED)" : "No (Normal)");
        _gfx->setCursor(20, 92);
        _gfx->printf("Internal Fuse Status: %s", data.fuseError ? "BLOWN (FAIL)" : "OK (Intact)");
        _gfx->setCursor(20, 109);
        _gfx->printf("Total Overdischarges: %u events", data.totalOverdischarge);
        _gfx->setCursor(20, 126);
        _gfx->printf("Overcurrent Events  : 18V: %u  |  36V: %u  |  High Load: %u",
                     data.oc18vBrushless + data.oc18vBrushed, data.oc36v, data.ocHighLoad);
        _gfx->setCursor(20, 143);
        _gfx->printf("Overtemperature Disch: %u events", data.dischOvertemp);
    }

    // --- Bottom Action Buttons ---
    int btnY = 236;
    drawButton(15, btnY, 130, 30, "READ / SCAN", COLOR_HIKOKI);
    drawButton(160, btnY, 160, 30, "RESET LOCKOUT", COLOR_DANGER);
    drawButton(335, btnY, 130, 30, "DEEP SLEEP", COLOR_CARD_ALT);

    if (statusMsg) {
        _gfx->setTextColor(COLOR_WARN);
        _gfx->setTextSize(1);
        _gfx->setCursor(10, 222);
        _gfx->print(statusMsg);
    }
}

void DisplayManager::renderAutoDetectScreen(const char *statusMsg) {
    renderHeader(TAB_AUTO);
    _gfx->fillRect(0, 33, SCREEN_WIDTH, SCREEN_HEIGHT - 33, COLOR_BG);

    drawCard(30, 50, 420, 170, COLOR_CARD, "Automatic Battery Detection");
    _gfx->setTextColor(0x07FF);
    _gfx->setTextSize(2);
    _gfx->setCursor(60, 95);
    _gfx->print("SCANNING BATTERY PORTS...");

    _gfx->setTextSize(1);
    _gfx->setTextColor(COLOR_TEXT_MUTED);
    _gfx->setCursor(60, 135);
    _gfx->print("- Monitoring Makita LXT 1-Wire bus (GPIO 6/7)...");
    _gfx->setCursor(60, 155);
    _gfx->print("- Monitoring HiKOKI MultiVolt Serial bus (GPIO 16/17/18)...");

    if (statusMsg) {
        _gfx->setTextColor(COLOR_OK);
        _gfx->setCursor(60, 185);
        _gfx->print(statusMsg);
    }
}

void DisplayManager::renderConfirmDialog(const char *title, const char *message) {
    _gfx->fillRoundRect(60, 50, 360, 170, 8, 0x18C3);
    _gfx->drawRoundRect(60, 50, 360, 170, 8, COLOR_WARN);

    _gfx->setTextColor(COLOR_WARN);
    _gfx->setTextSize(1);
    _gfx->setCursor(75, 65);
    _gfx->print(title);
    _gfx->drawFastHLine(70, 80, 340, 0x39E7);

    _gfx->setTextColor(WHITE);
    _gfx->setCursor(75, 95);
    _gfx->print(message);

    _gfx->setTextColor(COLOR_TEXT_MUTED);
    _gfx->setCursor(75, 120);
    _gfx->print("Ensure cells are balanced before clearing lockouts!");

    drawButton(90, 160, 130, 36, "CANCEL", 0x39E7);
    drawButton(260, 160, 130, 36, "CONFIRM", COLOR_DANGER);
}

UIAction DisplayManager::handleTouch(int tx, int ty, ActiveTab &currentTab, HikokiSubTab &currentSubTab, bool inConfirmDialog) {
    if (inConfirmDialog) {
        // Cancel button: 90..220, 160..196
        if (tx >= 90 && tx <= 220 && ty >= 160 && ty <= 196) {
            return ACTION_NONE;
        }
        // Confirm button: 260..390, 160..196
        if (tx >= 260 && tx <= 390 && ty >= 160 && ty <= 196) {
            return (currentTab == TAB_HIKOKI) ? ACTION_RESET_HIKOKI_LOCK : ACTION_OMEGA_LOCK_MAKITA;
        }
        return ACTION_NONE;
    }

    // Top Tabs: y = 4..28
    if (ty >= 2 && ty <= 32) {
        if (tx >= 150 && tx <= 250) {
            currentTab = TAB_MAKITA;
            return ACTION_SWITCH_TAB_MAKITA;
        }
        if (tx >= 260 && tx <= 360) {
            currentTab = TAB_HIKOKI;
            return ACTION_SWITCH_TAB_HIKOKI;
        }
        if (tx >= 370 && tx <= 470) {
            currentTab = TAB_AUTO;
            return ACTION_SWITCH_TAB_AUTO;
        }
    }

    // Makita screen buttons
    if (currentTab == TAB_MAKITA) {
        if (ty >= 230 && ty <= 270) {
            if (tx >= 8 && tx <= 113)   return ACTION_SCAN_MAKITA;
            if (tx >= 120 && tx <= 240) return ACTION_UNLOCK_MAKITA;
            if (tx >= 248 && tx <= 353) return ACTION_RESET_MAKITA_DA04;
            if (tx >= 360 && tx <= 472) return ACTION_OMEGA_LOCK_MAKITA;
        }
    }

    // HiKOKI screen
    if (currentTab == TAB_HIKOKI) {
        // Subtab bar: y = 36..56
        if (ty >= 34 && ty <= 58) {
            int subX = 8;
            for (int i = 0; i < 5; i++) {
                if (tx >= subX && tx <= subX + 88) {
                    currentSubTab = (HikokiSubTab)i;
                    return ACTION_NONE;
                }
                subX += 93;
            }
        }
        // Bottom buttons: y = 230..270
        if (ty >= 230 && ty <= 270) {
            if (tx >= 15 && tx <= 145)  return ACTION_SCAN_HIKOKI;
            if (tx >= 160 && tx <= 320) return ACTION_RESET_HIKOKI_LOCK;
            if (tx >= 335 && tx <= 465) return ACTION_DEEP_SLEEP;
        }
    }

    return ACTION_NONE;
}
