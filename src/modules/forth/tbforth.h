#ifndef TBFORTH_H
#define TBFORTH_H

#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TBFORTH_VERSION "4.08"
#define DICT_VERSION 22

#ifndef MAX_DICT_CELLS
#define MAX_DICT_CELLS      (4096)
#endif

#ifndef TOTAL_RAM_CELLS
#define TOTAL_RAM_CELLS     (4096) /* 16KB */
#endif

#define PAD_SIZE_BYTES      (1024)  /* bytes */
#define DS_CELLS            64
#define RS_CELLS            64

#define TIB_SIZE            (160)

#define USE_LITTLE_ENDIAN
#define SUPPORT_FLOAT_FIXED

typedef uint16_t CELL;
typedef uint32_t RAMC;

typedef enum { U_OK=0, COMPILING, E_NOT_A_WORD, E_NOT_A_NUM,
  E_STACK_UNDERFLOW, E_RSTACK_OVERFLOW,
  E_DSTACK_OVERFLOW, E_ABORT, E_EXIT } tbforth_stat;

typedef enum { NO_ABORT=0, ABORT_CTRL_C=1, ABORT_NAW=2,
               ABORT_ILLEGAL=3, ABORT_WORD=4, ABORT_STACKOVER=5 } abort_t;

extern abort_t _tbforth_abort_request;
#define tbforth_abort_request(why) _tbforth_abort_request = why
#define tbforth_aborting() (_tbforth_abort_request != NO_ABORT)
#define tbforth_abort_reason() _tbforth_abort_request
#define tbforth_abort_clr() (_tbforth_abort_request = NO_ABORT)

#ifdef __cplusplus
extern "C" {
#endif

char* tbforth_count_str(CELL addr, CELL* new_addr);

extern RAMC tbforth_ram[];
extern struct dict *dict;

extern void tbforth_init(void);
extern void tbforth_bootstrap(void);
extern void tbforth_load_prims(void);
extern void tbforth_abort(CELL idx);
extern tbforth_stat tbforth_interpret(const char*);
extern tbforth_stat c_handle(void);
extern void tbforth_cdef (const char*, int);
extern char* tbforth_next_word (void);
extern void tbforth_print_str(const char*);
extern void forth_output(const char*);
extern void forth_define_native(const char *name, CELL id);

#ifdef __cplusplus
}
#endif

struct tbforth_iram {
  RAMC state;           /* 0=interpreting .. */
  RAMC total_ram;       /* Total ram available */
  RAMC compiling_word;  /* 0=none */
  RAMC tibidx;          /* current index into buffer */
  RAMC tibwordidx;      /* point to current word in inbufptr */
  RAMC tibwordlen;      /* length of current word in inbufptr */
  RAMC tibclen;         /* size of data in the tib buffer */
  char tib[TIB_SIZE];   /* input buffer for interpreter */
};

struct tbforth_uram {
  RAMC len;          /* size of URAM */
  RAMC base;         /* numeric base for I/O */
  RAMC fixedp;       /* fixed point places */
  RAMC didx;         /* data stack index */
  RAMC ridx;         /* return stack index */
  RAMC dsize;        /* size of data stack */
  RAMC rsize;        /* size of return stack */
  RAMC ds[];         /* data & return stack */
};

struct dict {
  CELL version;         /* dictionary version number */
  CELL word_size;       /* size of native word */
  CELL max_cells;       /* MAX_DICT_CELLS */
  CELL here;            /* top of dictionary */
  CELL last_word_idx;   /* top word's code token (used for searches) */
  CELL varidx;          /* keep track of next variable slot */
  CELL d[MAX_DICT_CELLS]; /* dictionary */
};

#define dict_start_def()
#define dict_here() dict->here
#define dict_set_last_word(cell) dict->last_word_idx=cell
#define dict_incr_varidx(n) (dict->varidx += n)
#define dict_incr_here(n) dict->here += n
#define dict_append(cell) tbforth_dict[dict->here] = cell, dict_incr_here(1)
#define dict_write(idx,cell) tbforth_dict[idx] = cell
#define dict_append_string(src,len) { \
    memcpy((char*)((tbforth_dict)+(dict->here)),src,len); \
    dict->here += (len/sizeof(CELL)) + (len % sizeof(CELL)); \
}
#define dict_end_def()

extern CELL *tbforth_dict;
extern struct tbforth_iram *tbforth_iram;
extern struct tbforth_uram *tbforth_uram;

#define dpush(n) (tbforth_uram->ds[1+tbforth_uram->didx] = (RAMC)(n), tbforth_uram->didx++)
#define dpop() tbforth_uram->ds[tbforth_uram->didx--]
#define dpick(n) tbforth_uram->ds[tbforth_uram->didx-n]

#define rpush(n) (tbforth_uram->ds[--tbforth_uram->ridx] = (RAMC)(n))
#define rpop() tbforth_uram->ds[tbforth_uram->ridx++]
#define rpick(n) tbforth_uram->ds[tbforth_uram->ridx+n]

#define dtop() tbforth_uram->ds[tbforth_uram->didx]
#define dtop2() tbforth_uram->ds[(tbforth_uram->didx)-1]
#define dtop3() tbforth_uram->ds[(tbforth_uram->didx)-2]

#endif // TBFORTH_H
