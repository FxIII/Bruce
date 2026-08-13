#include "uforth_core.h"
#include "uforth.h"
#include <Arduino.h>

// Raw opcode values (from uforth.c enum — must stay in sync):
//   LIT=1  JMP=17  JMP_IF_ZERO=18  EXIT=20  COMMA=34
//
// Definitions longer than TIB_SIZE (128) are split across multiple strings;
// compiler state (uforth_iram->compiling) persists between uforth_interpret calls.

static const char * const _core[] = {

    // ── Stack ─────────────────────────────────────────────────────────────────
    ": dup 0 pick ;",
    ": over 1 pick ;",
    ": swap >r >r 1 rpick r> r> drop ;",
    ": rot >r swap r> swap ;",
    ": nip swap drop ;",
    ": tuck swap over ;",
    ": 2dup over over ;",
    ": 2drop drop drop ;",
    ": r@ 1 rpick ;",
    ": rdrop r> r> drop >r ;",

    // ── Arithmetic ─────────────────────────────────────────────────────────────
    ": nop ;",
    ": 1+ 1 + ;",
    ": 1- 1 - ;",
    ": 2+ 2 + ;",
    ": 2* 2 * ;",
    ": negate invert 1 + ;",
    ": not 0= ;",
    ": 0< <0 ;",

    // ── Comparison ─────────────────────────────────────────────────────────────
    ": = - 0= ;",
    ": <> - 0= invert ;",
    ": < - <0 ;",
    ": > swap < ;",
    ": 0> 0 > ;",
    ": >= - <0 invert ;",
    ": <= swap >= ;",
    ": min 2dup > if swap then drop ;",
    ": max 2dup < if swap then drop ;",

    // ── Memory utilities ───────────────────────────────────────────────────────
    ": +! dup >r @ + r> ! ;",
    ": incr 1 swap +! ;",
    ": decr -1 swap +! ;",
    ": mod over swap dup >r / r> * - ;",
    ": depth uram 2 + @ 1 + ;",

    // ── Dictionary / RAM constants ─────────────────────────────────────────────
    ": CELL 4 ;",
    ": CELL+ CELL + ;",
    ": dversion 0 dict@ ;",
    ": wordsize 1 dict@ ;",
    ": maxdict 2 dict@ ;",
    ": _here 3 ;",
    ": lwa 4 ;",

    // ── iram / uram introspection ──────────────────────────────────────────────
    ": iram ram 0 + ;",
    ": compiling? 0 iram + @ ;",
    ": ramsize 1 iram + @ ;",
    ": compiling-word-addr 2 iram + @ ;",
    ": uram-size uram 0 + @ ;",
    ": base uram 1 + ;",
    ": sidx uram 2 + @ ;",
    ": ridx uram 3 + @ ;",
    ": dslen uram 4 + @ ;",
    ": rslen uram 5 + @ ;",
    ": dsa uram 6 + ;",
    ": rsa dsa dslen + ;",
    ": tos dsa sidx + ;",

    // ── Control flow (immediate) ───────────────────────────────────────────────
    // Raw opcode values: LIT=1, JMP=17, JMP_IF_ZERO=18, EXIT=20
    ": begin here ; immediate",
    ": until 1 , , 18 , ; immediate",
    ": again 1 , , 17 , ; immediate",
    ": if 1 , here 0 , 18 , ; immediate",
    ": then >r here r> dict! ; immediate",
    ": else >r here 3 + r> dict! 1 , here 0 , 17 , ; immediate",
    ": while 1 , here 0 , 18 , ; immediate",
    ": repeat >r 1 , , 17 , here r> dict! ; immediate",
    ": exit 20 , ; immediate",

    // ── Derived ────────────────────────────────────────────────────────────────
    ": abs dup <0 if negate then ;",
    ": dup? dup 0= 0= if dup then ;",
    ": .s depth dup if dup 0 do dup i - pick . loop then drop ;",
    ": type 0 begin 2dup +c@ dup 0= if drop drop drop exit then emit 1+ again ;",

    // ── Tick / compile-time helpers ────────────────────────────────────────────
    // ['] uses the "LIT is 1" hack: (find) returns opcode value for primitives,
    // so compiling literal 1 followed by the token's execution token pushes it.
    ": ' next-word (find) ;",
    ": ['] next-word (find) 1 , , ; immediate",
    ": [compile] postpone ['] ['] , , ; immediate",

    // ── create / variable / constant ──────────────────────────────────────────
    // Pattern: _create makes the word header; [compile] ; compiles EXIT, and
    // the second `;` (consumed by [compile]) and outer `;` end the definition.
    ": create _create [compile] lit here 2+ , [compile] ; ;",
    ": variable _create [compile] lit _allot1 , [compile] ; ;",
    ": constant _create [compile] lit , [compile] ; ;",
    ": dconstant _create [compile] dlit d, [compile] ; ;",

    // ── do / loop ─────────────────────────────────────────────────────────────
    // do ( limit start -- ) compiles: swap >r >r, leaves loop-start addr on stack.
    // i / j peek rstack for current / outer loop index.
    // leave reserves a forward-jump placeholder; +loop patches it after the loop.
    // Note: `0 do ... loop` (limit=0) is not supported — loops are entered at least once.
    "variable _leaveloop",
    ": do [compile] swap [compile] >r [compile] >r here ; immediate",
    ": i [compile] lit 0 , [compile] rpick ; immediate",
    ": j [compile] lit 2 , [compile] rpick ; immediate",
    ": leave [compile] lit here _leaveloop ! dummy, [compile] jmp ; immediate",

    // +loop split across 4 strings (each < 128 chars):
    ": +loop [compile] r> [compile] + [compile] dup [compile] >r [compile] lit 1 , [compile] rpick",
    "[compile] swap [compile] - [compile] 0= [compile] lit , [compile] 0jmp?",
    "_leaveloop @ 0 > if here _leaveloop @ dict! 0 _leaveloop ! then",
    "[compile] r> [compile] r> [compile] drop [compile] drop ; immediate",

    ": loop [compile] lit 1 , postpone +loop ; immediate",
    ": unloop [compile] r> [compile] r> [compile] drop [compile] drop ; immediate",

    // ── defer / is ────────────────────────────────────────────────────────────
    // defer creates an indirection word: stores a function pointer in RAM,
    // fetches and executes it at call time.
    ": defer _create [compile] lit _allot1 , [compile] @ [compile] exec [compile] ; ;",
    ": is compiling? if [compile] lit postpone ' , [compile] 1+ [compile] @ [compile] ! else ' 1+ dict@ ! then ; immediate",
    "defer ed",

    // ── allot / word ──────────────────────────────────────────────────────────
    ": allot 0 do _allot1 drop loop ;",
    "variable pad 160 4 / allot",
    "variable _delim",
    ": word _delim ! 0 pad ! begin next-char dup _delim @ = over 0= or if drop pad exit then pad c!+ again ;",

    // ── Aliases ────────────────────────────────────────────────────────────────
    ": cls br.display.cls ;",
    ": write-line br.display.writeLine ;",
    ": set-char br.display.setChar ;",
    ": set-len br.display.setLen ;",
    ": draw-cursor br.display.drawCursor ;",
    ": block-load br.block.load ;",
    ": block-save br.block.save ;",

    nullptr
};

// ── Optional self-test ────────────────────────────────────────────────────────
// Uncomment the next line (or pass -DUFORTH_CORE_TEST) to compile in `test`.
// #define UFORTH_CORE_TEST

#ifdef UFORTH_CORE_TEST
#include "natives/natives.h"

static int _tn_ok   = 0;
static int _tn_fail = 0;

// ( flag n -- )  prints "Tn ok" / "Tn FAIL"
static void _fn_assert() {
    DCELL n    = dpop();
    DCELL flag = dpop();
    char buf[32];
    if (flag) { _tn_ok++;   snprintf(buf, sizeof(buf), "T%d ok\n",   (int)n); }
    else       { _tn_fail++; snprintf(buf, sizeof(buf), "T%d FAIL\n", (int)n); }
    forth_output(buf);
}

static const char * const _tests[] = {
    // ── Stack ─────────────────────────────────────────────────────────────────
    "1 dup + 2 = 1 _assert",
    ": _tov 1 2 over + + 4 = ; _tov 2 _assert",
    "1 2 swap 1 = swap 2 = and 3 _assert",
    ": _trot 1 2 3 rot 1 = rot 2 = rot 3 = and and ; _trot 4 _assert",
    "1 2 nip 2 = 5 _assert",
    ": _ttuck 1 2 tuck + + 5 = ; _ttuck 6 _assert",
    ": _t2dup 1 2 2dup + + + 6 = ; _t2dup 7 _assert",
    ": _tr@ 5 >r r@ r> = ; _tr@ 8 _assert",
    // ── Arithmetic ────────────────────────────────────────────────────────────
    "5 1+ 6 = 9 _assert",
    "5 1- 4 = 10 _assert",
    "5 2+ 7 = 11 _assert",
    "3 2* 6 = 12 _assert",
    "5 negate 5 + 0 = 13 _assert",
    "3 negate abs 3 = 14 _assert",
    // ── Comparisons ───────────────────────────────────────────────────────────
    "3 3 = 15 _assert",
    "3 4 = invert 16 _assert",
    "3 4 <> 17 _assert",
    "3 4 < 18 _assert",
    "4 3 > 19 _assert",
    "0 0= 20 _assert",
    "1 0= invert 21 _assert",
    "3 3 <= 22 _assert",
    "2 3 <= 23 _assert",
    "3 3 >= 24 _assert",
    "4 3 >= 25 _assert",
    "0 not 26 _assert",
    "3 negate 0< 27 _assert",
    "3 0< invert 28 _assert",
    "3 0> 29 _assert",
    // ── Memory ────────────────────────────────────────────────────────────────
    "variable _tv1  42 _tv1 ! _tv1 @ 42 = 30 _assert",
    "99 constant _tc1  _tc1 99 = 31 _assert",
    "variable _tv2  10 _tv2 !  5 _tv2 +!  _tv2 @ 15 = 32 _assert",
    "variable _tv3  10 _tv3 !  _tv3 incr  _tv3 @ 11 = 33 _assert",
    "variable _tv4  10 _tv4 !  _tv4 decr  _tv4 @ 9 = 34 _assert",
    "10 3 mod 1 = 35 _assert",
    // ── Control flow ──────────────────────────────────────────────────────────
    ": _tif  1 if 1 else 0 then ; _tif  36 _assert",
    ": _tif0 0 if 1 else 0 then ; _tif0 0 = 37 _assert",
    ": _tuntil 0 begin 1 + dup 5 = until ; _tuntil 5 = 38 _assert",
    ": _twhile 0 begin dup 5 < while 1 + repeat ; _twhile 5 = 39 _assert",
    ": _tagain 0 begin 1 + dup 3 = if exit then again ; _tagain 3 = 40 _assert",
    // ── do / loop ─────────────────────────────────────────────────────────────
    ": _tdo    0 5 0 do 1 + loop      ; _tdo    5 = 41 _assert",
    ": _tploop 0 10 0 do 1 + 2 +loop  ; _tploop 5 = 42 _assert",
    // ── Misc ──────────────────────────────────────────────────────────────────
    "CELL 4 = 43 _assert",
    // ── defer / is ────────────────────────────────────────────────────────────
    ": _dbody 99 ;",
    "defer _dword",
    "['] _dbody is _dword",
    "_dword 99 = 44 _assert",
    nullptr
};

static void _fn_test() {
    _tn_ok = _tn_fail = 0;
    forth_register("_assert", _fn_assert);

    for (int i = 0; _tests[i]; i++) {
        uforth_stat st = uforth_interpret(_tests[i]);
        if (st != UFORTH_OK) {
            char buf[64];
            snprintf(buf, sizeof(buf), "ERR %d: %.40s\n", (int)st, _tests[i]);
            forth_output(buf);
            uforth_abort();
            _tn_fail++;
        }
    }

    char sum[48];
    snprintf(sum, sizeof(sum), "--- %d ok  %d fail ---\n", _tn_ok, _tn_fail);
    forth_output(sum);
}

#endif /* UFORTH_CORE_TEST */

void uforth_load_core(void) {
    for (int i = 0; _core[i]; i++) {
        uforth_stat st = uforth_interpret(_core[i]);
        if (st != UFORTH_OK) {
            log_e("uforth_load_core: error %d loading: %s", (int)st, _core[i]);
            uforth_abort();
        }
    }
#ifdef UFORTH_CORE_TEST
    forth_register("test", _fn_test);
#endif
}
