#ifndef HIKOKI_READER_H
#define HIKOKI_READER_H

#include <Arduino.h>
#include "config.h"

struct HikokiErrorItem {
    bool fatal;
    uint16_t chargeNo;
    char message[64];
};

struct HikokiToolItem {
    char name[48];
    uint16_t count;
};

struct HikokiData {
    bool connected;
    char model[32];
    char serial[32];
    char mfgDate[32];
    char wakeTime[16];
    char work18VTime[16];
    char work36VTime[16];
    uint16_t chargeCount;
    float capacityPercent;

    // Voltages and Temperatures
    float lowerBankV;
    float upperBankV;
    float unbalanceLowerV;
    float unbalanceUpperV;
    float tempLowerC;
    float tempUpperC;
    float cellV[5]; // L1, L2, L3, L4, L5

    // Error Logs
    HikokiErrorItem errors[10];
    uint8_t errorCount;

    // Tool History
    HikokiToolItem tools[10];
    uint8_t toolCount;

    // Safety & Protection Flags
    bool errorFlagSet;
    bool fuseError;
    bool prevOvercharge;
    uint16_t unbalancedChgCount;
    uint16_t overtempChgCount;
    uint16_t overcurrentChgCount;

    // Stress Stats
    uint16_t totalOverdischarge;
    uint16_t oc18vBrushless;
    uint16_t oc18vBrushed;
    uint16_t oc36v;
    uint16_t ocHighLoad;
    uint16_t dischOvertemp;
};

class HikokiReader {
public:
    HikokiReader(uint8_t inPin = HIKOKI_PIN_IN, 
                 uint8_t invOutPin = HIKOKI_PIN_INV_OUT, 
                 uint8_t outPin = HIKOKI_PIN_OUT);

    void begin();
    void sendWakeup();
    bool isBatteryPresent();
    bool readAllData(HikokiData &data);
    bool resetLockout();

    static const char* getBatteryTypeName(uint16_t typeCode);
    static const char* getErrorName(uint16_t typeCode);
    static const char* getToolName(uint16_t toolCode);

private:
    uint8_t _inPin;
    uint8_t _invOutPin;
    uint8_t _outPin;

    int recvByte();
    void sendByte(uint8_t b);
    void writeBytes(const uint8_t *buf, int len);
    void readBytes(uint8_t *buf, int len);
    int doCommand(const char *cmd, uint16_t returnValues[], bool isShort = false);
    size_t splitTo(uint8_t delim, uint8_t *src, size_t len, uint8_t **dest, size_t dest_count);
    double calculateCapacity(double voltage);
    bool isBrokenBattery(uint16_t batteryCode, uint16_t errorFlags[]);
};

#endif // HIKOKI_READER_H
