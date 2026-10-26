#include "HikokiReader.h"
#include <ctype.h>

HikokiReader::HikokiReader(uint8_t inPin, uint8_t invOutPin, uint8_t outPin)
    : _inPin(inPin), _invOutPin(invOutPin), _outPin(outPin) {}

void HikokiReader::begin() {
    pinMode(_inPin, INPUT);
    pinMode(_outPin, OUTPUT);
    pinMode(_invOutPin, OUTPUT);

    digitalWrite(_outPin, LOW);
    digitalWrite(_invOutPin, HIGH);
}

void HikokiReader::sendWakeup() {
    static const uint8_t wakeSeq[8] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };
    writeBytes(wakeSeq, 8);
}

int HikokiReader::recvByte() {
    int data = 0;
    uint32_t waitIter = 1500; 

    while (digitalRead(_inPin) == HIGH) {
        esp_rom_delay_us(5);
        if (--waitIter == 0) return -1; 
    }

    portDISABLE_INTERRUPTS();
    esp_rom_delay_us(156); 

    for (uint8_t i = 1; i > 0; i <<= 1) {
        if (digitalRead(_inPin) == HIGH) data |= i;
        esp_rom_delay_us(104);
    }
    portENABLE_INTERRUPTS();
    return data;
}

void HikokiReader::sendByte(uint8_t b) {
    portDISABLE_INTERRUPTS();
    // Старт-бит
    digitalWrite(_outPin, HIGH);
    digitalWrite(_invOutPin, LOW);
    esp_rom_delay_us(104);

    for (uint8_t i = 1; i > 0; i <<= 1) {
        if (b & i) {
            digitalWrite(_outPin, LOW);
            digitalWrite(_invOutPin, HIGH);
        } else {
            digitalWrite(_outPin, HIGH);
            digitalWrite(_invOutPin, LOW);
        }
        esp_rom_delay_us(104);
    }

    // Стоп-бит
    digitalWrite(_outPin, LOW);
    digitalWrite(_invOutPin, HIGH);
    esp_rom_delay_us(104);
    portENABLE_INTERRUPTS();
}

void HikokiReader::writeBytes(const uint8_t *buf, int len) {
    for (int i = 0; i < len; i++) sendByte(buf[i]);
}

int HikokiReader::readBytesUntilTimeout(uint8_t *buf, int maxLen, uint32_t timeoutMs) {
    int bytesRead = 0;
    uint32_t startMs = millis();
    
    while (bytesRead < maxLen) {
        int b = recvByte();
        if (b >= 0) {
            buf[bytesRead++] = (uint8_t)b;
            startMs = millis(); 
        } else {
            if (millis() - startMs >= timeoutMs) {
                break; 
            }
            yield(); 
        }
    }
    return bytesRead;
}

void HikokiReader::readBytes(uint8_t *buf, int len) {
    readBytesUntilTimeout(buf, len, 8);
}

size_t HikokiReader::splitTo(uint8_t delim, uint8_t *src, size_t len, uint8_t **dest, size_t dest_count) {
    size_t prev_idx = 0;
    size_t str_count = 0;
    
    size_t actual_len = 0;
    while (actual_len < len && src[actual_len] != '\0') {
        actual_len++;
    }

    for (size_t i = 0; i <= actual_len && str_count < dest_count; i++) {
        if (src[i] == delim || src[i] == '\0' || i == actual_len) {
            size_t cur_len = i - prev_idx;
            dest[str_count] = src + prev_idx;
            while (cur_len > 0 && isspace(dest[str_count][0]) && dest[str_count] < &src[actual_len]) {
                dest[str_count]++;
                cur_len--;
            }
            str_count++;
            if (src[i] == '\0' || i == actual_len) return str_count;
            prev_idx = i + 1;
            src[i] = 0;
        }
    }
    return str_count;
}

int HikokiReader::doCommand(const char *cmd, uint16_t returnValues[], bool isShort) {
    uint8_t buffer[512];
    uint8_t *rpy[64];
    int maxReadLen = isShort ? 64 : 512;

    for (int retry = 0; retry < 3; retry++) {
        memset(returnValues, 0, 64 * sizeof(uint16_t));
        memset(buffer, 0, maxReadLen);
        
        writeBytes((const uint8_t*)cmd, strlen(cmd));
        delay(2);
        
        int actualLen = readBytesUntilTimeout(buffer, maxReadLen, 8);

        if (actualLen > 0) {
            int split_cnt = splitTo('\r', buffer, actualLen, rpy, 64);
            if (split_cnt > 1) {
                for (int i = 1; i < split_cnt; i++) {
                    returnValues[i - 1] = (uint16_t)atoi((char*)rpy[i]);
                }
                return max(split_cnt - 1, 0);
            }
        }
        delay(5);
    }
    return 0;
}

double HikokiReader::calculateCapacity(double voltage) {
    static const double voltages[7] = {14.5, 16.0, 16.935, 17.93, 18.705, 19.41, 20.25};
    static const double percentages[7] = {0.0, 10.0, 20.0, 40.0, 60.0, 80.0, 100.0};

    if (voltage <= voltages[0]) return 0.0;
    if (voltage >= voltages[6]) return 100.0;

    for (int i = 0; i < 6; i++) {
        if (voltage <= voltages[i + 1]) {
            double gradient = (voltages[i + 1] - voltages[i]) / (percentages[i + 1] - percentages[i]);
            double intercept = voltages[i] - gradient * percentages[i];
            return (voltage - intercept) / gradient;
        }
    }
    return 0.0;
}

bool HikokiReader::isBrokenBattery(uint16_t batteryCode, uint16_t errorFlags[]) {
    if (batteryCode == 0x2 || batteryCode == 0x3 || batteryCode == 0x9 || batteryCode == 0x11 || batteryCode == 0x12) {
        return (errorFlags[0] & 0x42 || errorFlags[2] & 0xE0 || errorFlags[3] & 0x80 || errorFlags[5] & 0x13 || errorFlags[6] & 0x3) > 0;
    }
    if (batteryCode == 0xA || batteryCode == 0xB || batteryCode == 0x17 || batteryCode == 0x14 || batteryCode == 0x13) {
        return (errorFlags[0] & 0x42 || errorFlags[2] & 0xC0 || errorFlags[5] & 0x33 || errorFlags[6] & 0x3) > 0;
    }
    if (batteryCode == 0xD || batteryCode == 0xE || batteryCode == 0xF || batteryCode == 0x10) {
        return (errorFlags[0] & 0x2 || errorFlags[3] & 0x80 || errorFlags[6] & 0x3) > 0;
    }
    return false;
}

bool HikokiReader::isBatteryPresent() {
    sendWakeup();
    uint16_t r1001[64];
    doCommand("\r\n", r1001, true);
    return (doCommand("R1001\r\n", r1001, true) > 0);
}

bool HikokiReader::readAllData(HikokiData &data) {
    memset(&data, 0, sizeof(HikokiData));
    sendWakeup();

    uint16_t r1001[64] = {0};
    uint16_t r1003[64] = {0};
    uint16_t r1004[64] = {0};
    uint16_t r1009[64] = {0};
    uint16_t r1010[64] = {0};

    doCommand("\r\n", r1001, true);

    if (doCommand("R1001\r\n", r1001) <= 0) {
        data.connected = false;
        return false;
    }
    data.connected = true;

    doCommand("R1004\r\n", r1004);
    doCommand("R1010\r\n", r1010);
    doCommand("R1009\r\n", r1009);
    doCommand("R1003\r\n", r1003);

    strncpy(data.model, getBatteryTypeName(r1001[3]), sizeof(data.model) - 1);
    snprintf(data.serial, sizeof(data.serial), "%02u%02u%02u%02u%02u%02u",
             r1001[9], r1001[10], r1001[11], r1001[12], r1001[13], r1001[14]);
    snprintf(data.mfgDate, sizeof(data.mfgDate), "%02u-%02u-%04u",
             r1001[11], r1001[10], r1001[9] + 2000);
    snprintf(data.wakeTime, sizeof(data.wakeTime), "%04u:%02u", r1010[4], r1010[5]);
    snprintf(data.work36VTime, sizeof(data.work36VTime), "%04u:%02u", r1010[7], r1010[8]);
    snprintf(data.work18VTime, sizeof(data.work18VTime), "%04u:%02u",
             r1010[10] + r1010[13], r1010[11] + r1010[14]);
    data.chargeCount = r1010[3];
    data.capacityPercent = (float)calculateCapacity((r1004[5] + r1004[11]) / 2000.0);

    data.lowerBankV = r1004[5] / 1000.0f;
    data.upperBankV = r1004[11] / 1000.0f;
    data.cellV[0] = r1004[6] / 1000.0f;
    data.cellV[1] = r1004[7] / 1000.0f;
    data.cellV[2] = r1004[8] / 1000.0f;
    data.cellV[3] = r1004[9] / 1000.0f;
    data.cellV[4] = r1004[10] / 1000.0f;

    data.unbalanceLowerV = r1009[27] / 1000.0f;
    data.unbalanceUpperV = r1009[28] / 1000.0f;
    data.tempLowerC = (r1004[12] & 1 ? -1.0f : 1.0f) * r1004[13];
    data.tempUpperC = (r1004[14] & 1 ? -1.0f : 1.0f) * r1004[15];

    data.errorCount = 0;
    for (int i = 0; i < 10; i++) {
        uint16_t code = r1003[i + 9];
        if (code > 0) {
            data.errors[data.errorCount].fatal = (code == 8 || code == 9 || code == 108 || code == 109);
            data.errors[data.errorCount].chargeNo = r1003[29 + i] + (i == 0 ? 0 : r1003[29]);
            strncpy(data.errors[data.errorCount].message, getErrorName(code), sizeof(data.errors[data.errorCount].message) - 1);
            data.errorCount++;
        }
    }

    data.toolCount = 0;
    for (int i = 0; i < 10; i++) {
        uint16_t code = r1010[i + 16];
        if (code > 0) {
            strncpy(data.tools[data.toolCount].name, getToolName(code), sizeof(data.tools[data.toolCount].name) - 1);
            data.tools[data.toolCount].count = r1010[i + 26];
            data.toolCount++;
        }
    }

    data.errorFlagSet = isBrokenBattery(r1001[3], &r1004[17]);
    data.fuseError = (r1004[25] > 0);
    data.prevOvercharge = false;
    for (int i = 0; i < data.errorCount; i++) {
        if (data.errors[i].fatal) data.prevOvercharge = true;
    }
    data.unbalancedChgCount = r1009[5];
    data.overtempChgCount = r1009[4];
    data.overcurrentChgCount = r1009[6];

    data.totalOverdischarge = r1009[7] + r1009[8] + r1009[9] + r1009[10];
    data.oc18vBrushless = r1009[12];
    data.oc18vBrushed = r1009[13];
    data.oc36v = r1009[14];
    data.ocHighLoad = r1009[15];
    data.dischOvertemp = r1009[16];

    return true;
}

// ИСПРАВЛЕНО: Теперь пошагово скармливаем байты с паузами, предотвращая переполнение буфера BMS батареи
bool HikokiReader::resetLockout() {
    uint8_t buffer[64];
    
    for (int i = 0; i < 3; i++) {
        sendWakeup();
        delay(15);
        
        // Очищаем входной поток
        writeBytes((const uint8_t*)"\r\n", 2);
        readBytesUntilTimeout(buffer, 64, 10);
        
        // Шаг 1: Отправляем базовую команду R1012 и ждем реакцию
        writeBytes((const uint8_t*)"R1012\r\n", 7);
        delay(15); 
        
        // Передаем параметры адреса регистра 050 СТРОГО раздельно с микропаузами
        writeBytes((const uint8_t*)"002\r\n", 5);
        delay(15);
        writeBytes((const uint8_t*)"050\r\n", 5);
        delay(15);
        writeBytes((const uint8_t*)"00050\r\n", 7);
        
        // Даем BMS накопить данные и записать Flash-память (45 мс)
readBytesUntilTimeout(buffer, 64, 45);
delay(40);
// Шаг 2: Повторяем процедуру для регистра 051
writeBytes((const uint8_t*)"R1012\r\n", 7);
delay(15);
writeBytes((const uint8_t*)"002\r\n", 5);
delay(15);
writeBytes((const uint8_t*)"051\r\n", 5);
delay(15);
writeBytes((const uint8_t*)"00051\r\n", 7);
readBytesUntilTimeout(buffer, 64, 45);
delay(80);
}
return true;
}
const char* HikokiReader::getBatteryTypeName(uint16_t typeCode) {
switch (typeCode) {
case 0x1: return "Unconnectable";
case 0x2: return "BSL36A18";
case 0x3: return "BSL36B18";
case 0x4: return "BSL36200(Adaptor)";
case 0x5: return "ET36A(AC/DC Adaptor)";
case 0x6: return "BSL1215";
case 0x7: return "BSL1240M";
case 0x8: return "BSL1225M";
case 0x9: return "BSL36C18";
case 0xA: return "BSL36A18B";
case 0xB: return "BSL36B18B";
case 0xC: return "ET36A(AC/DC Adaptor)";
case 0xD: return "BSL1820M";
case 0xE: return "BSL1830M";
case 0xF: return "BSL1840M";
case 0x10: return "BSL1850MA";
case 0x11: return "BSL36A18X";
case 0x12: return "BSL36B18X";
case 0x13: return "BSL36A18BX";
case 0x14: return "BSL36B18BX";
case 0x15: return "BSL1220M";
case 0x16: return "BSL3640MVT";
case 0x17: return "BSL3640MVBT";
case 0x18: return "BSL1240MT";
default: return "Unknown Hikoki";
}
}
const char* HikokiReader::getErrorName(uint16_t code) {
switch (code) {
case 0x01: return "Upper Side Over Charge";
case 0x02: return "Over Charge cell L1";
case 0x03: return "Over Charge cell L2";
case 0x04: return "Over Charge cell L3";
case 0x05: return "Over Charge cell L4";
case 0x06: return "Over Charge cell L5";
case 0x08: return "Fatal Over Charge 2";
case 0x09: return "Fatal Over Charge 2 (Alt)";
case 0x0B: return "Charge overtemp Upper";
case 0x0C: return "Charge overtemp Lower";
case 0x13: return "Over Charge Current";
case 0x15: return "Abnormal Cell Upper";
case 0x1C: return "Over Discharge Upper";
case 0x1D: return "Over Discharge L1";
case 0x1E: return "Over Discharge L2";
case 0x1F: return "Over Discharge L3";
case 0x20: return "Over Discharge L4";
case 0x21: return "Over Discharge L5";
case 0x43: return "Cell TH Disconnect Upper";
case 0x44: return "Cell TH Disconnect Lower";
case 0x48: return "Current Detect Error";
case 0x49: return "Cell Voltage Detect Err";
case 0x4E: return "Bank Voltage Unbalance L>U";
case 0x4F: return "Bank Voltage Unbalance L<U";
case 0x50: return "Critical Cell Unbalance";
case 0x55: return "Cell Unbalance";
case 0x59: return "Abnormally High Temp";
case 0x5A: return "Extreme Overdischarge";
case 0x6C: return "Fatal Over Charge 2";
case 0x90: return "Over Current (18V)";
case 0x93: return "Abnormal Current";
case 0xBD: return "Abnormal High Temp";
case 0xBE: return "Extreme Overdischarge";
case 0xCE: return "Protect Over Current";
case 0xD3: return "Protect Low Voltage";
case 0xD8: return "Protect Over Voltage";
case 0xDD: return "BMS Broken Detect 1";
case 0xDE: return "BMS Broken Detect 2";
default: return "Fault Event";
}
}
const char* HikokiReader::getToolName(uint16_t code) {
switch (code) {
case 0x2714: return "G3610DA Grinder";
case 0x2715: return "C3606DA Saw";
case 0x2717: return "WH36DA Impact";
case 0x2718: return "DS/DV36DA Drill";
case 0x2719: return "DH36DPA Hammer";
case 0x271C: return "WH36DB Impact";
case 0x271D: return "WR36DA Wrench";
case 0x271E: return "CR36DA Recip Saw";
case 0x272E: return "C3605DA Saw";
case 0x2736: return "VB3616DA Rebar Bender";
case 0x273D: return "WH36DC Impact";
case 0x2744: return "WR36DD Wrench";
case 0x2749: return "M3612DA Router";
case 0x2751: return "WR36DE Wrench";
case 0x2757: return "WH18DC Impact";
case 0x2767: return "WR36DC Wrench";
case 0x2768: return "G3610DC Grinder";
case 0x277F: return "WH36DD Impact";
default: return "HiKOKI Tool";
}
}
