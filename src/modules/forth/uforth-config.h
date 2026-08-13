#ifndef UFORTH_CONFIG_H
#define UFORTH_CONFIG_H
#include <stdint.h>
#include <stdlib.h>
#define INLINE
#define MAX_DICT_CELLS      4096    /* 8KB dictionary */
#define USE_LITTLE_ENDIAN
#define FIXED_PT_DIVISOR    ((double)(1000000.0))
#define FIXED_PT_PLACES     6
#define FIXED_PT_MULT(x)    (x*(1000000))

/* Cardputer (ESP32-S3) has plenty of SRAM (512KB)
*/
#define TOTAL_RAM_CELLS     1024    /* 1024 * 8 bytes = 8KB */

/* NOTE: PAD_SIZE/TIB_SIZE are counts of DCELL *cells* here (not bytes),
 * since PAD_ADDR/PAD_STR and the tib buffer are laid out in uforth_ram
 * (DCELL[]) units. Keep this in mind if you ever change PAD_SIZE. */
#define PAD_SIZE            64
#define TIB_SIZE            128     /* chars for the text-input-buffer (tib[TIB_SIZE] is char[], byte-sized) */

#define TASK0_DS_CELLS      64
#define TASK0_RS_CELLS      32
#define TASK0_URAM_CELLS    150
#endif /* UFORTH_CONFIG_H */
