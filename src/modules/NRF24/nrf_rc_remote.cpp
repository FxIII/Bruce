#include "nrf_rc_remote.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/powerSave.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "modules/NRF24/nrf_common.h"
#include <globals.h>
#include <SD.h>
#include <vector>
#include <ctype.h>

struct RcRemoteCmd {
    char key;
    String name;
    std::vector<uint8_t> payload;
};

struct RcRemoteConfig {
    uint8_t addr[5];
    uint8_t addrWidth;
    std::vector<uint8_t> channels;
    rf24_datarate_e rate;
    String rateStr;
    bool loadedFromSd;
    std::vector<RcRemoteCmd> commands;
};

static RcRemoteConfig s_remCfg = {
    {0x44, 0x5A, 0x43, 0x97, 0x00}, // 44:5A:43:97
    4,
    {16, 20, 21, 22, 64, 65, 67, 70}, // 8 hopping channels by default
    RF24_1MBPS,
    "1M",
    false,
    {}
};

// Parse hex string into byte vector
static bool parseHexBytes(const String &str, std::vector<uint8_t> &outVec) {
    outVec.clear();
    String clean = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if (isxdigit(c)) clean += c;
    }
    if (clean.length() < 2 || (clean.length() % 2 != 0)) return false;

    for (size_t i = 0; i < clean.length(); i += 2) {
        String byteStr = clean.substring(i, i + 2);
        outVec.push_back((uint8_t)strtol(byteStr.c_str(), NULL, 16));
    }
    return true;
}

// Helper: parse hex address like "44:5A:43:97"
static bool parseAddress(const String &str, uint8_t *outBuf, uint8_t maxLen, uint8_t &outLen) {
    std::vector<uint8_t> v;
    if (!parseHexBytes(str, v)) return false;
    outLen = v.size();
    if (outLen > maxLen) outLen = maxLen;
    memset(outBuf, 0, maxLen);
    for (uint8_t i = 0; i < outLen; i++) outBuf[i] = v[i];
    return true;
}

static void loadOrCreateRemoteConfig() {
    s_remCfg.loadedFromSd = false;
    s_remCfg.commands.clear();

    const char *cfgPath = "/nrf_remote.conf";

    if (!setupSdCard()) {
        Serial.println("[RC-REMOTE] SD card not available, creating built-in defaults");
    } else {
        if (!SD.exists(cfgPath)) {
            File f = SD.open(cfgPath, FILE_WRITE);
            if (f) {
                f.println("# NRF24 RC Remote Controller Configuration");
                f.println("address=44:5A:43:97");
                f.println("addr_width=4");
                f.println("channels=16,20,21,22,64,65,67,70");
                f.println("datarate=1M");
                f.println();
                f.println("# Commands: key,name(max 8 chars),payload_hex");
                f.println("W,AVANTI,659A6520496B3F352D977F6C2B67");
                f.println("A,SX_FWD,659A6520496B3F352C977F6F23C7");
                f.println("D,DX_FWD,659A6520496B3F352F977F6E202A");
                f.println("Q,ROTAZ,659A6520496B3F352B977F6A224F");
                f.println("S,STOP,659A6520496B3F352E977F713D50");
                f.close();
                Serial.println("[RC-REMOTE] Created default /nrf_remote.conf on SD");
            }
        }

        File f = SD.open(cfgPath, FILE_READ);
        if (f) {
            bool hasCustomChannels = false;
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() == 0 || line.startsWith("#")) continue;

                int eq = line.indexOf('=');
                if (eq >= 0) {
                    String key = line.substring(0, eq);
                    String val = line.substring(eq + 1);
                    key.trim();
                    val.trim();

                    if (key.equalsIgnoreCase("address")) {
                        uint8_t len = 0;
                        if (parseAddress(val, s_remCfg.addr, 5, len)) {
                            if (len >= 3 && len <= 5) s_remCfg.addrWidth = len;
                        }
                    } else if (key.equalsIgnoreCase("addr_width")) {
                        int w = val.toInt();
                        if (w >= 3 && w <= 5) s_remCfg.addrWidth = w;
                    } else if (key.equalsIgnoreCase("channels")) {
                        if (!hasCustomChannels) {
                            s_remCfg.channels.clear();
                            hasCustomChannels = true;
                        }
                        int start = 0;
                        while (start < (int)val.length()) {
                            int comma = val.indexOf(',', start);
                            String chStr = (comma >= 0) ? val.substring(start, comma) : val.substring(start);
                            chStr.trim();
                            int chVal = chStr.toInt();
                            if (chVal >= 0 && chVal <= 125) {
                                s_remCfg.channels.push_back((uint8_t)chVal);
                            }
                            if (comma < 0) break;
                            start = comma + 1;
                        }
                    } else if (key.equalsIgnoreCase("channel")) {
                        int ch = val.toInt();
                        if (ch >= 0 && ch <= 125) {
                            s_remCfg.channels.clear();
                            s_remCfg.channels.push_back((uint8_t)ch);
                            hasCustomChannels = true;
                        }
                    } else if (key.equalsIgnoreCase("datarate")) {
                        if (val.equalsIgnoreCase("250K")) {
                            s_remCfg.rate = RF24_250KBPS;
                            s_remCfg.rateStr = "250K";
                        } else if (val.equalsIgnoreCase("2M")) {
                            s_remCfg.rate = RF24_2MBPS;
                            s_remCfg.rateStr = "2M";
                        } else {
                            s_remCfg.rate = RF24_1MBPS;
                            s_remCfg.rateStr = "1M";
                        }
                    }
                } else {
                    // Command line format: KEY,NAME,HEX_PAYLOAD
                    int c1 = line.indexOf(',');
                    if (c1 > 0) {
                        int c2 = line.indexOf(',', c1 + 1);
                        if (c2 > c1) {
                            String keyStr = line.substring(0, c1);
                            String nameStr = line.substring(c1 + 1, c2);
                            String hexStr = line.substring(c2 + 1);
                            keyStr.trim();
                            nameStr.trim();
                            hexStr.trim();

                            if (keyStr.length() > 0) {
                                RcRemoteCmd cmd;
                                cmd.key = toupper(keyStr[0]);
                                cmd.name = nameStr.substring(0, 8);
                                if (parseHexBytes(hexStr, cmd.payload) && cmd.payload.size() > 0) {
                                    s_remCfg.commands.push_back(cmd);
                                }
                            }
                        }
                    }
                }
            }
            f.close();
            s_remCfg.loadedFromSd = true;
            Serial.printf("[RC-REMOTE] Loaded /nrf_remote.conf with %u commands\n", s_remCfg.commands.size());
        }
    }

    // Default fallback commands if none loaded
    if (s_remCfg.commands.empty()) {
        RcRemoteCmd c1; c1.key = 'W'; c1.name = "AVANTI"; parseHexBytes("659A6520496B3F352D977F6C2B67", c1.payload); s_remCfg.commands.push_back(c1);
        RcRemoteCmd c2; c2.key = 'A'; c2.name = "SX_FWD"; parseHexBytes("659A6520496B3F352C977F6F23C7", c2.payload); s_remCfg.commands.push_back(c2);
        RcRemoteCmd c3; c3.key = 'D'; c3.name = "DX_FWD"; parseHexBytes("659A6520496B3F352F977F6E202A", c3.payload); s_remCfg.commands.push_back(c3);
        RcRemoteCmd c4; c4.key = 'Q'; c4.name = "ROTAZ";  parseHexBytes("659A6520496B3F352B977F6A224F", c4.payload); s_remCfg.commands.push_back(c4);
        RcRemoteCmd c5; c5.key = 'S'; c5.name = "STOP";   parseHexBytes("659A6520496B3F352E977F713D50", c5.payload); s_remCfg.commands.push_back(c5);
    }
}

// ── TX Hardware Tuning ───────────────────────────────────────────
static void tuneRemoteTx() {
    NRFradio.stopListening();
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC(); // CRC is precomputed and embedded in payload
    NRFradio.disableDynamicPayloads();
    NRFradio.setDataRate(s_remCfg.rate);
    NRFradio.setRetries(0, 0);
    if (!s_remCfg.channels.empty()) {
        NRFradio.setChannel(s_remCfg.channels[0]);
    }
    NRFradio.setAddressWidth(s_remCfg.addrWidth);
    NRFradio.openWritingPipe(s_remCfg.addr);
    NRFradio.flush_tx();
}

// ── UI Drawing ──────────────────────────────────────────────────
static void drawRemoteUI(char activeKey, uint32_t txCount) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextSize(FP);

    // Header Line
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    char hdr[48];
    if (s_remCfg.channels.size() > 1) {
        snprintf(hdr, sizeof(hdr), "RC Remote [HOP %dB %s]", (int)s_remCfg.channels.size(), s_remCfg.rateStr.c_str());
    } else {
        uint8_t singleCh = s_remCfg.channels.empty() ? 67 : s_remCfg.channels[0];
        snprintf(hdr, sizeof(hdr), "RC Remote [CH%02d %s]", singleCh, s_remCfg.rateStr.c_str());
    }
    tft.drawString(hdr, 2, 2);

    // TX Counter / Status on top right
    if (activeKey != 0) {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        char txStr[24];
        snprintf(txStr, sizeof(txStr), "[TX] Pkts:%lu", (unsigned long)txCount);
        tft.drawRightString(txStr, tftWidth - 2, 2, 1);
    } else {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        char txStr[24];
        snprintf(txStr, sizeof(txStr), "Pkts:%lu", (unsigned long)txCount);
        tft.drawRightString(txStr, tftWidth - 2, 2, 1);
    }

    int sepY = LH + 2;
    tft.drawFastHLine(0, sepY, tftWidth, TFT_DARKGREY);

    int footerY = tftHeight - LH - 1;

    // Subheader: Target Address
    int y = sepY + 3;
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    char addrStr[64] = "ADDR: ";
    int pos = strlen(addrStr);
    for (int i = 0; i < s_remCfg.addrWidth; i++) {
        pos += snprintf(addrStr + pos, sizeof(addrStr) - pos, "%02X%s",
                        s_remCfg.addr[i], (i < s_remCfg.addrWidth - 1) ? ":" : "");
    }
    snprintf(addrStr + pos, sizeof(addrStr) - pos, " (%dB)", s_remCfg.addrWidth);
    tft.drawString(addrStr, 2, y);
    y += LH + 3;

    // Draw Buttons Grid
    // Up to 9 buttons arranged in 3 columns x 3 rows (ideal for 3x3 RC matrix)
    int cols = 3;
    int btnMargin = 3;
    int startY = y;
    int availableH = footerY - startY - 2;
    int maxRows = 3;
    int btnH = (availableH - (maxRows - 1) * btnMargin) / maxRows;
    if (btnH < 16) btnH = 16;
    int btnW = (tftWidth - (cols + 1) * btnMargin) / cols;

    for (size_t i = 0; i < s_remCfg.commands.size() && i < 9; i++) {
        const RcRemoteCmd &cmd = s_remCfg.commands[i];
        int r = i / cols;
        int c = i % cols;
        int bx = btnMargin + c * (btnW + btnMargin);
        int by = startY + r * (btnH + btnMargin);

        bool isActive = (toupper(cmd.key) == toupper(activeKey));

        if (isActive) {
            // Active transmitting button: Green fill, Black text
            tft.fillRoundRect(bx, by, btnW, btnH, 3, TFT_GREEN);
            tft.drawRoundRect(bx, by, btnW, btnH, 3, TFT_WHITE);
            tft.setTextColor(TFT_BLACK, TFT_GREEN);
        } else {
            // Idle button: Dark background, light border, White text
            tft.fillRoundRect(bx, by, btnW, btnH, 3, 0x2104);
            tft.drawRoundRect(bx, by, btnW, btnH, 3, TFT_DARKGREY);
            tft.setTextColor(TFT_WHITE, 0x2104);
        }

        char label[24];
        snprintf(label, sizeof(label), "[%c] %s", cmd.key, cmd.name.c_str());
        int txX = bx + (btnW - strlen(label) * 6) / 2;
        if (txX < bx + 2) txX = bx + 2;
        int txY = by + (btnH - 8) / 2;
        tft.drawString(label, txX, txY);
    }

    // Footer
    tft.drawFastHLine(0, footerY - 1, tftWidth, TFT_DARKGREY);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawString("Press KEY to TOGGLE TX | [ESC] Exit", 2, footerY);
}

// ── Main Entry ──────────────────────────────────────────────────
void nrf_rc_remote() {
    NRF24_MODE mode = nrf_setMode();
    if (!CHECK_NRF_SPI(mode)) {
        displayError("NRF24 SPI mode required", true);
        return;
    }

    if (!nrf_start(NRF_MODE_SPI)) {
        displayError("NRF24 not found", true);
        return;
    }

    loadOrCreateRemoteConfig();
    tuneRemoteTx();

    char activeKey = 0;
    char prevActiveKey = 0;
    unsigned long nextTxMillis = 0;
    uint32_t txCount = 0;

    drawRemoteUI(activeKey, txCount);

    while (true) {
        if (check(EscPress)) {
            break;
        }

        previousMillis = millis();

        // Check Cardputer keyboard letter press
        char k = checkLetterShortcutPress();
        if (k != (char)-1 && k != 0) {
            char upperK = toupper(k);
            // Verify if key matches one of our commands
            for (const auto &cmd : s_remCfg.commands) {
                if (toupper(cmd.key) == upperK) {
                    if (activeKey == upperK) {
                        // Same key pressed again -> STOP transmission (Toggle OFF)
                        activeKey = 0;
                    } else {
                        // Different key pressed -> Switch to new command (Toggle ON)
                        activeKey = upperK;
                    }
                    break;
                }
            }
        }

        // Periodic continuous transmission when a key is toggled active (burst every 15ms)
        if (activeKey != 0) {
            if (millis() >= nextTxMillis) {
                for (const auto &cmd : s_remCfg.commands) {
                    if (toupper(cmd.key) == activeKey) {
                        // Transmit across all hopping channels!
                        for (uint8_t ch : s_remCfg.channels) {
                            NRFradio.setChannel(ch);
                            NRFradio.write(cmd.payload.data(), cmd.payload.size());
                            txCount++;
                        }
                        break;
                    }
                }
                nextTxMillis = millis() + 15;
            }
        }

        // Redraw only on state change
        if (activeKey != prevActiveKey) {
            drawRemoteUI(activeKey, txCount);
            prevActiveKey = activeKey;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    NRFradio.powerDown();
}
