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

#define TOTAL_RAM_CELLS     256     /* 256 * 8 bytes = 2KB */
#define PAD_SIZE            128
#define TIB_SIZE            PAD_SIZE

#define TASK0_DS_CELLS      64
#define TASK0_RS_CELLS      32
#define TASK0_URAM_CELLS    150

#endif /* UFORTH_CONFIG_H */
