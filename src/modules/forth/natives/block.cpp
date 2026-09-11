#include "natives_internal.h"
#include "../uforth.h"
#include "core/sd_functions.h"
#include <Arduino.h>
#include <FS.h>

static void fn_mem_swap() {
    size_t size = dpop();
    CELL tmp_addr = dpop();
    CELL dst_addr = dpop();
    CELL src_addr = dpop();

    uint8_t *src = (uint8_t*)&uforth_ram[src_addr];
    uint8_t *dst = (uint8_t*)&uforth_ram[dst_addr];
    uint8_t *tmp = (uint8_t*)&uforth_ram[tmp_addr];

    memcpy(tmp, src, size);
    memcpy(src, dst, size);
    memcpy(dst, tmp, size);
}

static void fn_block_reorder() {
    CELL buf_addr = dpop();
    CELL order_addr = dpop();

    uint8_t *order = (uint8_t*)&uforth_ram[order_addr];
    uint8_t *buf   = (uint8_t*)&uforth_ram[buf_addr];

    uint8_t tmp[64];

    for (int i = 0; i < 16; i++) {
        for (int j = 1; j < 16 - i; j++) {
            if (order[j] < order[j - 1]) {
                // 1. Swap valori nella tabella order
                uint8_t t = order[j];
                order[j] = order[j - 1];
                order[j - 1] = t;

                // 2. Swap dei due slot da 64 byte nel buffer RAM
                uint8_t *slot_prev = buf + (j - 1) * 64;
                uint8_t *slot_curr = buf + j * 64;
                memcpy(tmp, slot_prev, 64);
                memcpy(slot_prev, slot_curr, 64);
                memcpy(slot_curr, tmp, 64);
            }
        }
    }
}

static void fn_block_read_page() {
    int page_num = dpop();
    int num = dpop();
    CELL addr = dpop();

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_read_page: storage not available");
        return;
    }

    char *base_ptr = (char*)&uforth_ram[addr];
    memset(base_ptr, 0, 1024);

    if (!fs->exists(filepath)) {
        log_w("fn_block_read_page: file '%s' does not exist, using empty page %d", filepath, page_num);
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        log_e("fn_block_read_page: failed to open '%s'", filepath);
        return;
    }

    int skip_lines = page_num * 16;
    int curr_line = 0;
    while (file.available() && curr_line < skip_lines) {
        file.readStringUntil('\n');
        curr_line++;
    }

    int line_idx = 0;
    while (file.available() && line_idx < 16) {
        String line = file.readStringUntil('\n');
        while (line.length() > 0 && (line.endsWith("\r") || line.endsWith("\n"))) {
            line.remove(line.length() - 1);
        }

        int len = line.length();
        if (len > 64) len = 64;

        char *line_ptr = base_ptr + line_idx * 64;
        memcpy(line_ptr, line.c_str(), len);
        line_idx++;
    }
    file.close();
    log_d("fn_block_read_page: loaded page %d of '%s'", page_num, filepath);
}

static void fn_block_read() {
    dpush(0); // page_num = 0
    fn_block_read_page();
}

static void fn_block_write_page() {
    int page_num = dpop();
    int num = dpop();
    CELL addr = dpop();

    char filepath[64];
    char tmppath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);
    snprintf(tmppath, sizeof(tmppath), "/forth/blocks/%d.tmp", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_write_page: storage not available");
        return;
    }

    if (!fs->exists("/forth")) fs->mkdir("/forth");
    if (!fs->exists("/forth/blocks")) fs->mkdir("/forth/blocks");

    File src;
    bool src_exists = fs->exists(filepath);
    if (src_exists) {
        src = fs->open(filepath, "r");
    }

    File dst = fs->open(tmppath, "w");
    if (!dst) {
        if (src) src.close();
        log_e("fn_block_write_page: failed to open tmp file for writing");
        return;
    }

    int start_line = page_num * 16;
    int current_line = 0;

    // Phase 1: Copy lines before target page
    while (current_line < start_line) {
        if (src && src.available()) {
            String line = src.readStringUntil('\n');
            while (line.length() > 0 && (line.endsWith("\r") || line.endsWith("\n"))) {
                line.remove(line.length() - 1);
            }
            dst.println(line);
        } else {
            dst.println(""); // Fill missing preceding lines
        }
        current_line++;
    }

    // Phase 2: Write 16 lines from RAM buffer, skip target page lines in src if present
    char *base_ptr = (char*)&uforth_ram[addr];
    for (int i = 0; i < 16; i++) {
        if (src && src.available()) {
            src.readStringUntil('\n'); // Skip old line in src
        }

        char *line_ptr = base_ptr + i * 64;
        int len = strnlen(line_ptr, 64);
        while (len > 0 && line_ptr[len - 1] == ' ') {
            len--;
        }
        char buf[65];
        memcpy(buf, line_ptr, len);
        buf[len] = '\0';
        dst.println(buf);
        current_line++;
    }

    // Phase 3: Copy remaining lines after target page (if src has more)
    if (src && src.available()) {
        while (src.available()) {
            String line = src.readStringUntil('\n');
            while (line.length() > 0 && (line.endsWith("\r") || line.endsWith("\n"))) {
                line.remove(line.length() - 1);
            }
            dst.println(line);
        }
    }

    if (src) src.close();
    dst.close();

    fs->remove(filepath);
    fs->rename(tmppath, filepath);
    log_d("fn_block_write_page: successfully wrote page %d of '%s'", page_num, filepath);
}

static void fn_block_write() {
    dpush(0); // page_num = 0
    fn_block_write_page();
}

static void fn_block_interpret() {
    int num = dpop();
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        forth_output("Storage not available\n");
        return;
    }

    if (!fs->exists(filepath)) {
        forth_output("Block file not found: ");
        forth_output(filepath);
        forth_output("\n");
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        forth_output("Failed to open block file\n");
        return;
    }

    int line_num = 1;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("\\")) {
            line_num++;
            continue;
        }

        char buf[256];
        strncpy(buf, line.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        uforth_stat st = uforth_interpret(buf);
        if (st != UFORTH_OK) {
            char errMsg[192];
            snprintf(errMsg, sizeof(errMsg), "Error %d on line %d: %s\n", (int)st, line_num, buf);
            forth_output(errMsg);
            file.close();
            uforth_abort_request(ABORT_NAW);
            uforth_abort();
            return;
        }
        line_num++;
    }
    file.close();
}

void block_bindings() {
    forth_register("load", fn_block_interpret);
    forth_register("br.block.read", fn_block_read);
    forth_register("br.block.write", fn_block_write);
    forth_register("br.block.readPage", fn_block_read_page);
    forth_register("br.block.writePage", fn_block_write_page);
    forth_register("br.block.memSwap", fn_mem_swap);
    forth_register("mem-swap", fn_mem_swap);
    forth_register("br.block.reorder", fn_block_reorder);
    forth_register("reorder", fn_block_reorder);
}

void block_definitions() {
    uforth_interpret(": block-read 0 br.block.readPage ;");
    uforth_interpret(": block-write 0 br.block.writePage ;");
    uforth_interpret(": block-read-page br.block.readPage ;");
    uforth_interpret(": block-write-page br.block.writePage ;");
    uforth_interpret(": init-line-order ( order-addr buf-addr -- )\n"
                     "  16 0 do 255 2 pick i +c! loop\n"
                     "  0 16 0 do 1 pick i 64 * +c@ if drop i 1+ then loop\n"
                     "  dup 0= if drop drop drop exit then\n"
                     "  0 3 pick 0 +c!\n"
                     "  dup 1 do 2 pick i 1- +c@ 2 pick i 1- 8 * + line-rows + 3 pick i +c! loop\n"
                     "  drop drop drop ;");
}
