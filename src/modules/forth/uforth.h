#ifndef UFORTH_H
#define UFORTH_H

#include "uforth-config.h"

#define UFORTH_VERSION "1.2"
#define DICT_VERSION 0006

typedef uint16_t CELL;
typedef int64_t DCELL;

#ifndef __cplusplus
typedef char bool;
#endif

typedef enum { UFORTH_OK=0, E_NOT_A_WORD, E_STACK_UNDERFLOW, E_RSTACK_OVERFLOW,
     E_DSTACK_OVERFLOW, E_ABORT, E_EXIT } uforth_stat;

#define BYTES_PER_CELL sizeof(CELL)

struct uforth_iram {
    DCELL compiling;
    DCELL total_ram;
    DCELL compiling_word;
    DCELL curtask_idx;
    DCELL tibidx;
    DCELL tibclen;
    DCELL tibwordidx;
    DCELL tibwordlen;
    char tib[TIB_SIZE];
};

struct uforth_uram {
    DCELL len;
    DCELL base;
    DCELL didx;
    DCELL ridx;
    DCELL dsize;
    DCELL rsize;
    DCELL ds[];
};

struct dict {
    CELL version;
    CELL word_size;
    CELL max_cells;
    CELL here;
    CELL last_word_idx;
    CELL varidx;
    CELL d[MAX_DICT_CELLS];
};

#define DICT_HEADER_WORDS   5
#define DICT_INFO_SIZE_BYTES (sizeof(CELL)*DICT_HEADER_WORDS)

#ifdef __cplusplus
extern "C" {
#endif

extern CELL *uforth_dict;
extern struct uforth_iram *uforth_iram;
extern struct uforth_uram *uforth_uram;
extern struct dict *dict;
extern DCELL uforth_ram[];

typedef enum { NO_ABORT=0, ABORT_CTRL_C=1, ABORT_NAW=2,
     ABORT_ILLEGAL=3, ABORT_WORD=4, ABORT_STACKOVER=5 } abort_t;

extern abort_t _uforth_abort_request;

#define uforth_abort_request(why) _uforth_abort_request = why
#define uforth_aborting() (_uforth_abort_request != NO_ABORT)
#define uforth_abort_reason() _uforth_abort_request
#define uforth_abort_clr() (_uforth_abort_request = NO_ABORT)

char* uforth_count_str(CELL addr, CELL* new_addr);

#define dict_start_def()
#define dict_here() dict->here
#define dict_set_last_word(cell) dict->last_word_idx=cell
#define dict_incr_varidx(n) (dict->varidx += n)
#define dict_incr_here(n) dict->here += n
#define dict_append(cell) uforth_dict[dict->here] = cell, dict_incr_here(1)
#define dict_write(idx,cell) uforth_dict[idx] = cell
#define dict_append_string(src,len) { \
    strncpy((char*)((uforth_dict)+(dict->here)),src,len); \
    dict->here += (len/BYTES_PER_CELL) + (len % BYTES_PER_CELL); \
}
#define dict_end_def()

INLINE void dpush(const DCELL w);
INLINE DCELL dpop(void);
INLINE DCELL dpick(const DCELL n);
INLINE uint32_t dpop32(void);
INLINE void dpush32(const uint32_t w2);
INLINE void rpush(const DCELL w);
INLINE DCELL rpop(void);
INLINE DCELL rpick(const DCELL n);

#define dtop() uforth_uram->ds[uforth_uram->didx]
#define rtop() uforth_uram->ds[uforth_uram->ridx]

extern void uforth_init(void);
extern void uforth_load_prims(void);
extern void uforth_abort(void);
extern uforth_stat uforth_interpret(const char*);
extern uforth_stat c_handle(void);
char* uforth_next_word(void);

#define IRAM_BYTES (DCELL)(sizeof(struct uforth_iram))/sizeof(DCELL)
#define URAM_HDR_BYTES (DCELL)(sizeof(struct uforth_uram))/sizeof(DCELL)
#define VAR_ALLOT(n) (IRAM_BYTES+URAM_HDR_BYTES+dict_incr_varidx(n))
#define VAR_ALLOT_1() (IRAM_BYTES+URAM_HDR_BYTES+dict_incr_varidx(1))
#define PAD_ADDR (uforth_iram->total_ram - PAD_SIZE)
#define PAD_STR (char*)&uforth_ram[PAD_ADDR+1]
#define PAD_STRLEN uforth_ram[PAD_ADDR]

#ifdef USE_LITTLE_ENDIAN
# define BYTEPACK_FIRST(b) b
# define BYTEPACK_SECOND(b) ((CELL)b)<<8
#else
# define BYTEPACK_FIRST(b) ((CELL)b)<<8
# define BYTEPACK_SECOND(b) b
#endif

void c_ext_init(void);
void c_ext_create_cmds(void);
uforth_stat c_ext_handle_cmds(CELL n);
void forth_define_native(const char *name, CELL id);
void uforth_load_core(void);

#ifdef __cplusplus
}
#endif

#endif /* UFORTH_H */
