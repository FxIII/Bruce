#ifndef UFORTH_CONFIG_H
#define UFORTH_CONFIG_H
#include <stdint.h>
#include <stdlib.h>
#define INLINE
#define MAX_DICT_CELLS      8192    /* 16KB dictionary */
#define USE_LITTLE_ENDIAN

/* Cardputer (ESP32-S3) has plenty of SRAM (512KB)
*/
#define TOTAL_RAM_CELLS     1024    /* 1024 * 8 bytes = 8KB */

/* NOTE: PAD_SIZE is a count of DCELL cells (64 * 8 = 512 bytes) at the end of
 * uforth_ram. TIB_SIZE is a count of bytes/chars for the input buffer tib[TIB_SIZE]
 * in struct uforth_iram. Keep this distinction in mind if altering these values. */
#define PAD_SIZE            64      /* DCELL cells (512 bytes) */
#define TIB_SIZE            128     /* chars/bytes for the text-input-buffer */

#define TASK0_DS_CELLS      64
#define TASK0_RS_CELLS      32
#define TASK0_URAM_CELLS    150
#endif /* UFORTH_CONFIG_H */
