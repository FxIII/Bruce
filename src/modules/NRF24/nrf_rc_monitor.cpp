#include "nrf_rc_monitor.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/powerSave.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <globals.h>
#include <SD.h>
#include <vector>
#include <algorithm>

// ── XN297 Standard Descramble Table ─────────────────────────────
static const uint8_t s_xn297Scramble[32] = {
    0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
    0x0d, 0xae, 0x8c, 0x65, 0x12, 0xc1, 0xe9, 0xc6,
    0xc9, 0x63, 0xf6, 0x0e, 0x37, 0x93, 0x9a, 0x9e,
    0xe7, 0x41, 0x24, 0xb3, 0x81, 0x62, 0x47, 0xd8
};

// Preambles for promiscuous hardware capture
static const uint8_t PREAMBLES[][2] = {
    {0x55, 0x55},
    {0xAA, 0xAA}
};

// ── Configuration Struct ────────────────────────────────────────
struct RcMonitorConfig {
    uint8_t addr[5];
    uint8_t addrWidth;
    std::vector<uint8_t> channels;
    rf24_datarate_e rate;
    String rateStr;
    uint8_t payloadSize;
    bool xn297Descramble;
    bool filterAddress; // true = filter by target address, false = accept any packet
    bool directAddress; // true = hardware direct address pipe, false = promiscuous preamble mode
    uint8_t crc;        // 0 = disabled, 1 = 8-bit, 2 = 16-bit
    bool autoAck;       // true/false (EN_AA)
    bool loadedFromSd;
};

// Default fallback configuration
static RcMonitorConfig s_cfg = {
    {0x88, 0xB4, 0x87, 0x2E, 0x00}, // Address (4 bytes: 88 B4 87 2E)
    4,                               // 4 bytes
    {16, 20, 21, 22, 64, 65, 67, 70}, // 8 detected hopping channels
    RF24_1MBPS,
    "1M",
    32,                              // 32-byte capture
    false,                           // RAW by default
    true,                            // filter by address
    false,                           // direct_address = 0 by default (promiscuous preamble)
    0,                               // crc = 0 (disabled by default)
    false,                           // auto_ack = false
    false
};

// Channel hit counters for 5-second sliding window
static uint16_t s_chanHits[126] = {0};
static unsigned long s_lastHitsReset = 0;

// Computed match addresses (direct and shifted)
static uint8_t s_matchDirect[5];
static uint8_t s_matchShifted[5];
static bool s_hasShifted = false;

// Compute bit-shifted variant
static void computeShiftedAddr(const uint8_t *in, uint8_t *out, uint8_t len) {
    uint8_t carry = 0;
    for (int i = 0; i < len; i++) {
        uint8_t nextCarry = (in[i] & 0x01) << 7;
        out[i] = (in[i] >> 1) | carry;
        carry = nextCarry;
    }
}

static void updateMatchAddresses() {
    memset(s_matchDirect, 0, sizeof(s_matchDirect));
    memset(s_matchShifted, 0, sizeof(s_matchShifted));
    memcpy(s_matchDirect, s_cfg.addr, s_cfg.addrWidth);
    computeShiftedAddr(s_cfg.addr, s_matchShifted, s_cfg.addrWidth);
    s_hasShifted = true;
}

// Helper: parse hex byte string like "88:B4:87:2E:CB" or "88B4872E"
static bool parseHexAddress(const String &str, uint8_t *outBuf, uint8_t maxLen, uint8_t &outLen) {
    String clean = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if (isxdigit(c)) clean += c;
    }
    if (clean.length() < 2 || (clean.length() % 2 != 0)) return false;

    outLen = clean.length() / 2;
    if (outLen > maxLen) outLen = maxLen;

    memset(outBuf, 0, maxLen);
    for (uint8_t i = 0; i < outLen; i++) {
        String byteStr = clean.substring(i * 2, i * 2 + 2);
        outBuf[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
    }
    return true;
}

// ── SD Config Reader & Writer ───────────────────────────────────
static void loadOrCreateConfigFile() {
    s_cfg.loadedFromSd = false;

    if (!setupSdCard()) {
        Serial.println("[RC-MON] SD card not available, using built-in defaults");
        updateMatchAddresses();
        return;
    }

    const char *cfgPath = "/nrf_rc.conf";

    // If config does not exist, create it as a template with current settings
    if (!SD.exists(cfgPath)) {
        File f = SD.open(cfgPath, FILE_WRITE);
        if (f) {
            f.println("# NRF24 RC Monitor Configuration");
            f.println("address=88:B4:87:2E");
            f.println("addr_width=4");
            f.println("channels=16,20,21,22,64,65,67,70");
            f.println("datarate=1M");
            f.println("payload_size=32");
            f.println("xn297_scramble=0");
            f.println("# direct_address: 0 = promiscuous preamble sniff, 1 = hardware direct pipe");
            f.println("direct_address=0");
            f.println("# crc: 0 = disabled, 1 = 8-bit, 2 = 16-bit");
            f.println("crc=0");
            f.println("# auto_ack: 0 = disabled, 1 = enabled");
            f.println("auto_ack=0");
            f.close();
            Serial.println("[RC-MON] Created default /nrf_rc.conf on SD");
        }
    }

    File f = SD.open(cfgPath, FILE_READ);
    if (!f) {
        Serial.println("[RC-MON] Could not open /nrf_rc.conf");
        updateMatchAddresses();
        return;
    }

    s_cfg.channels.clear();

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        int eq = line.indexOf('=');
        if (eq < 0) continue;

        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim();
        val.trim();

        if (key.equalsIgnoreCase("address")) {
            uint8_t len = 0;
            if (parseHexAddress(val, s_cfg.addr, 5, len)) {
                if (len >= 3 && len <= 5) s_cfg.addrWidth = len;
            }
        } else if (key.equalsIgnoreCase("addr_width")) {
            uint8_t w = val.toInt();
            if (w >= 3 && w <= 5) s_cfg.addrWidth = w;
        } else if (key.equalsIgnoreCase("channels")) {
            int start = 0;
            while (start < (int)val.length()) {
                int comma = val.indexOf(',', start);
                String chStr = (comma >= 0) ? val.substring(start, comma) : val.substring(start);
                chStr.trim();
                int chVal = chStr.toInt();
                if (chVal >= 0 && chVal <= 125) {
                    s_cfg.channels.push_back((uint8_t)chVal);
                }
                if (comma < 0) break;
                start = comma + 1;
            }
        } else if (key.equalsIgnoreCase("datarate")) {
            if (val.equalsIgnoreCase("250K")) {
                s_cfg.rate = RF24_250KBPS;
                s_cfg.rateStr = "250K";
            } else if (val.equalsIgnoreCase("2M")) {
                s_cfg.rate = RF24_2MBPS;
                s_cfg.rateStr = "2M";
            } else {
                s_cfg.rate = RF24_1MBPS;
                s_cfg.rateStr = "1M";
            }
        } else if (key.equalsIgnoreCase("payload_size")) {
            int ps = val.toInt();
            if (ps >= 1 && ps <= 32) s_cfg.payloadSize = ps;
        } else if (key.equalsIgnoreCase("xn297_scramble")) {
            s_cfg.xn297Descramble = (val.toInt() != 0);
        } else if (key.equalsIgnoreCase("direct_address")) {
            s_cfg.directAddress = (val.toInt() != 0);
        } else if (key.equalsIgnoreCase("crc")) {
            int c = val.toInt();
            if (c >= 0 && c <= 2) s_cfg.crc = (uint8_t)c;
        } else if (key.equalsIgnoreCase("auto_ack")) {
            s_cfg.autoAck = (val.toInt() != 0);
        }
    }
    f.close();

    if (s_cfg.channels.empty()) {
        s_cfg.channels = {16, 20, 21, 22, 64, 65, 67, 70};
    }

    updateMatchAddresses();
    s_cfg.loadedFromSd = true;
    Serial.println("[RC-MON] Loaded /nrf_rc.conf successfully from SD");
}

enum PreambleChoice {
    PREAMBLE_CHOICE_55 = 0,
    PREAMBLE_CHOICE_AA = 1,
    PREAMBLE_CHOICE_BOTH = 2
};
static PreambleChoice s_preambleChoice = PREAMBLE_CHOICE_BOTH;

// ── Sniff / Direct Tuning Helper ────────────────────────────────
static void tuneSniffer(uint8_t ch, int preambleIdx) {
    NRFradio.stopListening();

    // Configure Auto-ACK
    NRFradio.setAutoAck(s_cfg.autoAck);

    // Configure CRC
    if (s_cfg.crc == 1) {
        NRFradio.setCRCLength(RF24_CRC_8);
    } else if (s_cfg.crc == 2) {
        NRFradio.setCRCLength(RF24_CRC_16);
    } else {
        NRFradio.disableCRC();
    }

    // Disable dynamic payloads to avoid Enhanced ShockBurst PCF requirement
    NRFradio.disableDynamicPayloads();

    NRFradio.setDataRate(s_cfg.rate);
    NRFradio.setRetries(0, 0);
    NRFradio.setChannel(ch);

    if (s_cfg.directAddress) {
        // Direct Hardware Address Filtering Mode
        NRFradio.setAddressWidth(s_cfg.addrWidth);
        NRFradio.setPayloadSize(s_cfg.payloadSize);
        // Open Pipe 0 with configured direct address
        NRFradio.openReadingPipe(0, s_matchDirect);
        // If shifted variant exists, open Pipe 1 with shifted address!
        if (s_hasShifted) {
            NRFradio.openReadingPipe(1, s_matchShifted);
        } else {
            NRFradio.closeReadingPipe(1);
        }
        for (uint8_t i = 2; i < 6; i++) {
            NRFradio.closeReadingPipe(i);
        }
    } else {
        // Promiscuous Preamble Sniff Mode (2-byte preamble trick)
        NRFradio.setAddressWidth(2);
        NRFradio.setPayloadSize(32);

        if (s_preambleChoice == PREAMBLE_CHOICE_55) {
            for (uint8_t i = 0; i < 6; i++) {
                NRFradio.openReadingPipe(i, PREAMBLES[0]);
            }
        } else if (s_preambleChoice == PREAMBLE_CHOICE_AA) {
            for (uint8_t i = 0; i < 6; i++) {
                NRFradio.openReadingPipe(i, PREAMBLES[1]);
            }
        } else {
            // BOTH: Pipe 0 triggers on 0x55, Pipe 1 triggers on 0xAA simultaneously!
            NRFradio.openReadingPipe(0, PREAMBLES[0]);
            NRFradio.openReadingPipe(1, PREAMBLES[1]);
            for (uint8_t i = 2; i < 6; i++) {
                NRFradio.closeReadingPipe(i);
            }
        }
    }

    NRFradio.flush_rx();
    NRFradio.flush_tx();
    NRFradio.startListening();
}

// ── CSV Recording Helpers ───────────────────────────────────────
static File s_csvFile;
static bool s_isRecording = false;

static void startRecording() {
    if (!setupSdCard()) {
        Serial.println("[RC-MON] SD card not mounted, cannot record");
        return;
    }
    const char *csvPath = "/nrf_rc_capture.csv";
    bool exists = SD.exists(csvPath);
    s_csvFile = SD.open(csvPath, FILE_APPEND);
    if (s_csvFile) {
        if (!exists || s_csvFile.size() == 0) {
            s_csvFile.println("timestamp_ms,channel,preamble,raw_hex");
            s_csvFile.flush();
        }
        s_isRecording = true;
        Serial.println("[RC-MON] Recording started: /nrf_rc_capture.csv");
    } else {
        Serial.println("[RC-MON] Failed to open CSV file for append");
    }
}

static void stopRecording() {
    if (s_isRecording && s_csvFile) {
        s_csvFile.flush();
        s_csvFile.close();
        Serial.println("[RC-MON] Recording stopped");
    }
    s_isRecording = false;
}

static void deleteCsvFile() {
    stopRecording();
    if (setupSdCard()) {
        const char *csvPath = "/nrf_rc_capture.csv";
        if (SD.exists(csvPath)) {
            SD.remove(csvPath);
            Serial.println("[RC-MON] Deleted /nrf_rc_capture.csv");
        }
    }
}

static void logPacketToCsv(uint8_t ch, uint8_t preamble, const uint8_t *rawBuf, uint8_t rawLen) {
    if (!s_isRecording || !s_csvFile) return;

    // timestamp_ms,channel,preamble,raw_hex
    s_csvFile.printf("%lu,%u,%02X,", (unsigned long)millis(), ch, preamble);
    for (uint8_t i = 0; i < rawLen; i++) {
        s_csvFile.printf("%02X", rawBuf[i]);
    }
    s_csvFile.println();
    s_csvFile.flush();
}

// ── Draw Screen UI ──────────────────────────────────────────────
static void drawMonitorUI(
    bool isPaused,
    bool isRecording,
    bool hopMode,
    uint8_t currentCh,
    int singleChIdx,
    uint32_t totalPackets,
    const uint8_t *curPayload,
    const uint8_t *prevPayload,
    uint8_t curLen,
    uint8_t prevLen,
    const uint8_t *lastMatchedAddr,
    uint8_t matchedAddrLen,
    int preambleIdx,
    const char *statusMsg = nullptr
) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextSize(FP);

    // Header Line
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "RC Mon [%s] %s", s_cfg.rateStr.c_str(), s_cfg.loadedFromSd ? "SD:OK" : "SD:DEF");
    tft.drawString(hdr, 2, 2);

    if (isRecording) {
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        char pStr[24];
        snprintf(pStr, sizeof(pStr), "[REC] Pkts:%lu", (unsigned long)totalPackets);
        tft.drawRightString(pStr, tftWidth - 2, 2, 1);
    } else if (isPaused) {
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        tft.drawRightString("[PAUSED]", tftWidth - 2, 2, 1);
    } else {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        char pStr[24];
        snprintf(pStr, sizeof(pStr), "Pkts:%lu", (unsigned long)totalPackets);
        tft.drawRightString(pStr, tftWidth - 2, 2, 1);
    }

    int sepY = LH + 2;
    tft.drawFastHLine(0, sepY, tftWidth, TFT_DARKGREY);

    int footerY = tftHeight - LH - 1;

    // Right Column: Channel : Hits (5 chars, e.g. 16:23)
    int rightColW = 34; // fits 5 chars (30px) + 4px margin
    int rightColX = tftWidth - rightColW;
    int sepLineX = rightColX - 3;
    tft.drawFastVLine(sepLineX, sepY, footerY - sepY, TFT_DARKGREY);

    int colY = sepY + 2;
    int colLineH = LH - 1;
    if (colLineH < 8) colLineH = 8;
    for (size_t i = 0; i < s_cfg.channels.size() && (colY + colLineH <= footerY); i++) {
        uint8_t ch = s_cfg.channels[i];
        uint16_t hits = s_chanHits[ch];
        char chBuf[12];
        snprintf(chBuf, sizeof(chBuf), "%02d:%-2d", ch, hits > 99 ? 99 : hits);

        if (!hopMode && ch == s_cfg.channels[singleChIdx]) {
            tft.setTextColor(TFT_GREEN, bruceConfig.bgColor); // Canale selezionato/bloccato in VERDE
        } else if (hits > 0) {
            tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        } else {
            tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        }
        tft.drawString(chBuf, rightColX, colY);
        colY += colLineH;
    }

    // Left Area: Subheader: Filter Target Address
    int y = sepY + 3;
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    char addrStr[64] = "FLT: ";
    int pos = strlen(addrStr);
    for (int i = 0; i < s_cfg.addrWidth; i++) {
        pos += snprintf(addrStr + pos, sizeof(addrStr) - pos, "%02X%s", s_cfg.addr[i], (i < s_cfg.addrWidth - 1) ? ":" : "");
    }
    snprintf(addrStr + pos, sizeof(addrStr) - pos, " (%dB)", s_cfg.addrWidth);
    tft.drawString(addrStr, 2, y);
    y += LH;

    // Mode & Channel Row
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    char modeStr[48];
    const char *pStr = s_cfg.directAddress ? "DIR" : ((s_preambleChoice == PREAMBLE_CHOICE_55) ? "55" : ((s_preambleChoice == PREAMBLE_CHOICE_AA) ? "AA" : "BTH"));
    if (hopMode) {
        snprintf(modeStr, sizeof(modeStr), "HOP (CH%02d %s)", currentCh, s_cfg.directAddress ? "DIR" : pStr);
    } else {
        snprintf(modeStr, sizeof(modeStr), "LOCK CH%02d %s [%d/%d]",
                 s_cfg.channels[singleChIdx], s_cfg.directAddress ? "DIR" : pStr, singleChIdx + 1, (int)s_cfg.channels.size());
    }
    tft.drawString(modeStr, 2, y);

    // Tags on the right of the left area
    char tagStr[24];
    snprintf(tagStr, sizeof(tagStr), "%s %s", s_cfg.directAddress ? "DIR" : (s_cfg.filterAddress ? "M" : "A"), s_cfg.xn297Descramble ? "XN" : "RW");
    tft.setTextColor(s_cfg.xn297Descramble ? TFT_ORANGE : TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawRightString(tagStr, sepLineX - 3, y, 1);
    y += LH + 2;

    tft.drawFastHLine(0, y - 1, sepLineX, TFT_DARKGREY);

    // Payload & Last Match
    if (totalPackets == 0) {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawString("Listening on channels...", 4, y + 4);
        tft.drawString("Move sticks to trigger", 4, y + 4 + LH);
    } else {
        const int bytesPerRow = 8;

        // 1) Render [NEW] Payload
        tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
        tft.drawString("NEW:", 2, y);
        y += LH;

        int newRows = (curLen + bytesPerRow - 1) / bytesPerRow;
        if (newRows > 2) newRows = 2; // up to 16 bytes visible per slot
        for (int r = 0; r < newRows && (y + LH <= footerY); r++) {
            int x = 2;
            char offBuf[12];
            snprintf(offBuf, sizeof(offBuf), "+%02X: ", r * bytesPerRow);
            tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
            x += tft.drawString(offBuf, x, y);

            for (int b = 0; b < bytesPerRow; b++) {
                int byteIdx = r * bytesPerRow + b;
                if (byteIdx >= curLen) break;

                uint8_t val = curPayload[byteIdx];
                if (s_cfg.xn297Descramble) val ^= s_xn297Scramble[byteIdx % 32];

                // Check difference against prevPayload
                bool changed = false;
                if (prevLen > 0 && byteIdx < prevLen) {
                    uint8_t pVal = prevPayload[byteIdx];
                    if (s_cfg.xn297Descramble) pVal ^= s_xn297Scramble[byteIdx % 32];
                    changed = (val != pVal);
                }

                // Highlight changed bytes in YELLOW!
                tft.setTextColor(changed ? TFT_YELLOW : TFT_WHITE, changed ? TFT_DARKCYAN : bruceConfig.bgColor);
                char bBuf[8];
                snprintf(bBuf, sizeof(bBuf), "%02X ", val);
                x += tft.drawString(bBuf, x, y);
            }
            y += LH;
        }

        // 2) Render [OLD] Payload (if available)
        if (prevLen > 0 && (y + LH * 2 <= footerY)) {
            tft.drawFastHLine(2, y, sepLineX - 4, 0x39E7); // faint separator line
            y += 2;
            tft.setTextColor(0x7BEF, bruceConfig.bgColor); // dim grey/blue
            tft.drawString("OLD:", 2, y);
            y += LH;

            int oldRows = (prevLen + bytesPerRow - 1) / bytesPerRow;
            if (oldRows > 2) oldRows = 2;
            for (int r = 0; r < oldRows && (y + LH <= footerY); r++) {
                int x = 2;
                char offBuf[12];
                snprintf(offBuf, sizeof(offBuf), "+%02X: ", r * bytesPerRow);
                tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
                x += tft.drawString(offBuf, x, y);

                for (int b = 0; b < bytesPerRow; b++) {
                    int byteIdx = r * bytesPerRow + b;
                    if (byteIdx >= prevLen) break;

                    uint8_t val = prevPayload[byteIdx];
                    if (s_cfg.xn297Descramble) val ^= s_xn297Scramble[byteIdx % 32];

                    tft.setTextColor(TFT_LIGHTGREY, bruceConfig.bgColor);
                    char bBuf[8];
                    snprintf(bBuf, sizeof(bBuf), "%02X ", val);
                    x += tft.drawString(bBuf, x, y);
                }
                y += LH;
            }
        }
    }

    // Footer
    tft.drawFastHLine(0, footerY - 1, tftWidth, TFT_DARKGREY);
    if (statusMsg) {
        tft.setTextColor(TFT_ORANGE, bruceConfig.bgColor);
        tft.drawString(statusMsg, 2, footerY);
    } else {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawString(isPaused ? "[Space]RESUME  [U/D]Ch  [ESC]" : (isRecording ? "[R]STOP [Space]PAUSE [ESC]" : "[R]REC [D]EL [B]Pre [ESC]"), 2, footerY);
    }
}

// ── Main Entry ──────────────────────────────────────────────────
void nrf_rc_monitor() {
    NRF24_MODE mode = nrf_setMode();
    if (!CHECK_NRF_SPI(mode)) {
        displayError("NRF24 SPI mode required", true);
        return;
    }

    if (!nrf_start(NRF_MODE_SPI)) {
        displayError("NRF24 not found", true);
        return;
    }

    // Load or create SD config
    loadOrCreateConfigFile();

    uint8_t curPayload[32] = {0};
    uint8_t prevPayload[32] = {0};
    uint8_t lastMatchedAddr[5] = {0};
    uint8_t curLen = 0;
    uint8_t prevLen = 0;

    uint32_t totalPackets = 0;
    unsigned long lastDraw = 0;

    memset(s_chanHits, 0, sizeof(s_chanHits));
    s_lastHitsReset = millis();

    bool isPaused = false;
    bool hopMode = true;
    int hopIdx = 0;
    int singleChIdx = 0;
    int preambleIdx = 0;
    unsigned long dwellStart = millis();

    const char *statusMsg = nullptr;
    unsigned long statusMsgUntil = 0;

    uint8_t currentCh = s_cfg.channels[0];
    tuneSniffer(currentCh, preambleIdx);

    bool needRedraw = true;

    while (true) {
        if (check(EscPress)) {
            break;
        }

        // Keep screen awake
        previousMillis = millis();

        // Expire transient status message
        if (statusMsg && millis() > statusMsgUntil) {
            statusMsg = nullptr;
            needRedraw = true;
        }

        char k = checkLetterShortcutPress();

        // Key Handling
        if (check(SelPress) || k == ' ' || k == 'p' || k == 'P') {
            isPaused = !isPaused;
            if (!isPaused) {
                tuneSniffer(currentCh, preambleIdx);
                dwellStart = millis();
            }
            needRedraw = true;
        } else if (k == 'r' || k == 'R') {
            // Toggle CSV Recording
            if (s_isRecording) {
                stopRecording();
                statusMsg = "[REC STOPPED]";
            } else {
                startRecording();
                statusMsg = s_isRecording ? "[REC STARTED]" : "[REC FAILED: NO SD]";
            }
            statusMsgUntil = millis() + 1500;
            needRedraw = true;
        } else if (k == 'd' || k == 'D') {
            // Delete CSV File
            deleteCsvFile();
            statusMsg = "[CSV DELETED]";
            statusMsgUntil = millis() + 1500;
            needRedraw = true;
        } else if (k == 'b' || k == 'B') {
            // Cycle preamble: 55 -> AA -> BOTH -> 55
            s_preambleChoice = (PreambleChoice)((s_preambleChoice + 1) % 3);
            tuneSniffer(currentCh, preambleIdx);
            needRedraw = true;
        } else if (k == 'x' || k == 'X') {
            s_cfg.xn297Descramble = !s_cfg.xn297Descramble;
            needRedraw = true;
        } else if (k == 'a' || k == 'A') {
            // Toggle between address filter and promiscuous ANY
            s_cfg.filterAddress = !s_cfg.filterAddress;
            needRedraw = true;
        } else if (check(UpPress) || check(NextPress) || k == 'm' || k == 'M') {
            if (hopMode) {
                hopMode = false;
                singleChIdx = 0;
            } else {
                singleChIdx++;
                if (singleChIdx >= (int)s_cfg.channels.size()) {
                    hopMode = true;
                    singleChIdx = 0;
                }
            }
            currentCh = hopMode ? s_cfg.channels[hopIdx] : s_cfg.channels[singleChIdx];
            tuneSniffer(currentCh, preambleIdx);
            dwellStart = millis();
            needRedraw = true;
        } else if (check(DownPress) || check(PrevPress)) {
            if (hopMode) {
                hopMode = false;
                singleChIdx = (int)s_cfg.channels.size() - 1;
            } else {
                if (singleChIdx > 0) {
                    singleChIdx--;
                } else {
                    hopMode = true;
                }
            }
            currentCh = hopMode ? s_cfg.channels[hopIdx] : s_cfg.channels[singleChIdx];
            tuneSniffer(currentCh, preambleIdx);
            dwellStart = millis();
            needRedraw = true;
        } else if (k == 'c' || k == 'C') {
            totalPackets = 0;
            curLen = 0;
            prevLen = 0;
            memset(curPayload, 0, sizeof(curPayload));
            memset(prevPayload, 0, sizeof(prevPayload));
            memset(lastMatchedAddr, 0, sizeof(lastMatchedAddr));
            memset(s_chanHits, 0, sizeof(s_chanHits));
            s_lastHitsReset = millis();
            needRedraw = true;
        }

        // Radio Packet Processing (when not paused)
        if (!isPaused) {
            while (NRFradio.available()) {
                uint8_t rxBuf[32];
                NRFradio.read(rxBuf, 32);

                // Reject blank or noise-only packets
                bool isNoise = true;
                for (int i = 0; i < 5; i++) {
                    if (rxBuf[i] != 0x00 && rxBuf[i] != 0xFF && rxBuf[i] != 0x55 && rxBuf[i] != 0xAA) {
                        isNoise = false;
                        break;
                    }
                }
                if (isNoise) continue;

                const uint8_t *inPayload = nullptr;
                uint8_t inPlSize = 0;

                if (s_cfg.directAddress) {
                    // Direct Hardware Address Mode: nRF hardware already filtered by address!
                    uint8_t pipeNum = 0;
                    NRFradio.available(&pipeNum);
                    if (pipeNum == 1 && s_hasShifted) {
                        memcpy(lastMatchedAddr, s_matchShifted, s_cfg.addrWidth);
                    } else {
                        memcpy(lastMatchedAddr, s_matchDirect, s_cfg.addrWidth);
                    }

                    inPlSize = s_cfg.payloadSize;
                    if (inPlSize > 24) inPlSize = 24;
                    inPayload = rxBuf;

                    totalPackets++;
                    s_chanHits[currentCh]++;

                    logPacketToCsv(currentCh, 0x00, rxBuf, s_cfg.payloadSize);
                } else {
                    // Promiscuous Sniff Mode: Match against configured address or shifted variant
                    bool matched = false;
                    if (s_cfg.filterAddress) {
                        if (memcmp(rxBuf, s_matchDirect, s_cfg.addrWidth) == 0) {
                            matched = true;
                            memcpy(lastMatchedAddr, s_matchDirect, s_cfg.addrWidth);
                        } else if (s_hasShifted && memcmp(rxBuf, s_matchShifted, s_cfg.addrWidth) == 0) {
                            matched = true;
                            memcpy(lastMatchedAddr, s_matchShifted, s_cfg.addrWidth);
                        }
                    } else {
                        matched = true;
                        memcpy(lastMatchedAddr, rxBuf, s_cfg.addrWidth);
                    }

                    if (matched) {
                        inPlSize = 32 - s_cfg.addrWidth;
                        if (inPlSize > 24) inPlSize = 24;
                        inPayload = rxBuf + s_cfg.addrWidth;

                        totalPackets++;
                        s_chanHits[currentCh]++;

                        logPacketToCsv(currentCh, PREAMBLES[preambleIdx][0], rxBuf, 32);
                    }
                }

                if (inPayload != nullptr) {
                    // Check if new incoming payload is identical to either curPayload or prevPayload
                    bool sameAsCur = (curLen == inPlSize && memcmp(curPayload, inPayload, inPlSize) == 0);
                    bool sameAsPrev = (prevLen == inPlSize && memcmp(prevPayload, inPayload, inPlSize) == 0);

                    if (!sameAsCur && !sameAsPrev) {
                        // Unique new packet! Shift cur -> prev, and put new packet into cur
                        if (curLen > 0) {
                            memcpy(prevPayload, curPayload, curLen);
                            prevLen = curLen;
                        }
                        memcpy(curPayload, inPayload, inPlSize);
                        curLen = inPlSize;
                        needRedraw = true;
                    }

                    Serial.printf("[RC-PKT CH%02d] #%lu ADDR:", currentCh, (unsigned long)totalPackets);
                    for (int i = 0; i < s_cfg.addrWidth; i++) Serial.printf("%02X ", lastMatchedAddr[i]);
                    Serial.print(" | DATA: ");
                    for (int i = 0; i < inPlSize; i++) Serial.printf("%02X ", inPayload[i]);
                    Serial.println();
                }
            }

            // Reset channel hits every 5 seconds
            if (millis() - s_lastHitsReset >= 5000) {
                memset(s_chanHits, 0, sizeof(s_chanHits));
                s_lastHitsReset = millis();
                needRedraw = true;
            }

            // Hopping logic (dwell 25ms per channel when in AUTO-HOP)
            if (hopMode && (millis() - dwellStart >= 25)) {
                hopIdx = (hopIdx + 1) % s_cfg.channels.size();
                currentCh = s_cfg.channels[hopIdx];
                if (hopIdx == 0) {
                    preambleIdx = (preambleIdx + 1) % 2;
                }
                tuneSniffer(currentCh, preambleIdx);
                dwellStart = millis();
            }
        }

        // Screen refresh: ONLY redraw when something changed, throttled to 80ms
        if (needRedraw && (millis() - lastDraw > 80)) {
            drawMonitorUI(isPaused, s_isRecording, hopMode, currentCh, singleChIdx, totalPackets,
                          curPayload, prevPayload, curLen, prevLen, lastMatchedAddr,
                          s_cfg.addrWidth, preambleIdx, statusMsg);
            lastDraw = millis();
            needRedraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    stopRecording();
    NRFradio.stopListening();
    NRFradio.powerDown();
}
