#include "MakitaReader.h"

// ─── Protocol constants ───────────────────────────────────────
static constexpr uint8_t CMD_BASIC_INFO[]     = { 0xAA, 0x00 };
static constexpr uint8_t CMD_MODEL[]          = { 0xDC, 0x0C };
static constexpr uint8_t CMD_TESTMODE_ENTER[] = { 0xD9, 0x96, 0xA5 };
static constexpr uint8_t CMD_TESTMODE_EXIT[]  = { 0xD9, 0xFF, 0xFF };
static constexpr uint8_t CMD_RESET_ERRORS[]   = { 0xDA, 0x04 };
static constexpr uint8_t CMD_CC_F0[]          = { 0xF0, 0x00 };
static constexpr uint8_t CMD_STORE[]          = { 0x55, 0xA5 };
static constexpr uint8_t FRAME_WRITE_OPCODE   = 0x0F;
static constexpr uint8_t FRAME_WRITE_PAD      = 0x00;
static constexpr uint8_t BASIC_INFO_LEN       = 32;
static constexpr uint8_t TYPE_PROBE_MAGIC     = 0x06;

static constexpr uint16_t BUS_BYTE_GAP_US     = 90;
static constexpr uint16_t BUS_SETTLE_US       = 100;
static constexpr uint16_t BUS_SETTLE_ON_MS    = 150;
static constexpr uint16_t POWERCYCLE_OFF_MS   = 100;
static constexpr uint16_t INTER_CMD_MS        = 30;

static inline uint16_t le16(const uint8_t *b) {
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

MakitaReader::MakitaReader(uint8_t owPin, uint8_t enPin)
    : _owPin(owPin), _enPin(enPin), _ow(owPin),
      _chargerArmIssued(false), _inTestMode(false) {}

void MakitaReader::begin() {
    pinMode(_enPin, OUTPUT);
    digitalWrite(_enPin, LOW);
    _ow.begin(_owPin);
}

void MakitaReader::busEnable(uint16_t ms) {
    digitalWrite(_enPin, HIGH);
    delay(ms);
}

void MakitaReader::busDisable() {
    digitalWrite(_enPin, LOW);
}

void MakitaReader::powerCycleBus(uint16_t offMs, uint16_t onMs) {
    busDisable();
    _chargerArmIssued = false;
    delay(offMs);
    busEnable(onMs);
}

bool MakitaReader::isBatteryPresent() {
    digitalWrite(_enPin, HIGH);
    delay(BUS_SETTLE_ON_MS);
    bool p = (_ow.reset() == 1);
    digitalWrite(_enPin, LOW);
    return p;
}

MakitaReader::BusResult MakitaReader::owTransaction(bool managed, uint8_t romPrefix,
                                                    const uint8_t *cmd, uint8_t cmdLen,
                                                    uint8_t *rsp, uint8_t rspLen) {
    if (managed) busEnable();
    if (!_ow.reset()) {
        if (managed) busDisable();
        return BUS_NO_PRESENCE;
    }
    _ow.write(romPrefix, 0);
    if (romPrefix == 0x33) {
        for (uint8_t i = 0; i < 8; i++) {
            delayMicroseconds(BUS_BYTE_GAP_US);
            rsp[i] = _ow.read();
        }
        rsp += 8;
    }
    for (uint8_t i = 0; i < cmdLen; i++) {
        delayMicroseconds(BUS_BYTE_GAP_US);
        _ow.write(cmd[i], 0);
    }
    delayMicroseconds(BUS_SETTLE_US);
    for (uint8_t i = 0; i < rspLen; i++) {
        delayMicroseconds(BUS_BYTE_GAP_US);
        rsp[i] = _ow.read();
    }
    if (managed) busDisable();
    return BUS_OK;
}

MakitaReader::BusResult MakitaReader::cmdCC(const uint8_t *c, uint8_t cl, uint8_t *r, uint8_t rl) {
    return owTransaction(true, 0xCC, c, cl, r, rl);
}

MakitaReader::BusResult MakitaReader::cmdCCRaw(const uint8_t *c, uint8_t cl, uint8_t *r, uint8_t rl) {
    return owTransaction(false, 0xCC, c, cl, r, rl);
}

MakitaReader::BusResult MakitaReader::cmd33WithRom(const uint8_t *c, uint8_t cl,
                                                   uint8_t romOut[8], uint8_t *rsp, uint8_t rspLen) {
    uint8_t buf[8 + 32] = {0};
    BusResult res = owTransaction(true, 0x33, c, cl, buf, rspLen);
    if (res == BUS_OK) {
        memcpy(romOut, buf, 8);
        if (rsp && rspLen) memcpy(rsp, buf + 8, rspLen);
    }
    return res;
}

uint8_t MakitaReader::nybbleGet(const uint8_t *d, uint8_t n) {
    return (n % 2 == 0) ? (d[n/2] & 0x0F) : ((d[n/2] >> 4) & 0x0F);
}

void MakitaReader::nybbleSet(uint8_t *d, uint8_t n, uint8_t v) {
    v &= 0x0F;
    if (n % 2 == 0) d[n/2] = (d[n/2] & 0xF0) | v;
    else            d[n/2] = (d[n/2] & 0x0F) | (v << 4);
}

uint8_t MakitaReader::nybblePair(const uint8_t *d, uint8_t hi, uint8_t lo) {
    return (uint8_t)((nybbleGet(d, hi) << 4) | nybbleGet(d, lo));
}

uint8_t MakitaReader::checksumCalc(const uint8_t *d, uint8_t s, uint8_t e) {
    uint8_t sum = 0;
    for (uint8_t i = s; i <= e; i++) sum += nybbleGet(d, i);
    return sum & 0x0F;
}

MakitaReader::BusResult MakitaReader::enterTestMode() {
    uint8_t r[1] = {0};
    BusResult res = cmdCC(CMD_TESTMODE_ENTER, sizeof(CMD_TESTMODE_ENTER), r, 1);
    if (res == BUS_OK) {
        _inTestMode = true;
        delay(INTER_CMD_MS);
    }
    return res;
}

void MakitaReader::exitTestMode() {
    if (!_inTestMode) return;
    uint8_t r[1] = {0};
    cmdCC(CMD_TESTMODE_EXIT, sizeof(CMD_TESTMODE_EXIT), r, 1);
    _inTestMode = false;
    delay(INTER_CMD_MS);
}

MakitaReader::BasicInfoResult MakitaReader::readBasicInfo(uint8_t d[32]) {
    if (cmdCC(CMD_BASIC_INFO, sizeof(CMD_BASIC_INFO), d, 32) != BUS_OK)
        return BASIC_INFO_NO_RESP;
    uint8_t orv = 0, andv = 0xFF;
    for (uint8_t i = 0; i < 32; i++) {
        orv |= d[i];
        andv &= d[i];
    }
    if (orv == 0x00) return BASIC_INFO_NO_RESP;
    if (andv == 0xFF) return BASIC_INFO_NO_RESP;
    return BASIC_INFO_OK;
}

bool MakitaReader::waitForResponse(uint32_t timeoutMs) {
    uint32_t started = millis();
    while (millis() - started < timeoutMs) {
        powerCycleBus();
        uint8_t dummy[32];
        if (readBasicInfo(dummy) == BASIC_INFO_OK) return true;
        delay(30);
    }
    return false;
}

bool MakitaReader::readModel(char *modelBuf, size_t maxLen, MakitaBatteryType type) {
    memset(modelBuf, 0, maxLen);
    if (type == MakitaBatteryType::T5) {
        uint8_t r[8] = {0};
        static const uint8_t cmd5[] = {0x31};
        if (cmdCC(cmd5, 1, r, 7) == BUS_OK) {
            for (int i = 0; i < 7; i++) modelBuf[i] = (r[i] >= 32 && r[i] <= 126) ? (char)r[i] : ' ';
            return true;
        }
        return false;
    }
    uint8_t r[8] = {0};
    if (cmdCC(CMD_MODEL, sizeof(CMD_MODEL), r, 7) == BUS_OK) {
        for (int i = 0; i < 7; i++) modelBuf[i] = (r[i] >= 32 && r[i] <= 126) ? (char)r[i] : ' ';
        return true;
    }
    return false;
}

bool MakitaReader::readVoltagesType0(MakitaData &data) {
    static const uint8_t ext[] = {0xD7, 0x00, 0x00, 0xFF};
    uint8_t r[27] = {0};
    uint8_t n = min(data.cellCount, (uint8_t)10);

    if (cmdCC(ext, sizeof(ext), r, 27) == BUS_OK && le16(&r[0]) > 0) {
        data.packVoltage = le16(&r[0]) / 1000.0f;
        for (uint8_t i = 0; i < min(n, (uint8_t)5); i++) {
            data.cellV[i] = le16(&r[2 + i * 2]) / 1000.0f;
        }
        uint16_t rc = le16(&r[14]), rm = le16(&r[16]);
        data.tempCellC = (rc != 0 && rc != 0xFFFF) ? rc / 100.0f : -99.0f;
        data.tempMosfetC = (rm != 0 && rm != 0xFFFF) ? rm / 100.0f : -99.0f;
        return true;
    }

    static const uint8_t shortCmd[] = {0xD7, 0x00, 0x00, 0x0C};
    uint8_t s[13] = {0};
    if (cmdCC(shortCmd, sizeof(shortCmd), s, 13) == BUS_OK && s[12] == TYPE_PROBE_MAGIC) {
        data.packVoltage = le16(&s[0]) / 1000.0f;
        for (uint8_t i = 0; i < min(n, (uint8_t)5); i++) {
            data.cellV[i] = le16(&s[2 + i * 2]) / 1000.0f;
        }
        return true;
    }
    return false;
}

bool MakitaReader::readVoltagesType5(MakitaData &data) {
    static const uint8_t CELL_CMDS[] = { 0x31, 0x32, 0x33, 0x34, 0x35 };
    uint8_t n = min(data.cellCount, (uint8_t)5);

    uint8_t r[2] = {0};
    cmdCC(&CELL_CMDS[0], 1, r, 2);
    uint16_t mv = le16(r);
    if (mv >= 2000 && mv <= 4500) {
        float pack = mv / 1000.0f;
        data.cellV[0] = mv / 1000.0f;
        busEnable();
        for (uint8_t i = 1; i < n; i++) {
            r[0] = r[1] = 0;
            cmdCCRaw(&CELL_CMDS[i], 1, r, 2);
            mv = le16(r);
            if (mv >= 500 && mv <= 4800) {
                data.cellV[i] = mv / 1000.0f;
                pack += data.cellV[i];
            }
        }
        busDisable();
        data.packVoltage = pack;
        return true;
    }
    return false;
}

bool MakitaReader::readVoltagesType6(MakitaData &data) {
    uint8_t ign[1] = {0}, r[20] = {0};
    busEnable();
    static const uint8_t c[] = {0x10, 0x21};
    cmdCCRaw(c, sizeof(c), ign, 0);
    delay(10);

    bool ok = (_ow.reset() != 0);
    if (ok) {
        delayMicroseconds(BUS_BYTE_GAP_US);
        _ow.write(0xD4, 0);
        delayMicroseconds(BUS_SETTLE_US);
        for (uint8_t i = 0; i < 20; i++) {
            r[i] = _ow.read();
            delayMicroseconds(BUS_BYTE_GAP_US);
        }
    }
    busDisable();
    if (!ok) return false;

    uint8_t n = min(data.cellCount, (uint8_t)10);
    float pack = 0;
    for (uint8_t i = 0; i < n; i++) {
        data.cellV[i] = (6000.0f - le16(&r[i * 2]) / 10.0f) / 1000.0f;
        pack += data.cellV[i];
    }
    data.packVoltage = pack;
    return true;
}

bool MakitaReader::readAllData(MakitaData &data) {
    memset(&data, 0, sizeof(MakitaData));
    powerCycleBus();

    uint8_t d[32] = {0};
    if (readBasicInfo(d) != BASIC_INFO_OK) {
        data.connected = false;
        return false;
    }
    data.connected = true;
    memcpy(data.rawFrame, d, 32);

    // Parse ROM ID
    uint8_t dummy[32];
    data.romIdValid = (cmd33WithRom(CMD_BASIC_INFO, sizeof(CMD_BASIC_INFO), data.romId, dummy, 32) == BUS_OK);

    // Battery Type & Cell count
    uint8_t battTypeRaw = nybbleGet(d, 24);
    data.typeInt = battTypeRaw;
    if (data.romIdValid && data.romId[0] == 0x05) {
        data.type = MakitaBatteryType::T5;
    } else if (battTypeRaw == 0) {
        data.type = MakitaBatteryType::T0;
    } else if (battTypeRaw == 2) {
        data.type = MakitaBatteryType::T2;
    } else if (battTypeRaw == 3) {
        data.type = MakitaBatteryType::T3;
    } else if (battTypeRaw >= 30) {
        data.type = MakitaBatteryType::T6;
    } else {
        data.type = MakitaBatteryType::T0;
    }

    if (battTypeRaw < 13)      data.cellCount = 4;
    else if (battTypeRaw < 30) data.cellCount = 5;
    else                       data.cellCount = 10;

    data.capacityAh = nybblePair(d, 25, 26) / 10.0f;
    if (data.capacityAh < 1.0f || data.capacityAh > 12.0f) {
        data.capacityAh = (battTypeRaw < 30) ? 5.0f : 10.0f;
    }

    readModel(data.model, sizeof(data.model), data.type);

    // Checksums and lock
    uint8_t cs0_calc = checksumCalc(d, 0, 15);
    uint8_t cs0_read = nybbleGet(d, 41);
    uint8_t cs2_calc = checksumCalc(d, 32, 40);
    uint8_t cs2_read = nybbleGet(d, 43);
    uint8_t n34 = nybbleGet(d, 34);

    data.lockCauses = 0;
    if (cs0_calc != cs0_read) data.lockCauses |= MAKITA_LF_CS0;
    if (cs2_calc != cs2_read) data.lockCauses |= MAKITA_LF_CS2;
    if (n34 != 0)             data.lockCauses |= MAKITA_LF_N34;

    data.failureCode = nybbleGet(d, 40);
    data.locked = (data.lockCauses != 0 || data.failureCode != MAKITA_FC_OK);

    switch (data.failureCode) {
        case MAKITA_FC_OK:         strncpy(data.failureText, "OK (0)", sizeof(data.failureText)); break;
        case MAKITA_FC_OVERLOADED: strncpy(data.failureText, "Overloaded (1)", sizeof(data.failureText)); break;
        case MAKITA_FC_WARNING:    strncpy(data.failureText, "Warning (5)", sizeof(data.failureText)); break;
        case MAKITA_FC_DEAD:       strncpy(data.failureText, "Dead (15)", sizeof(data.failureText)); break;
        default: snprintf(data.failureText, sizeof(data.failureText), "Error (%u)", data.failureCode); break;
    }

    // Cycles
    data.cycles = ((uint16_t)(nybbleGet(d, 52) & 0x1) << 12)
                | ((uint16_t)nybbleGet(d, 53) << 8)
                | ((uint16_t)nybbleGet(d, 54) << 4)
                |  (uint16_t)nybbleGet(d, 55);

    // Read Voltages
    if (data.type == MakitaBatteryType::T5) {
        readVoltagesType5(data);
    } else if (data.type == MakitaBatteryType::T6) {
        readVoltagesType6(data);
    } else {
        readVoltagesType0(data);
    }

    // Cell diff & imbalance check
    float minV = 99.0f, maxV = 0.0f;
    for (uint8_t i = 0; i < data.cellCount && i < 10; i++) {
        if (data.cellV[i] > 0.5f) {
            if (data.cellV[i] < minV) minV = data.cellV[i];
            if (data.cellV[i] > maxV) maxV = data.cellV[i];
        }
    }
    if (minV < 90.0f && maxV > 0.0f) {
        data.cellDiff = maxV - minV;
        data.cellImbalance = (data.cellDiff >= 0.300f);
    } else {
        data.cellDiff = 0.0f;
        data.cellImbalance = false;
    }

    // Health Rating
    if (data.cycles > 0) {
        float deg = (float)data.cycles / 400.0f;
        data.healthRating = max(0.0f, 4.0f - deg);
    } else {
        data.healthRating = 4.0f;
    }

    if (data.healthRating >= 3.5f)      strcpy(data.healthBar, "####");
    else if (data.healthRating >= 2.5f) strcpy(data.healthBar, "###-");
    else if (data.healthRating >= 1.5f) strcpy(data.healthBar, "##--");
    else if (data.healthRating >= 0.5f) strcpy(data.healthBar, "#---");
    else                                strcpy(data.healthBar, "----");

    // Estimate SOC %
    if (data.packVoltage > 15.0f) {
        float p = (data.packVoltage - 15.0f) / (20.5f - 15.0f) * 100.0f;
        data.socPercent = constrain(p, 0.0f, 100.0f);
    }

    return true;
}

bool MakitaReader::resetErrorsDA04() {
    powerCycleBus();
    if (enterTestMode() != BUS_OK) return false;
    uint8_t r[1] = {0};
    cmdCC(CMD_RESET_ERRORS, sizeof(CMD_RESET_ERRORS), r, 1);
    delay(INTER_CMD_MS);
    exitTestMode();
    powerCycleBus();
    return true;
}

bool MakitaReader::repairFrame(MakitaData &data) {
    uint8_t frame[BASIC_INFO_LEN];
    memcpy(frame, data.rawFrame, BASIC_INFO_LEN);

    // Set nybble 34 = 0 (unlock byte)
    frame[17] = (frame[17] & 0xF0) | 0x00;

    // Recalculate CS0 and CS2
    nybbleSet(frame, 41, checksumCalc(frame, 0, 15));
    nybbleSet(frame, 43, checksumCalc(frame, 32, 40));

    if (enterTestMode() != BUS_OK) return false;

    // Write frame opcode
    uint8_t writeCmd[1 + 1 + BASIC_INFO_LEN];
    writeCmd[0] = FRAME_WRITE_OPCODE;
    writeCmd[1] = FRAME_WRITE_PAD;
    memcpy(writeCmd + 2, frame, BASIC_INFO_LEN);

    uint8_t r[1] = {0};
    cmdCCRaw(writeCmd, sizeof(writeCmd), r, 0);
    delay(INTER_CMD_MS);

    // Store command
    uint8_t dummy[8];
    cmd33WithRom(CMD_STORE, sizeof(CMD_STORE), dummy, nullptr, 0);
    delay(INTER_CMD_MS);

    exitTestMode();
    powerCycleBus();

    // Verify
    return waitForResponse();
}

bool MakitaReader::autoUnlock(MakitaData &data) {
    if (!data.locked) return true;

    // 1. Try DA04 error register reset
    resetErrorsDA04();
    if (readAllData(data) && !data.locked) return true;

    // 2. Try Frame Repair (up to 3 attempts)
    for (int i = 0; i < 3; i++) {
        if (repairFrame(data)) {
            readAllData(data);
            if (!data.locked) return true;
        }
        delay(100);
    }

    return !data.locked;
}

bool MakitaReader::setOmegaLock(MakitaData &data) {
    uint8_t frame[BASIC_INFO_LEN];
    memcpy(frame, data.rawFrame, BASIC_INFO_LEN);

    // Set nybble 34 = 1 (Omega lock byte)
    frame[17] = (frame[17] & 0xF0) | 0x01;

    // Recalculate CS0 and CS2
    nybbleSet(frame, 41, checksumCalc(frame, 0, 15));
    nybbleSet(frame, 43, checksumCalc(frame, 32, 40));

    if (enterTestMode() != BUS_OK) return false;

    uint8_t writeCmd[1 + 1 + BASIC_INFO_LEN];
    writeCmd[0] = FRAME_WRITE_OPCODE;
    writeCmd[1] = FRAME_WRITE_PAD;
    memcpy(writeCmd + 2, frame, BASIC_INFO_LEN);

    uint8_t r[1] = {0};
    cmdCCRaw(writeCmd, sizeof(writeCmd), r, 0);
    delay(INTER_CMD_MS);

    uint8_t dummy[8];
    cmd33WithRom(CMD_STORE, sizeof(CMD_STORE), dummy, nullptr, 0);
    delay(INTER_CMD_MS);

    exitTestMode();
    powerCycleBus();

    waitForResponse();
    readAllData(data);
    return data.locked;
}
