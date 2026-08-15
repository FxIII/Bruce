#include "tbforth_core.h"
#include "tbforth.h"
#include <Arduino.h>

static const char * const _tbcore[] = {

    // ── Stack Utilities ────────────────────────────────────────────────────────
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

    // ── Arithmetic & Logical ───────────────────────────────────────────────────
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

    // ── Memory Operations ──────────────────────────────────────────────────────
    ": +! dup >r @ + r> ! ;",
    ": incr 1 swap +! ;",
    ": decr -1 swap +! ;",
    ": mod over swap dup >r / r> * - ;",
    ": depth uram 3 + @ 1 + ;",

    // ── Dictionary / RAM Constants ─────────────────────────────────────────────
    ": CELL 4 ;",
    ": CELL+ CELL + ;",
    ": dversion 0 dict@ ;",
    ": wordsize 1 dict@ ;",
    ": maxdict 2 dict@ ;",
    ": _here 3 ;",
    ": lwa 4 ;",

    // ── iram / uram Inspection ─────────────────────────────────────────────────
    ": iram ram 0 + ;",
    ": compiling? 0 iram + @ ;",
    ": ramsize 1 iram + @ ;",
    ": compiling-word-addr 2 iram + @ ;",
    ": uram-size uram 0 + @ ;",
    ": base uram 1 + ;",
    ": sidx uram 3 + @ ;",
    ": ridx uram 4 + @ ;",
    ": dslen uram 5 + @ ;",
    ": rslen uram 6 + @ ;",
    ": dsa uram 7 + ;",

    // ── Control Flow (Immediate) ───────────────────────────────────────────────
    ": begin here ; immediate",
    ": until 1 , , 18 , ; immediate",
    ": again 1 , , 17 , ; immediate",
    ": if 1 , here 0 , 18 , ; immediate",
    ": then >r here r> dict! ; immediate",
    ": else >r here 3 + r> dict! 1 , here 0 , 17 , ; immediate",
    ": while 1 , here 0 , 18 , ; immediate",
    ": repeat >r 1 , , 17 , here r> dict! ; immediate",
    ": exit 20 , ; immediate",

    // ── Derived Utilities ──────────────────────────────────────────────────────
    ": abs dup <0 if negate then ;",
    ": dup? dup 0= 0= if dup then ;",
    ": .s depth dup if dup 0 do dup i - pick . loop then drop ;",
    ": type 0 begin 2dup +c@ dup 0= if drop drop drop exit then emit 1+ again ;",

    // ── Tick & Compile-Time Helpers ───────────────────────────────────────────
    ": ' next-word (find) ;",
    ": ['] next-word (find) 1 , , ; immediate",
    ": [compile] postpone ['] ['] , , ; immediate",

    // ── Word Creation & Allocation ────────────────────────────────────────────
    ": create _create [compile] lit here 2+ , [compile] ; ;",
    ": variable _create [compile] lit (allot1) , [compile] ; ;",
    ": constant _create [compile] lit , [compile] ; ;",
    ": dconstant _create [compile] dlit d, [compile] ; ;",

    // ── Counted Loops (for/next & do/loop ToolboxForth 4.08) ───────────────────
    ": for [compile] 1- [compile] >r here ; immediate",
    ": next [compile] r> [compile] 1- [compile] dup [compile] >r [compile] 0< [compile] lit , [compile] 0jmp? [compile] r> [compile] drop ; immediate",

    "variable _leaveloop",
    ": do [compile] swap [compile] >r [compile] >r here ; immediate",
    ": i [compile] lit 0 , [compile] rpick ; immediate",
    ": j [compile] lit 2 , [compile] rpick ; immediate",
    ": leave [compile] lit here _leaveloop ! 0 , [compile] jmp ; immediate",

    ": +loop [compile] r> [compile] + [compile] dup [compile] >r [compile] lit 1 , [compile] rpick [compile] >= [compile] lit , [compile] 0jmp? _leaveloop @ 0 > if here _leaveloop @ dict! 0 _leaveloop ! then [compile] r> [compile] r> [compile] drop [compile] drop ; immediate",
    ": loop [compile] lit 1 , postpone +loop ; immediate",
    ": unloop [compile] r> [compile] r> [compile] drop [compile] drop ; immediate",

    // ── Defer & Aliases ────────────────────────────────────────────────────────
    ": defer _create [compile] lit (allot1) , [compile] @ [compile] exec [compile] ; ;",
    ": is compiling? if [compile] lit postpone ' , [compile] 1+ [compile] @ [compile] ! else ' 1+ dict@ ! then ; immediate",
    "defer ed",

    ": allot 0 do (allot1) drop loop ;",
    "variable pad 160 4 / allot",
    "variable _delim",

    // ── Native C++ Aliases for Bruce ───────────────────────────────────────────
    ": cls br.display.cls ;",
    ": write-line br.display.writeLine ;",
    ": draw-cursor br.display.drawCursor ;",
    ": block-read br.block.read ;",
    ": block-write br.block.write ;",
    ": include br.file.include ;",
};

void tbforth_load_core(void) {
    size_t count = sizeof(_tbcore) / sizeof(_tbcore[0]);
    for (size_t i = 0; i < count; i++) {
        tbforth_interpret(_tbcore[i]);
    }
}
