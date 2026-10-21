#ifndef MAKITA_READER_H
#define MAKITA_READER_H

#include <Arduino.h>
#include "config.h"
#if __has_include(<OneWire2.h>)
#include <OneWire2.h>
#elif __has_include("../../lib/OneWire2/OneWire2.h")
#include "../../lib/OneWire2/OneWire2.h"
#else
#include "OneWire2.h"
#endif

// Lock cause flags (bitfield)
static constexpr uint16_t MAKITA_LF_CS0 = 0x0002; // CS0 mismatch (nybbles 0-15)
static constexpr uint16_t MAKITA_LF_CS2 = 0x0008; // CS2 mismatch (nybbles 32-40)
static constexpr uint16_t MAKITA_LF_N34 = 0x0040; // nybble 34 != 0 (charger lock)

// Failure codes
enum MakitaFailureCode : uint8_t {
    MAKITA_FC_OK         = 0,
    MAKITA_FC_OVERLOADED = 1,
    MAKITA_FC_WARNING    = 5,
    MAKITA_FC_DEAD       = 15,
};

enum class MakitaBatteryType : int8_t {
    UNKNOWN = -1,
    T0      = 0,
    T2      = 2,
    T3      = 3,
    T5      = 5,
    T6      = 6,
};

struct MakitaData {
    bool connected;
    char model[16];
    uint8_t romId[8];
    bool romIdValid;
    MakitaBatteryType type;
    int typeInt;

    float packVoltage;
    float cellV[10];
    uint8_t cellCount;
    float cellDiff;
    bool cellImbalance; // True if cell spread >= 0.300V

    float tempCellC;
    float tempMosfetC;

    uint16_t cycles;
    float healthRating; // 0.0 - 4.0
    char healthBar[6];  // e.g. "####"

    float capacityAh;
    float socPercent;

    bool locked;
    uint8_t failureCode;
    char failureText[32];
    uint16_t lockCauses; // bitfield of MAKITA_LF_*

    uint8_t rawFrame[32];
};

class MakitaReader {
public:
    MakitaReader(uint8_t owPin = MAKITA_PIN_OW, uint8_t enPin = MAKITA_PIN_EN);

    void begin();
    bool isBatteryPresent();
    bool readAllData(MakitaData &data);
    bool autoUnlock(MakitaData &data);
    bool resetErrorsDA04();
    bool repairFrame(MakitaData &data);
    bool setOmegaLock(MakitaData &data);

    void powerCycleBus(uint16_t offMs = 100, uint16_t onMs = 150);
    void busEnable(uint16_t ms = 150);
    void busDisable();

private:
    uint8_t _owPin;
    uint8_t _enPin;
    OneWire _ow;

    bool _chargerArmIssued;
    bool _inTestMode;

    enum BusResult { BUS_OK = 0, BUS_NO_PRESENCE = 1 };
    enum BasicInfoResult { BASIC_INFO_NO_RESP = 0, BASIC_INFO_OK = 1, BASIC_INFO_PRE_T0 = -1 };

    BusResult owTransaction(bool managed, uint8_t romPrefix,
                            const uint8_t *cmd, uint8_t cmdLen,
                            uint8_t *rsp, uint8_t rspLen);

    BusResult cmdCC(const uint8_t *c, uint8_t cl, uint8_t *r, uint8_t rl);
    BusResult cmdCCRaw(const uint8_t *c, uint8_t cl, uint8_t *r, uint8_t rl);
    BusResult cmd33WithRom(const uint8_t *c, uint8_t cl, uint8_t romOut[8], uint8_t *rsp, uint8_t rspLen);

    uint8_t nybbleGet(const uint8_t *d, uint8_t n);
    void nybbleSet(uint8_t *d, uint8_t n, uint8_t v);
    uint8_t nybblePair(const uint8_t *d, uint8_t hi, uint8_t lo);
    uint8_t checksumCalc(const uint8_t *d, uint8_t s, uint8_t e);

    BusResult enterTestMode();
    void exitTestMode();
    BasicInfoResult readBasicInfo(uint8_t d[32]);
    bool waitForResponse(uint32_t timeoutMs = 10000);

    bool readVoltagesType0(MakitaData &data);
    bool readVoltagesType5(MakitaData &data);
    bool readVoltagesType6(MakitaData &data);
    bool readModel(char *modelBuf, size_t maxLen, MakitaBatteryType type);
};

#endif // MAKITA_READER_H
