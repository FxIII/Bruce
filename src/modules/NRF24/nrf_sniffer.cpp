#include "nrf_sniffer.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/powerSave.h"
#include "core/utils.h"
#include <globals.h>
#include <algorithm>

// ── State Machine ───────────────────────────────────────────────
enum HunterPhase {
    PHASE_SPECTRUM_PROFILER = 0, // Profile RF activity & detect door closure + active hopping channels
    PHASE_TARGETED_SNIFF    = 1  // Lock onto isolated hopping channels and capture address/payload
};

// ── Profiler Metrics per channel (0..125) ───────────────────────
struct ChannelStats {
    uint16_t hits;               // Total times carrier detected
    uint16_t consecutiveMisses;  // Consecutive sweeps with zero carrier since last hit
    uint32_t lastHitTime;
};

#define TOTAL_CHANNELS 126
static ChannelStats s_chanStats[TOTAL_CHANNELS];

// Locked hopping channels (up to 8 channels)
#define MAX_LOCKED_CH 8
static uint8_t s_lockedChannels[MAX_LOCKED_CH];
static uint8_t s_numLocked = 0;

// Captured candidates in Deep Sniff phase
struct CandidateAddr {
    uint8_t channel;
    uint8_t preamble;
    uint8_t addr[5];
    uint16_t hits;
    uint32_t lastSeen;
};

#define MAX_CANDIDATES 12
static CandidateAddr s_candidates[MAX_CANDIDATES];
static uint8_t s_candCount = 0;

static const rf24_datarate_e RATES[] = { RF24_1MBPS, RF24_250KBPS, RF24_2MBPS };
static const char* RATE_STRS[] = { "1M", "250K", "2M" };
static uint8_t s_rateIdx = 0;

static bool s_keepAwake = true;
static HunterPhase s_phase = PHASE_SPECTRUM_PROFILER;

// Preambles for address capture
static const uint8_t PREAMBLES[][2] = {
    {0x55, 0x55},
    {0xAA, 0xAA}
};

// ── Helper: Configure Radio for Deep Sniffing ───────────────────
static void setupSniffChannel(int channel, int preambleIdx) {
    NRFradio.stopListening();
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setAddressWidth(2); // 2-byte preamble trigger
    NRFradio.setDataRate(RATES[s_rateIdx]);
    NRFradio.setPayloadSize(32);
    NRFradio.setRetries(0, 0);
    NRFradio.setChannel(channel);

    for (uint8_t i = 0; i < 6; i++) {
        NRFradio.openReadingPipe(i, PREAMBLES[preambleIdx]);
    }

    NRFradio.flush_rx();
    NRFradio.flush_tx();
    NRFradio.startListening();
}

// ── Draw UI for Phase 1: Profiler ───────────────────────────────
static void drawProfilerScreen(uint32_t totalSweeps, uint8_t activeCount) {
    tft.fillScreen(bruceConfig.bgColor);

    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "Profiler [%s] Swp:%lu", RATE_STRS[s_rateIdx], (unsigned long)totalSweeps);
    tft.drawString(hdr, 2, 2);

    tft.setTextColor(activeCount > 0 ? TFT_GREEN : TFT_ORANGE, bruceConfig.bgColor);
    char actStr[32];
    snprintf(actStr, sizeof(actStr), "Active CH:%d", activeCount);
    tft.drawRightString(actStr, tftWidth - 2, 2, 1);

    int sepY = LH + 2;
    tft.drawFastHLine(0, sepY, tftWidth, TFT_DARKGREY);

    int y = sepY + 2;
    int lineHeight = LH - 1;
    if (lineHeight < 8) lineHeight = 8;

    // Sort channels by hits descending to show the top active channels
    int sortedCh[TOTAL_CHANNELS];
    for (int i = 0; i < TOTAL_CHANNELS; i++) sortedCh[i] = i;
    std::sort(sortedCh, sortedCh + TOTAL_CHANNELS, [](int a, int b) {
        return s_chanStats[a].hits > s_chanStats[b].hits;
    });

    int displayed = 0;
    int maxLines = (tftHeight - sepY - LH - 4) / lineHeight;
    if (maxLines > 12) maxLines = 12;

    for (int i = 0; i < TOTAL_CHANNELS && displayed < maxLines; i++) {
        int ch = sortedCh[i];
        if (s_chanStats[ch].hits == 0) break; // No more active channels

        char line[64];
        // Format: CH42: 154 hits (miss: 2) -> alive
        snprintf(line, sizeof(line), "CH%02d: %u hits (miss:%u)",
                 ch, s_chanStats[ch].hits, s_chanStats[ch].consecutiveMisses);

        // Highlight channels that are actively hit right now (low consecutive misses)
        bool isCurrentlyActive = (s_chanStats[ch].consecutiveMisses < 30);
        tft.setTextColor(isCurrentlyActive ? TFT_GREEN : TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawString(line, 2, y);
        y += lineHeight;
        displayed++;
    }

    if (displayed == 0) {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawString("Close door with remote on...", 4, y + 6);
        tft.drawString("Analyzing spectrum silence & peaks", 4, y + 6 + LH);
    }

    int footerY = tftHeight - LH - 1;
    tft.drawFastHLine(0, footerY - 1, tftWidth, TFT_DARKGREY);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawString("[OK]Lock CH  [C]lear  [ESC]", 2, footerY);
}

static bool s_isPaused = false;

// ── Draw UI for Phase 2: Targeted Sniff ──────────────────────────
static void drawTargetedScreen(int currentCh, int preambleIdx, uint32_t totalCaptured) {
    tft.fillScreen(bruceConfig.bgColor);

    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "LockSniff [%s] P:%02X", RATE_STRS[s_rateIdx], PREAMBLES[preambleIdx][0]);
    tft.drawString(hdr, 2, 2);

    if (s_isPaused) {
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        tft.drawRightString("[PAUSED]", tftWidth - 2, 2, 1);
    } else {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        char capStr[24];
        snprintf(capStr, sizeof(capStr), "Cap:%lu", (unsigned long)totalCaptured);
        tft.drawRightString(capStr, tftWidth - 2, 2, 1);
    }

    int sepY = LH + 2;
    tft.drawFastHLine(0, sepY, tftWidth, TFT_DARKGREY);

    int y = sepY + 2;
    int lineHeight = LH - 1;
    if (lineHeight < 8) lineHeight = 8;

    // Line height reserve: footer (LH) + channel bar (LH) + sep (2)
    int maxLines = (tftHeight - sepY - (LH * 2) - 6) / lineHeight;
    if (maxLines > MAX_CANDIDATES) maxLines = MAX_CANDIDATES;

    if (s_candCount == 0) {
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawString("Listening on locked set...", 4, y + 4);
        tft.drawString("Awaiting address matches...", 4, y + 4 + LH);
    } else {
        // Sort candidates by hits descending (highest hits on top!)
        int sortedCand[MAX_CANDIDATES];
        for (int i = 0; i < s_candCount; i++) sortedCand[i] = i;
        std::sort(sortedCand, sortedCand + s_candCount, [](int a, int b) {
            return s_candidates[a].hits > s_candidates[b].hits;
        });

        for (int i = 0; i < s_candCount && i < maxLines; i++) {
            const CandidateAddr &c = s_candidates[sortedCand[i]];
            char line[64];
            snprintf(line, sizeof(line), "c%02d %02X:%02X:%02X:%02X:%02X (x%d)",
                     c.channel, c.addr[0], c.addr[1], c.addr[2], c.addr[3], c.addr[4], c.hits);

            tft.setTextColor(c.hits > 5 ? TFT_GREEN : (i == 0 ? bruceConfig.secColor : TFT_WHITE), bruceConfig.bgColor);
            tft.drawString(line, 2, y);
            y += lineHeight;
        }
    }

    // Channel status row right above footer
    int chBarY = tftHeight - (LH * 2) - 3;
    tft.drawFastHLine(0, chBarY - 1, tftWidth, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    int xPos = 2;
    xPos += tft.drawString("CH: ", xPos, chBarY);
    for (int i = 0; i < s_numLocked; i++) {
        char chBuf[16];
        snprintf(chBuf, sizeof(chBuf), "%d ", s_lockedChannels[i]);
        if (s_lockedChannels[i] == currentCh && !s_isPaused) {
            tft.setTextColor(TFT_GREEN, bruceConfig.bgColor); // Active channel in GREEN
        } else {
            tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        }
        xPos += tft.drawString(chBuf, xPos, chBarY);
    }

    // Footer
    int footerY = tftHeight - LH - 1;
    tft.drawFastHLine(0, footerY - 1, tftWidth, TFT_DARKGREY);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawString(s_isPaused ? "[Space]RESUME  [P]rofile  [ESC]" : "[Space]PAUSE  [P]rofile  [ESC]", 2, footerY);
}

// ── Record Candidate Address (Matches by 4-byte address across any channel!) ──
static void recordCandidateAddress(int channel, int preambleIdx, const uint8_t *addr5) {
    // Crucial: Match by ADDRESS, regardless of which hopping channel it was received on!
    for (int i = 0; i < s_candCount; i++) {
        if (memcmp(s_candidates[i].addr, addr5, 4) == 0) {
            s_candidates[i].hits++;
            s_candidates[i].channel = channel; // update last seen channel
            s_candidates[i].lastSeen = millis();
            return;
        }
    }

    int targetIdx = s_candCount;
    if (targetIdx >= MAX_CANDIDATES) {
        int minHits = s_candidates[0].hits;
        targetIdx = 0;
        for (int i = 1; i < MAX_CANDIDATES; i++) {
            if (s_candidates[i].hits < minHits) {
                minHits = s_candidates[i].hits;
                targetIdx = i;
            }
        }
    } else {
        s_candCount++;
    }

    CandidateAddr &c = s_candidates[targetIdx];
    c.channel = channel;
    c.preamble = PREAMBLES[preambleIdx][0];
    memcpy(c.addr, addr5, 5);
    c.hits = 1;
    c.lastSeen = millis();
}

// ── Main Sniffer Entry ──────────────────────────────────────────
void nrf_sniffer() {
    NRF24_MODE mode = nrf_setMode();
    if (!CHECK_NRF_SPI(mode)) {
        displayError("NRF24 SPI mode required", true);
        return;
    }

    if (!nrf_start(NRF_MODE_SPI)) {
        displayError("NRF24 not found", true);
        return;
    }

    memset(s_chanStats, 0, sizeof(s_chanStats));
    s_candCount = 0;
    s_numLocked = 0;
    s_phase = PHASE_SPECTRUM_PROFILER;

    uint32_t totalSweeps = 0;
    uint32_t totalCaptured = 0;
    unsigned long lastDraw = millis();

    int lockChIdx = 0;
    int preambleIdx = 0;
    unsigned long dwellStart = millis();

    while (true) {
        if (check(EscPress)) {
            break;
        }

        if (s_keepAwake) {
            previousMillis = millis(); // Refresh watchdog to keep screen on
        }

        char k = checkLetterShortcutPress();

        // ══════════════════════════════════════════════════════════════
        // PHASE 1: SPECTRUM PROFILER (Fast RPD scan across 0..125)
        // ══════════════════════════════════════════════════════════════
        if (s_phase == PHASE_SPECTRUM_PROFILER) {
            // Rapid sweep across all 126 channels (128us dwell each)
            digitalWrite(bruceConfigPins.NRF24_bus.io0, LOW);
            for (int ch = 0; ch < TOTAL_CHANNELS; ch++) {
                NRFradio.setChannel(ch);
                NRFradio.startListening();
                delayMicroseconds(128);
                NRFradio.stopListening();

                if (NRFradio.testRPD()) {
                    s_chanStats[ch].hits++;
                    s_chanStats[ch].consecutiveMisses = 0;
                    s_chanStats[ch].lastHitTime = millis();
                } else {
                    if (s_chanStats[ch].hits > 0) {
                        s_chanStats[ch].consecutiveMisses++;
                    }
                }
            }
            digitalWrite(bruceConfigPins.NRF24_bus.io0, HIGH);
            totalSweeps++;

            // Count currently active channels (channels with high hits and low misses)
            uint8_t activeCount = 0;
            for (int ch = 0; ch < TOTAL_CHANNELS; ch++) {
                if (s_chanStats[ch].hits > 5 && s_chanStats[ch].consecutiveMisses < 50) {
                    activeCount++;
                }
            }

            // Keyboard actions in Profiler
            if (check(SelPress) || k == 'l' || k == 'L' || (totalSweeps > 300 && activeCount >= 1 && activeCount <= 8)) {
                // Auto-lock or manual OK lock on top active channels
                s_numLocked = 0;
                int sortedCh[TOTAL_CHANNELS];
                for (int i = 0; i < TOTAL_CHANNELS; i++) sortedCh[i] = i;
                std::sort(sortedCh, sortedCh + TOTAL_CHANNELS, [](int a, int b) {
                    return s_chanStats[a].hits > s_chanStats[b].hits;
                });

                for (int i = 0; i < TOTAL_CHANNELS && s_numLocked < MAX_LOCKED_CH; i++) {
                    int ch = sortedCh[i];
                    if (s_chanStats[ch].hits > 3 && s_chanStats[ch].consecutiveMisses < 80) {
                        s_lockedChannels[s_numLocked++] = ch;
                    }
                }

                if (s_numLocked > 0) {
                    s_phase = PHASE_TARGETED_SNIFF;
                    lockChIdx = 0;
                    preambleIdx = 0;
                    setupSniffChannel(s_lockedChannels[0], 0);
                    dwellStart = millis();
                }
            } else if (k == 'c' || k == 'C') {
                memset(s_chanStats, 0, sizeof(s_chanStats));
                totalSweeps = 0;
            }

            if (millis() - lastDraw > 250) {
                drawProfilerScreen(totalSweeps, activeCount);
                lastDraw = millis();
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }
        // ══════════════════════════════════════════════════════════════
        // PHASE 2: TARGETED LOCK SNIFF (Hop ONLY across locked channels)
        // ══════════════════════════════════════════════════════════════
        else {
            int ch = s_lockedChannels[lockChIdx];

            // Only process radio packets if NOT paused
            if (!s_isPaused) {
                while (NRFradio.available()) {
                    uint8_t buf[32];
                    NRFradio.read(buf, 32);

                    // Check for non-blank packet
                    bool invalid = true;
                    for (int i = 0; i < 5; i++) {
                        if (buf[i] != 0x00 && buf[i] != 0xFF && buf[i] != 0x55 && buf[i] != 0xAA) {
                            invalid = false;
                            break;
                        }
                    }

                    if (!invalid) {
                        recordCandidateAddress(ch, preambleIdx, buf);
                        totalCaptured++;

                        Serial.printf("[LOCK-CAP CH%02d P:%02X] ADDR: %02X:%02X:%02X:%02X:%02X\n",
                                      ch, PREAMBLES[preambleIdx][0],
                                      buf[0], buf[1], buf[2], buf[3], buf[4]);
                    }
                }

                // Dwell for 30ms on this hopping channel before rotating to next channel in the hopping set
                if (millis() - dwellStart >= 30) {
                    lockChIdx = (lockChIdx + 1) % s_numLocked;
                    if (lockChIdx == 0) {
                        preambleIdx = (preambleIdx + 1) % 2;
                    }
                    setupSniffChannel(s_lockedChannels[lockChIdx], preambleIdx);
                    dwellStart = millis();
                }
            }

            // Controls in Targeted Sniff
            // Space, OK/SEL, or 's' toggles Pause/Resume
            if (check(SelPress) || k == ' ' || k == 's' || k == 'S') {
                s_isPaused = !s_isPaused;
                if (!s_isPaused) {
                    setupSniffChannel(s_lockedChannels[lockChIdx], preambleIdx);
                    dwellStart = millis();
                }
                drawTargetedScreen(ch, preambleIdx, totalCaptured);
            } else if (k == 'p' || k == 'P') {
                // Return to profiler mode
                s_phase = PHASE_SPECTRUM_PROFILER;
                s_numLocked = 0;
                s_isPaused = false;
            } else if (k == 'c' || k == 'C') {
                s_candCount = 0;
                totalCaptured = 0;
            }

            if (millis() - lastDraw > 200) {
                drawTargetedScreen(ch, preambleIdx, totalCaptured);
                lastDraw = millis();
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    NRFradio.stopListening();
    NRFradio.powerDown();
}
