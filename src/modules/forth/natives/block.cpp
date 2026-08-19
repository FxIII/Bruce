#include "natives_internal.h"
#include "../uforth.h"
#include "core/sd_functions.h"
#include <Arduino.h>
#include <FS.h>

static void fn_block_read() {
    int num = dpop();
    CELL addr = dpop();

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_read: storage not available");
        return;
    }

    // Initialize the entire buffer in RAM to spaces first
    char *base_ptr = (char*)&uforth_ram[addr];
    for (int i = 0; i < 16; i++) {
        memset(base_ptr + i * 64, ' ', 64);
    }

    if (!fs->exists(filepath)) {
        log_w("fn_block_read: file '%s' does not exist, using empty block", filepath);
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        log_e("fn_block_read: failed to open '%s'", filepath);
        return;
    }

    int line_idx = 0;
    while (file.available() && line_idx < 16) {
        String line = file.readStringUntil('\n');
        // Remove trailing carriage returns/spaces
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
    log_d("fn_block_read: loaded '%s'", filepath);
}

static void fn_block_write() {
    int num = dpop();
    CELL addr = dpop();

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_write: storage not available");
        return;
    }

    // Ensure directory /forth/blocks exists
    if (!fs->exists("/forth")) {
        fs->mkdir("/forth");
    }
    if (!fs->exists("/forth/blocks")) {
        fs->mkdir("/forth/blocks");
    }

    File file = fs->open(filepath, "w");
    if (!file) {
        log_e("fn_block_write: failed to open '%s' for writing", filepath);
        return;
    }

    char *base_ptr = (char*)&uforth_ram[addr];
    for (int i = 0; i < 16; i++) {
        char *line_ptr = base_ptr + i * 64;

        // Trim trailing spaces for cleaner text files on disk
        int trim_len = 64;
        while (trim_len > 0 && line_ptr[trim_len - 1] == ' ') {
            trim_len--;
        }

        char buf[65];
        memcpy(buf, line_ptr, trim_len);
        buf[trim_len] = '\0';

        file.println(buf);
    }
    file.close();
    log_d("fn_block_save: saved '%s'", filepath);
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
}
