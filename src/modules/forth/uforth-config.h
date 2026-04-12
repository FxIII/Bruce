#ifndef UFORTH_CONFIG_H
#define UFORTH_CONFIG_H
#include <stdint.h>
#include <stdlib.h>

#define INLINE

#define MAX_DICT_CELLS      4096    /* 8KB dictionary */
#define USE_LITTLE_ENDIAN

#define FIXED_PT_MULT       (1LL << 32)
#define FIXED_PT_DIVISOR    ((double)FIXED_PT_MULT)

#define TOTAL_RAM_CELLS     4096    /* 4096 * 8 bytes = 32KB (PSRAM) */
#define PAD_SIZE            128
#define TIB_SIZE            PAD_SIZE

#define TASK0_DS_CELLS      64
#define TASK0_RS_CELLS      32
#define TASK0_URAM_CELLS    150

#endif /* UFORTH_CONFIG_H */
