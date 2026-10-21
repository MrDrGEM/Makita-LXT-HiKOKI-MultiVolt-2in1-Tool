#include <Arduino.h>
#include "config.h"
#include "makita/MakitaReader.h"
#include "hikoki/HikokiReader.h"
#include "ui/DisplayManager.h"

// Hardware instances
static MakitaReader g_makita(MAKITA_PIN_OW, MAKITA_PIN_EN);
static HikokiReader g_hikoki(HIKOKI_PIN_IN, HIKOKI_PIN_INV_OUT, HIKOKI_PIN_OUT);
static DisplayManager g_display;

// Data state
static MakitaData g_makitaData;
static HikokiData g_hikokiData;

static ActiveTab g_currentTab = TAB_AUTO;
static HikokiSubTab g_hikokiSubTab = HIKOKI_SUB_INFO;

static bool g_inConfirmDialog = false;
static const char *g_confirmTitle = "";
static const char *g_confirmMsg = "";
static UIAction g_pendingAction = ACTION_NONE;

static uint32_t g_lastAutoProbe = 0;
static uint32_t g_lastTouchTime = 0;

void refreshCurrentScreen(const char *statusMsg = nullptr) {
    if (g_inConfirmDialog) {
        g_display.renderConfirmDialog(g_confirmTitle, g_confirmMsg);
        return;
    }

    switch (g_currentTab) {
        case TAB_MAKITA:
            g_display.renderMakitaScreen(g_makitaData, statusMsg);
            break;
        case TAB_HIKOKI:
            g_display.renderHikokiScreen(g_hikokiData, g_hikokiSubTab, statusMsg);
            break;
        case TAB_AUTO:
            g_display.renderAutoDetectScreen(statusMsg);
            break;
    }
}

void performMakitaScan() {
    refreshCurrentScreen("Reading Makita BMS...");
    if (g_makita.readAllData(g_makitaData)) {
        refreshCurrentScreen(g_makitaData.locked ? "Scan OK - Battery LOCKED" : "Scan OK - Battery UNLOCKED");
    } else {
        refreshCurrentScreen("No Makita response.");
    }
}

void performHikokiScan() {
    refreshCurrentScreen("Reading HiKOKI BMS...");
    if (g_hikoki.readAllData(g_hikokiData)) {
        refreshCurrentScreen(g_hikokiData.errorFlagSet ? "Scan OK - Fault flags set" : "Scan OK - Battery Healthy");
    } else {
        refreshCurrentScreen("No HiKOKI response.");
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("================================================");
    Serial.println("   Makita & HiKOKI Standalone Battery Tool      ");
    Serial.println("   Target: Guition JC4827W543 (ESP32-S3)       ");
    Serial.println("================================================");

    // Initialize display and touch
    if (!g_display.begin()) {
        Serial.println("FATAL: Display/Touch initialization failed!");
    } else {
        Serial.println("Display & Capacitive Touch GT911 ready.");
    }

    // Initialize battery interfaces
    g_makita.begin();
    g_hikoki.begin();

    Serial.println("Makita interface on GPIO 6 (OW) / 7 (EN)");
    Serial.println("HiKOKI interface on GPIO 16 (IN) / 17 (LS) / 18 (V)");

    refreshCurrentScreen("Ready. Waiting for battery...");
}

void loop() {
    // 1. Process Capacitive Touch Input
    int tx, ty;
    if (g_display.getTouchPoint(tx, ty)) {
        if (millis() - g_lastTouchTime > 250) { // Touch debounce
            g_lastTouchTime = millis();

            if (g_inConfirmDialog) {
                // Cancel touch area
                if (tx >= 90 && tx <= 220 && ty >= 160 && ty <= 196) {
                    g_inConfirmDialog = false;
                    refreshCurrentScreen();
                }
                // Confirm touch area
                else if (tx >= 260 && tx <= 390 && ty >= 160 && ty <= 196) {
                    g_inConfirmDialog = false;
                    if (g_pendingAction == ACTION_RESET_HIKOKI_LOCK) {
                        refreshCurrentScreen("Executing HiKOKI Reset Lockout...");
                        g_hikoki.resetLockout();
                        performHikokiScan();
                    } else if (g_pendingAction == ACTION_OMEGA_LOCK_MAKITA) {
                        refreshCurrentScreen("Writing Makita Omega Lock...");
                        g_makita.setOmegaLock(g_makitaData);
                        refreshCurrentScreen("Omega Lock Set!");
                    }
                    g_pendingAction = ACTION_NONE;
                }
            } else {
                HikokiSubTab prevSubTab = g_hikokiSubTab;
                ActiveTab prevTab = g_currentTab;
                UIAction action = g_display.handleTouch(tx, ty, g_currentTab, g_hikokiSubTab, false);

                if (prevTab != g_currentTab || prevSubTab != g_hikokiSubTab) {
                    refreshCurrentScreen();
                }

                switch (action) {
                    case ACTION_SCAN_MAKITA:
                        performMakitaScan();
                        break;
                    case ACTION_UNLOCK_MAKITA:
                        refreshCurrentScreen("Auto-unlocking Makita...");
                        if (g_makita.autoUnlock(g_makitaData)) {
                            refreshCurrentScreen("SUCCESS: Battery Unlocked!");
                        } else {
                            refreshCurrentScreen("Unlock FAILED (Check cells/fuse)");
                        }
                        break;
                    case ACTION_RESET_MAKITA_DA04:
                        refreshCurrentScreen("Sending DA04 Reset...");
                        g_makita.resetErrorsDA04();
                        performMakitaScan();
                        break;
                    case ACTION_OMEGA_LOCK_MAKITA:
                        g_inConfirmDialog = true;
                        g_confirmTitle = "SET MAKITA OMEGA LOCK?";
                        g_confirmMsg = "This sets nybble 34 to lock charging.";
                        g_pendingAction = ACTION_OMEGA_LOCK_MAKITA;
                        refreshCurrentScreen();
                        break;
                    case ACTION_SCAN_HIKOKI:
                        performHikokiScan();
                        break;
                    case ACTION_RESET_HIKOKI_LOCK:
                        g_inConfirmDialog = true;
                        g_confirmTitle = "RESET HIKOKI BMS LOCKOUT?";
                        g_confirmMsg = "Clear software lock? Verify cell balance!";
                        g_pendingAction = ACTION_RESET_HIKOKI_LOCK;
                        refreshCurrentScreen();
                        break;
                    case ACTION_DEEP_SLEEP:
                        refreshCurrentScreen("Sleeping... Press reset/touch to wake.");
                        delay(500);
                        esp_deep_sleep_start();
                        break;
                    default:
                        break;
                }
            }
        }
    }

    // 2. Auto-Detect Loop (when on TAB_AUTO)
    if (g_currentTab == TAB_AUTO && !g_inConfirmDialog) {
        if (millis() - g_lastAutoProbe > 1500) {
            g_lastAutoProbe = millis();

            // Probe Makita 1-Wire
            if (g_makita.isBatteryPresent()) {
                g_currentTab = TAB_MAKITA;
                performMakitaScan();
                return;
            }

            // Probe HiKOKI Serial
            if (g_hikoki.isBatteryPresent()) {
                g_currentTab = TAB_HIKOKI;
                performHikokiScan();
                return;
            }
        }
    }

    // 3. Serial Monitor Control (Commands over USB-CDC)
    if (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 's':
            case 'S':
                if (g_currentTab == TAB_MAKITA) performMakitaScan();
                else if (g_currentTab == TAB_HIKOKI) performHikokiScan();
                break;
            case 'u':
            case 'U':
                if (g_currentTab == TAB_MAKITA) {
                    refreshCurrentScreen("Unlocking over Serial...");
                    g_makita.autoUnlock(g_makitaData);
                    refreshCurrentScreen();
                }
                break;
            case '0':
                g_currentTab = TAB_HIKOKI;
                g_hikokiSubTab = HIKOKI_SUB_INFO;
                performHikokiScan();
                break;
            case '1':
                g_currentTab = TAB_HIKOKI;
                g_hikokiSubTab = HIKOKI_SUB_CELLS;
                performHikokiScan();
                break;
            case '2':
                g_currentTab = TAB_HIKOKI;
                g_hikokiSubTab = HIKOKI_SUB_ERRORS;
                performHikokiScan();
                break;
            case '3':
                g_currentTab = TAB_HIKOKI;
                g_hikokiSubTab = HIKOKI_SUB_TOOLS;
                performHikokiScan();
                break;
            case '4':
                g_currentTab = TAB_HIKOKI;
                g_hikokiSubTab = HIKOKI_SUB_FLAGS;
                performHikokiScan();
                break;
            case '6':
                g_hikoki.resetLockout();
                performHikokiScan();
                break;
            case 'p':
            case 'P':
                Serial.println("Entering Deep Sleep...");
                esp_deep_sleep_start();
                break;
        }
    }

    delay(20);
}
