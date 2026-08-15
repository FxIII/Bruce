#include "tbforth.h"

#define min(a,b) ((a < b) ? a : b)

CELL *tbforth_dict;             /* treat dict struct like array */
abort_t _tbforth_abort_request; /* for emergency aborts */

struct tbforth_iram *tbforth_iram;
struct tbforth_uram *tbforth_uram;

#define WORD_LEN_BITS 0x3F
#define IMMEDIATE_BIT (1<<7)
#define PRIM_BIT      (1<<6)

RAMC tbforth_ram[TOTAL_RAM_CELLS];

enum {
  LIT=1, DLIT, ABORT, DEF, STATE, IMMEDIATE, URAM_BASE_ADDR, PICK, RPICK,
  HERE, INCR_HERE, RAM_BASE_ADDR,
  ADD, SUB, MULT, DIV, AND, JMP, JMP_IF_ZERO, SKIP_IF_ZERO, EXIT,
  OR, XOR, LSHIFT, RSHIFT, EQ_ZERO, DROP,
  NEXT, CNEXT, EXEC, LESS_THAN_ZERO, MAKE_TASK, SELECT_TASK,
  INVERT, COMMA, DCOMMA, RPUSH, RPOP, FETCH, STORE, DICT_FETCH, DICT_STORE,
  COMMA_STRING,
  VAR_ALLOT, CALLC, FIND, FIND_ADDR, CHAR_APPEND, CHAR_FETCH, DCHAR_FETCH,
  POSTPONE, _CREATE, PARSE_NUM, PARSE_FNUM,
  CHAR_STORE, STORE_URAM_BASE_ADDR, CHAR_A_INCR, CHAR_A_FETCH, CHAR_A_STORE, CHAR_A_FETCH_INCR, CHAR_A_STORE_INCR,
  LAST_PRIMITIVE
};

#define URAM_HDR_BYTES (sizeof(struct tbforth_uram))
#define URAM_START (sizeof(struct tbforth_iram) + URAM_HDR_BYTES)
#define VAR_ALLOTN(n) (sizeof(struct tbforth_iram)/sizeof(RAMC) + URAM_HDR_BYTES/sizeof(RAMC) + dict_incr_varidx(n))
#define VAR_ALLOT_1() (sizeof(struct tbforth_iram)/sizeof(RAMC) + URAM_HDR_BYTES/sizeof(RAMC) + dict_incr_varidx(1))

#define PAD_ADDR (sizeof(struct tbforth_iram) + URAM_HDR_BYTES)
#define PAD_STR (char*)&tbforth_ram[PAD_ADDR+1]
#define PAD_STRLEN tbforth_ram[PAD_ADDR]

#ifdef USE_LITTLE_ENDIAN
# define BYTEPACK_FIRST(b) b
# define BYTEPACK_SECOND(b) ((CELL)b)<<8
#else
# define BYTEPACK_FIRST(b) ((CELL)b)<<8
# define BYTEPACK_SECOND(b) b
#endif

static RAMC parse_num(char *s, uint8_t base) {
  return strtol(s, NULL, tbforth_uram->base == 10 ? 0 : tbforth_uram->base);
}

static CELL find_word(char* s, uint8_t len, RAMC* addr, bool *immediate, bool *prim);

static void make_word(char *str, uint8_t str_len) {
  CELL my_head = dict_here();
  dict_append(dict->last_word_idx);
  dict_append(str_len);
  dict_append_string(str, str_len);
  dict_set_last_word(my_head);
}

static void make_immediate(void) {
  dict_write((dict->last_word_idx+1), tbforth_dict[dict->last_word_idx+1]|IMMEDIATE_BIT);
}

static char next_char(void) {
  if (tbforth_iram->tibidx >= tbforth_iram->tibclen) return 0;
  return tbforth_iram->tib[tbforth_iram->tibidx++];
}

#define EOTIB() (tbforth_iram->tibidx >= tbforth_iram->tibclen)
#define CURR_TIB_WORD &(tbforth_iram->tib[tbforth_iram->tibwordidx])
#define CLEAR_TIB() (tbforth_iram->tibidx=0, tbforth_iram->tibclen=0, tbforth_iram->tibwordlen=0, tbforth_iram->tibwordidx=0)

char* tbforth_next_word (void) {
  char nc; uint8_t cnt = 0;
  do { nc = next_char(); } while (!EOTIB() && isspace((unsigned char)nc));
  if (EOTIB()) return "";
  cnt=1;
  tbforth_iram->tibwordidx = (tbforth_iram->tibidx)-1;
  while(!isspace((unsigned char)next_char()) && !EOTIB()) ++cnt;
  tbforth_iram->tibwordlen = cnt;
  return &(tbforth_iram->tib[tbforth_iram->tibwordidx]);
}

void tbforth_abort(CELL idx) {
  CELL limit = tbforth_uram->rsize + tbforth_uram->dsize;
  if (tbforth_uram->ridx < limit) {
    char msg[128];
    int pos = snprintf(msg, sizeof(msg), " R-Stack:");
    for (CELL i = tbforth_uram->ridx; i < limit; i++) {
      pos += snprintf(msg + pos, sizeof(msg) - pos, " %u", (unsigned int)tbforth_uram->ds[i]);
      if (pos >= sizeof(msg) - 10) break;
    }
    snprintf(msg + pos, sizeof(msg) - pos, "\n");
    tbforth_print_str(msg);
  }

  if (tbforth_iram->state == COMPILING) {
    dict_append(ABORT);
  }
  tbforth_iram->state = 0;
  tbforth_abort_clr();
  tbforth_uram->ridx = tbforth_uram->rsize + tbforth_uram->dsize;
  tbforth_uram->didx = -1;
}

static void store_prim(const char* str, CELL val) {
  make_word((char*)str, strlen(str));
  dict_append(val);
  dict_append(EXIT);
  dict_write((dict->last_word_idx+1), tbforth_dict[dict->last_word_idx+1]|PRIM_BIT);
}

void tbforth_cdef(const char *name, int id) {
  make_word((char*)name, strlen(name));
  dict_append(LIT);
  dict_append(id);
  dict_append(CALLC);
  dict_append(EXIT);
}

void forth_define_native(const char *name, CELL id) {
  tbforth_cdef(name, id);
}

typedef tbforth_stat (*wfunct_t)(void);

static RAMC r1, r2, r3, r4;
static char *str1, *str2;
static char char1;
static CELL cmd;

void tbforth_init(void) {
  memset(tbforth_ram, 0, sizeof(tbforth_ram));
  tbforth_dict = (CELL*)dict;
  tbforth_iram = (struct tbforth_iram*) tbforth_ram;
  tbforth_iram->state = 0;
  tbforth_iram->total_ram = TOTAL_RAM_CELLS;
  tbforth_uram = (struct tbforth_uram*)((void*)tbforth_ram + sizeof(struct tbforth_iram));
  tbforth_uram->len = TOTAL_RAM_CELLS - (sizeof(struct tbforth_iram)/sizeof(RAMC));
  tbforth_uram->dsize = DS_CELLS;
  tbforth_uram->rsize = RS_CELLS;
  tbforth_uram->ridx = DS_CELLS + RS_CELLS;
  tbforth_uram->didx = -1;

  tbforth_abort_clr();
  tbforth_abort(0);

  tbforth_uram->base = 10;
}

void tbforth_load_prims(void) {
  dict->here = 1;
  store_prim("cf", CALLC);
  store_prim("uram", URAM_BASE_ADDR);
  store_prim("ram", RAM_BASE_ADDR);
  store_prim("immediate", IMMEDIATE);
  store_prim("abort", ABORT);
  store_prim("pick", PICK);
  store_prim("rpick", RPICK);
  store_prim("0skip?", SKIP_IF_ZERO);
  store_prim("drop", DROP);
  store_prim("jmp", JMP);
  store_prim("0jmp?", JMP_IF_ZERO);
  store_prim("exec", EXEC);
  store_prim(",", COMMA);
  store_prim("d,", DCOMMA);
  store_prim(">num", PARSE_NUM);
  store_prim(">fnum", PARSE_FNUM);
  store_prim("dummy,", INCR_HERE);
  store_prim("+", ADD);
  store_prim("-", SUB);
  store_prim("and", AND);
  store_prim("or", OR);
  store_prim("xor", XOR);
  store_prim("invert", INVERT);
  store_prim("lshift", LSHIFT);
  store_prim("rshift", RSHIFT);
  store_prim("*", MULT);
  store_prim("/", DIV);
  store_prim("0=", EQ_ZERO);
  store_prim("lit", LIT);
  store_prim("dlit", DLIT);
  store_prim(">r", RPUSH);
  store_prim("r>", RPOP);
  store_prim("dict@", DICT_FETCH);
  store_prim("!", STORE);
  store_prim("@", FETCH);
  store_prim("dict!", DICT_STORE);
  store_prim(";", EXIT);  make_immediate();
  store_prim(":", DEF); 
  store_prim("_create", _CREATE); 
  store_prim("(allot1)", VAR_ALLOT);

  store_prim("(find)", FIND);
  store_prim("(find&)", FIND_ADDR);
  store_prim(",\"", COMMA_STRING); make_immediate();
  store_prim("postpone", POSTPONE); make_immediate();
  store_prim("next-word", NEXT);
  store_prim("next-char", CNEXT);
  store_prim("c!+", CHAR_APPEND);
  store_prim("+c@", CHAR_FETCH);
  store_prim("+c!", CHAR_STORE);
  store_prim("+dict-c@", DCHAR_FETCH);
  store_prim("here", HERE);
  store_prim("<0", LESS_THAN_ZERO);
}

char* tbforth_count_str(CELL addr, CELL* new_addr) {
  char *str = (char*)&tbforth_ram[addr+1];
  *new_addr = tbforth_ram[addr];
  return str;
}

static tbforth_stat exec(CELL wd_idx, bool toplevelprim, uint8_t last_exec_rdix) {
  while(1) {
    if (wd_idx == 0) {
      tbforth_abort_request(ABORT_ILLEGAL);
      tbforth_abort(0);
      return E_NOT_A_WORD;
    }
    cmd = tbforth_dict[wd_idx++];

    if (cmd > LAST_PRIMITIVE) {
      rpush(wd_idx);
      wd_idx = tbforth_dict[wd_idx-1];
      goto CHECK_STAT;
    }

    switch (cmd) {
    case 0:
      tbforth_abort_request(ABORT_ILLEGAL);
      tbforth_abort(0);
      return E_NOT_A_WORD;
    case ABORT:
      tbforth_abort_request(ABORT_WORD);
      break;
    case IMMEDIATE:
      make_immediate();
      break;
    case STATE:
      dpush(tbforth_iram->state);
      break;
    case RAM_BASE_ADDR:
      dpush (0x80000000 | 0);
      break;
    case URAM_BASE_ADDR:
      dpush(0x80000000 | (((char*)tbforth_uram - (char*)tbforth_ram)/sizeof(RAMC)));
      break;
    case STORE_URAM_BASE_ADDR:
      tbforth_uram = (struct tbforth_uram*) &tbforth_ram[0x7FFFFFFF & dpop()];
      break;
    case SKIP_IF_ZERO:
      r1 = dpop(); r2 = dpop();
      if (r2 == 0) wd_idx += r1;
      break;
    case DROP:
      dpop();
      break;
    case JMP:
      wd_idx = dpop();
      break;
    case JMP_IF_ZERO:
      r1 = dpop(); r2 = dpop();
      if (r2 == 0) wd_idx = r1;
      break;
    case HERE:
      dpush(dict_here());
      break;
    case INCR_HERE:
      dict_incr_here(1);
      break;
    case LIT:  
      dpush(tbforth_dict[wd_idx++]);
      break;
    case DLIT:  
      dpush((((RAMC)tbforth_dict[wd_idx])<<16) | (uint16_t)tbforth_dict[wd_idx+1]); 
      wd_idx+=2;
      break;
    case LESS_THAN_ZERO:
      r1 = dpop();
      dpush((int32_t)r1 < 0);
      break;
    case ADD:
      r1 = dpop(); r2 = dtop(); 
      dtop() = r1+r2;
      break;
    case SUB:
      r1 = dpop(); r2 = dtop(); 
      dtop() = r2-r1;
      break;
    case AND:
      r1 = dpop(); r2 = dtop();
      dtop() = r1&r2;
      break;
    case LSHIFT:
      r1 = dpop(); r2 = dtop();
      dtop() = r2<<r1;
      break;
    case RSHIFT:
      r1 = dpop(); r2 = dtop(); 
      dtop() = r2>>r1;
      break;
    case OR:
      r1 = dpop(); r2 = dtop(); 
      dtop() = r1|r2;
      break;
    case XOR:
      r1 = dpop(); r2 = dtop(); 
      dtop() = r1^r2;
      break;
    case INVERT:
      dtop() = ~dtop();
      break;
    case MULT:
      r1 = dpop(); r2 = dtop(); 
      dtop() = r1*r2;
      break;
    case DIV :
      r1 = dpop(); r2 = dtop(); 
      dtop() = r2/r1;
      break;
    case RPICK:
      r1 = dpop();
      r2 = rpick(r1);
      dpush(r2);
      break;
    case PICK:
      r1 = dpop();
      r2 = dpick(r1);
      dpush(r2);
      break;
    case EQ_ZERO:
      dtop() = -(dtop() == 0);
      break;
    case RPUSH:
      rpush(dpop());
      break;
    case RPOP:
      dpush(rpop());
      break;
    case DICT_FETCH:
      r1 = dpop();
      dpush(tbforth_dict[r1]);
      break;
    case DICT_STORE:
      r1 = dpop();
      r2 = dpop();
      dict_write(r1,r2);
      break;
    case FETCH:
      r1 = dpop();
      if (r1 & 0x80000000) {
        dpush(tbforth_ram[r1 & 0x7FFFFFFF]);
      } else {
        dpush(tbforth_dict[r1]);
      }
      break;
    case STORE:
      r1 = dpop();
      r2 = dpop();
      if (r1 & 0x80000000) {
        tbforth_ram[r1 & 0x7FFFFFFF] = r2;
      } else {
        dict_write(r1, r2);
      }
      break;
    case EXEC:
      r1 = dpop();
      rpush(wd_idx);
      wd_idx = r1;
      break;
    case EXIT:
      if (tbforth_uram->ridx > last_exec_rdix) return U_OK;
      wd_idx = rpop();
      break;
    case CNEXT:
      char1 = next_char();
      dpush(char1);
      break;
    case CHAR_FETCH:
      r1 = dpop();
      r2 = dpop();
      if (r2 & 0x80000000)
        str1 = (char*)&tbforth_ram[r2 & 0x7FFFFFFF];
      else
        str1 = (char*)&tbforth_dict[r2];
      str1 += r1;
      dpush((unsigned char)*str1);
      break;
    case DCHAR_FETCH:
      r1 = dpop();
      r2 = dpop();
      str1 = (char*)&tbforth_dict[r2];
      str1 += r1;
      dpush((unsigned char)*str1);
      break;
    case CHAR_STORE:
      r1 = dpop(); // offset
      r2 = dpop(); // base
      if (r2 & 0x80000000)
        str1 = (char*)&tbforth_ram[r2 & 0x7FFFFFFF];
      else
        str1 = (char*)&tbforth_dict[r2];
      str1 += r1;
      *str1 = (char)dpop(); // char
      break;
    case CHAR_APPEND:
      r1 = dpop();
      if (r1 & 0x80000000) {
        r1 &= 0x7FFFFFFF;
        r2 = tbforth_ram[r1];
        str1 = (char*)&tbforth_ram[r1+1];
        tbforth_ram[r1]++;
        str1 += r2;
        char1 = dpop();
        *str1 = char1;
      } else {
        tbforth_abort_request(ABORT_ILLEGAL);
      }
      break;
    case COMMA_STRING:
      if (tbforth_iram->state == COMPILING) {
        dict_append(LIT);
        dict_append(dict_here()+sizeof(RAMC));

        dict_append(LIT);
        rpush(dict_here());
        dict_incr_here(1);
        dict_append(JMP);
      }
      rpush(dict_here());
      dict_incr_here(1);
      r1 = 0;
      do {
        r2 = 0;
        char1 = next_char();
        if (char1 == 0 || char1 == '"') break;
        r2 |= BYTEPACK_FIRST(char1);
        ++r1;
        char1 = next_char();
        if (char1 != 0 && char1 != '"') {
          ++r1;
          r2 |= BYTEPACK_SECOND(char1);
        }
        dict_append(r2);
      } while (char1 != 0 && char1 != '"');
      dict_write(rpop(),r1);
      if (tbforth_iram->state == COMPILING) {
        dict_write(rpop(),dict_here());
      }
      break;
    case CALLC:
      r1 = c_handle();
      if (r1 != U_OK) return (tbforth_stat)r1;
      break;
    case VAR_ALLOT:
      dpush(VAR_ALLOT_1() | 0x80000000);
      break;
    case DEF:
      tbforth_iram->state = COMPILING;
    case _CREATE:
      dict_start_def();
      tbforth_next_word();
      make_word(CURR_TIB_WORD, tbforth_iram->tibwordlen);
      if (cmd == _CREATE) {
        dict_end_def();
      } else {
        tbforth_iram->compiling_word = dict_here();
      }
      break;
    case COMMA:
      dict_append(dpop());
      break;
    case DCOMMA:
      r1 = dpop();
      dict_append((uint32_t)r1>>16);
      dict_append((uint16_t)r1);
      break;
    case PARSE_NUM:
      r1 = dpop();
      if (r1 & 0x80000000)
        str1 = (char*)&tbforth_ram[(r1 & 0x7FFFFFFF)+1];
      else
        str1 = (char*)&tbforth_ram[r1+1];
      r1 = tbforth_ram[r1 & 0x7FFFFFFF];
      str1[r1] = '\0';
      dpush(parse_num(str1, tbforth_uram->base));
      break;
    case PARSE_FNUM:
      r1 = dpop();
      str1 = (char*)&tbforth_ram[(r1 & 0x7FFFFFFF)+1];
      r1 = tbforth_ram[r1 & 0x7FFFFFFF];
      str1[r1] = '.';
      str1[r1+1] = '\0';
      dpush(parse_num(str1, tbforth_uram->base));
      break;
    case FIND:
    case FIND_ADDR:
      r1 = dpop();
      if (r1 & 0x80000000)
        str1 = (char*)&tbforth_ram[(r1 & 0x7FFFFFFF)+1];
      else
        str1 = (char*)&tbforth_ram[r1+1];
      r1 = tbforth_ram[r1 & 0x7FFFFFFF];
      {
        bool prim = false;
        r1 = find_word(str1, r1, &r2, 0, &prim);
        if (r1 > 0) {
          if (prim) r1 = tbforth_dict[r1];
        }
      }
      if (cmd == FIND) {
        dpush(r1);
      } else {
        dpush(r2);
      }
      break;
    case POSTPONE:
      str1 = tbforth_next_word();
      {
        bool prim = false;
        r1 = find_word(str1, tbforth_iram->tibwordlen, 0, 0, &prim);
        if (r1 == 0) {
          tbforth_abort_request(ABORT_NAW);
          tbforth_abort(0);
          return E_NOT_A_WORD;
        }
        if (prim) {
          dict_append(tbforth_dict[r1]);
        } else {
          dict_append(r1);
        }
      }
      break;
    default:
      tbforth_abort_request(ABORT_ILLEGAL);
      break;
    }
  CHECK_STAT:
    if (tbforth_aborting()) {
      tbforth_abort(0);
      return E_ABORT;
    }
    if (toplevelprim) return U_OK;
  }
}

static CELL find_word(char* s, uint8_t slen, RAMC* addr, bool *immediate, bool *primitive) {
  CELL fidx = dict->last_word_idx;
  CELL prev = fidx;
  uint8_t wlen;

  while (fidx != 0) {
    if (addr != 0) *addr = prev;
    prev = tbforth_dict[fidx++];
    wlen = tbforth_dict[fidx++]; 
    if (immediate) *immediate = (wlen & IMMEDIATE_BIT) ? 1 : 0;
    if (primitive) *primitive = (wlen & PRIM_BIT) ? 1 : 0;
    wlen &= WORD_LEN_BITS;
    if (wlen == slen && strncmp(s, (char*)(tbforth_dict+fidx), wlen) == 0) {
      fidx += (wlen / sizeof(CELL)) + (wlen % sizeof(CELL));
      return fidx;
    }
    fidx = prev;
  }
  return 0;
}

tbforth_stat tbforth_interpret(const char *str_in) {
  char *str = (char*)str_in;
  tbforth_stat stat;
  char *word;
  CELL wd_idx;
  bool immediate = 0;
  bool primitive = 0;

  CLEAR_TIB();
  tbforth_iram->tibclen = min(TIB_SIZE, strlen(str)+1);
  memcpy(tbforth_iram->tib, str, tbforth_iram->tibclen);
  while(*(word = tbforth_next_word()) != 0) {
    wd_idx = find_word(word, tbforth_iram->tibwordlen, 0, &immediate, &primitive);
    switch (tbforth_iram->state) {
    case 0: /* interpret mode */
      if (wd_idx == 0) { /* number or trash */
        RAMC num = parse_num(word, tbforth_uram->base);
        if (num == 0 && word[0] != '0') {
          tbforth_abort_request(ABORT_NAW);
          tbforth_abort(0);
          return E_NOT_A_WORD;
        }
        dpush(num);
      } else {
        stat = exec(wd_idx, primitive, tbforth_uram->ridx-1);
        if (stat != U_OK) {
          tbforth_abort(0);
          tbforth_abort_clr();
          return stat;
        }
      }
      break;
    case COMPILING: /* in the middle of a colon def */
      if (wd_idx == 0) { /* number or trash */
        RAMC num = parse_num(word, tbforth_uram->base);
        if (num == 0 && word[0] != '0') {
          tbforth_abort_request(ABORT_NAW);
          tbforth_abort(0);
          dict_end_def();
          return E_NOT_A_WORD;
        }
        dict_append(DLIT);
        dict_append(((uint32_t)num)>>16);
        dict_append(((uint16_t)num)&0xffff);
      } else if (word[0] == ';') { /* exit from a colon def */
        tbforth_iram->state = 0;
        dict_append(EXIT);
        dict_end_def();
        tbforth_iram->compiling_word = 0;
      } else if (immediate) { /* run immediate word */
        stat = exec(wd_idx, primitive, tbforth_uram->ridx-1);
        if (stat != U_OK) {
          tbforth_abort_request(ABORT_ILLEGAL);
          tbforth_abort(0);
          dict_end_def();
          return stat;
        }
      } else { /* just compile word */
        if (primitive) {
          dict_append(tbforth_dict[wd_idx]);
        } else {
          if (tbforth_dict[wd_idx] != EXIT) {
            if (wd_idx == tbforth_iram->compiling_word) {
              dict_append(LIT);
              dict_append(tbforth_iram->compiling_word);
              dict_append(JMP);
            } else {
              dict_append(wd_idx);
            }
          }
        }
      }
      break;
    }
  }
  return U_OK;
}
