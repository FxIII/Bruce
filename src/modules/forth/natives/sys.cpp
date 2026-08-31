#include "natives_internal.h"
#include "../forth_repl.h"
#include "core/mykeyboard.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define BIT_PRIM 0x40
#define BIT_IMM  0x80

// Render active ConsoleWidget immediately if available
static void render_console_if_active() {
    ConsoleWidget* cw = getActiveConsoleWidget();
    if (cw) cw->render();
}

// sys.ds - Print Data Stack status
static void fn_sys_ds() {
    char buf[128];
    int depth = (int)(uforth_uram->didx + 1);
    snprintf(buf, sizeof(buf), "DS: %d/%d\n", depth, (int)uforth_uram->dsize);
    forth_output(buf);

    if (depth <= 0) {
        forth_output(" [Empty]\n");
        render_console_if_active();
        return;
    }

    for (int i = 0; i <= uforth_uram->didx; i++) {
        DCELL val = uforth_uram->ds[i];
        snprintf(buf, sizeof(buf), " [%d] %lld (0x%llX)\n", i, (long long)val, (unsigned long long)val);
        forth_output(buf);
    }
    render_console_if_active();
}

// sys.rs - Print Return Stack status
static void fn_sys_rs() {
    char buf[128];
    int total_stack_size = (int)(uforth_uram->dsize + uforth_uram->rsize);
    int top_init = total_stack_size - 1;
    int items = top_init - (int)uforth_uram->ridx;

    snprintf(buf, sizeof(buf), "RS: %d/%d\n", items, (int)uforth_uram->rsize);
    forth_output(buf);

    if (items <= 0) {
        forth_output(" [Empty]\n");
        render_console_if_active();
        return;
    }

    for (int idx = top_init; idx > uforth_uram->ridx; idx--) {
        DCELL val = uforth_uram->ds[idx];
        snprintf(buf, sizeof(buf), " [%d] %lld (0x%llX)\n", idx, (long long)val, (unsigned long long)val);
        forth_output(buf);
    }
    render_console_if_active();
}

// sys.dict - Print Dictionary status and paginated/aligned words list
static void fn_sys_dict() {
    char buf[128];
    CELL free_cells = dict->max_cells - dict->here;

    // Collect word indices backward from dictionary header
    CELL indices[512];
    int count = 0;
    CELL idx = dict->last_word_idx;
    while (idx != 0 && count < 512) {
        indices[count++] = idx;
        idx = uforth_dict[idx];
    }

    int total_pages = (count + 9) / 10;
    int page = 0;

    while (page < total_pages) {
        int start_k = page * 10;
        int end_k = start_k + 10;
        if (end_k > count) end_k = count;

        snprintf(buf, sizeof(buf), "DICT: %d/%d c (%d free), %d words [Pg %d/%d]\n",
                 (int)dict->here, (int)dict->max_cells, (int)free_cells, count, page + 1, total_pages);
        forth_output(buf);

        for (int k = start_k; k < end_k; k++) {
            // Ascending chronological order
            int i = (count - 1) - k;
            CELL cur = indices[i];
            CELL next = (i > 0) ? indices[i - 1] : dict->here;
            CELL cells = next - cur;

            uint8_t flags = (uint8_t)uforth_dict[cur + 1];
            uint8_t len = flags & 0x3F;

            char name[64];
            if (len > 0 && len < 63) {
                memcpy(name, (char *)(uforth_dict + cur + 2), len);
                name[len] = '\0';
            } else {
                snprintf(name, sizeof(name), "?");
            }

            CELL header_size = 2 + (len / BYTES_PER_CELL) + (len % BYTES_PER_CELL);
            CELL body_idx = cur + header_size;

            const char *type = "USR ";
            if (flags & BIT_PRIM) {
                type = (flags & BIT_IMM) ? "PRMI" : "PRM ";
            } else if (flags & BIT_IMM) {
                type = "IMM ";
            } else if (body_idx + 3 < dict->here && uforth_dict[body_idx] == 1 && uforth_dict[body_idx + 2] == 44) {
                type = "NAT ";
            }

            snprintf(buf, sizeof(buf), "%4d %3dc %-4s %s\n", (int)cur, (int)cells, type, name);
            forth_output(buf);
        }

        page++;
        if (page < total_pages) {
            forth_output("-- More (ENTER next, Q exit) --");
            render_console_if_active();

            bool exit_paging = false;
            keyStroke ks;
            while (true) {
                ks = _getKeyPress();
                if (ks.pressed) {
                    // Exit only if physical ESC/exit key (without enter flag) OR 'q'/'Q' is pressed
                    bool is_q = !ks.word.empty() && (ks.word[0] == 'q' || ks.word[0] == 'Q');
                    if ((ks.exit_key && !ks.enter) || is_q) {
                        exit_paging = true;
                    }
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // Wait for key release to prevent multiple rapid page steps
            while (_getKeyPress().pressed) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            forth_output("\n");
            render_console_if_active();

            if (exit_paging) break;
        } else {
            render_console_if_active();
        }
    }
}

// sys.ram - Print RAM memory allocation status
static void fn_sys_ram() {
    char buf[128];
    CELL var_cells = dict->varidx;
    DCELL total_ram = uforth_iram->total_ram; // 1024 cells
    DCELL free_ram = total_ram - IRAM_BYTES - TASK0_URAM_CELLS - PAD_SIZE - var_cells;

    snprintf(buf, sizeof(buf), "RAM Total: %lld c (%lld B)\n", (long long)total_ram, (long long)(total_ram * 8));
    forth_output(buf);
    snprintf(buf, sizeof(buf), "IRAM: %d c | URAM: %d c | PAD: %d c\n", (int)IRAM_BYTES, (int)TASK0_URAM_CELLS, (int)PAD_SIZE);
    forth_output(buf);
    snprintf(buf, sizeof(buf), "VAR: %d c (%d B) | FREE: %lld c (%lld B)\n",
             (int)var_cells, (int)(var_cells * 8), (long long)free_ram, (long long)(free_ram * 8));
    forth_output(buf);

    render_console_if_active();
}

// sys - Global VM status overview
static void fn_sys() {
    char buf[128];

    snprintf(buf, sizeof(buf), "--- VM STATUS ---\n");
    forth_output(buf);

    snprintf(buf, sizeof(buf), "Mode: %s | Base: %lld | Task: %lld\n",
             uforth_iram->compiling ? "COMPILE" : "INTERPRET",
             (long long)uforth_uram->base,
             (long long)uforth_iram->curtask_idx);
    forth_output(buf);

    snprintf(buf, sizeof(buf), "TIB [%lld]: \"%s\"\n",
             (long long)uforth_iram->tibclen, uforth_iram->tib);
    forth_output(buf);

    int ds_depth = (int)(uforth_uram->didx + 1);
    int total_stack_size = (int)(uforth_uram->dsize + uforth_uram->rsize);
    int rs_items = (total_stack_size - 1) - (int)uforth_uram->ridx;

    snprintf(buf, sizeof(buf), "DS: %d/%d c | RS: %d/%d c\n",
             ds_depth, (int)uforth_uram->dsize, rs_items, (int)uforth_uram->rsize);
    forth_output(buf);

    CELL free_dict = dict->max_cells - dict->here;
    CELL var_cells = dict->varidx;
    DCELL free_ram = uforth_iram->total_ram - IRAM_BYTES - TASK0_URAM_CELLS - PAD_SIZE - var_cells;

    snprintf(buf, sizeof(buf), "DICT: %d/%d c (%d free)\n", (int)dict->here, (int)dict->max_cells, (int)free_dict);
    forth_output(buf);
    snprintf(buf, sizeof(buf), "RAM VAR: %d c | FREE: %lld c\n", (int)var_cells, (long long)free_ram);
    forth_output(buf);

    render_console_if_active();
}

void sys_bindings(void) {
    forth_register("sys.ds", fn_sys_ds);
    forth_register("sys.rs", fn_sys_rs);
    forth_register("sys.dict", fn_sys_dict);
    forth_register("sys.ram", fn_sys_ram);
    forth_register("sys", fn_sys);
}
