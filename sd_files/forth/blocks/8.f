\ Page 0 - Setup & Var - 1
marker -- use display
variable dbg-pc variable dbg-h variable dbg-xt
variable dbg-cur variable dbg-top variable dbg-end
variable nav-d variable dbg-run? variable bp
variable lbuf 64 allot variable nav-stk 16 allot
: nav-push
  dbg-h @ nav-stk nav-d @ 2 * + !
  dbg-cur @ nav-stk nav-d @ 2 * 1 + + !
  1 nav-d +! ;
: nav-pop
  -1 nav-d +!
  nav-stk nav-d @ 2 * + @ dbg-h !
  nav-stk nav-d @ 2 * 1 + + @ dbg-cur ! ;


\ Page 1 - Dict Helpers - 17
: h>len 1+ dict@ 63 and ;
: h>xt dup 2 + swap h>len 1 + 2 / + ;
: pc>h >r 4 dict@ begin dup 0= if r> drop exit then
  r@ over h>xt >= if r> drop exit then dict@ again ;
: xt>h >r 4 dict@ begin dup 0= if r> drop exit then
  dup h>xt r@ = if r> drop exit then dict@ again ;
: prim>h >r 4 dict@ begin dup 0= if r> drop exit then
  dup 1+ dict@ 64 and if dup h>xt dict@ r@ =
  if r> drop exit then then dict@ again ;
: next-h >r 4 dict@ begin dup 0= if r> drop exit then
  dup dict@ r@ = if r> drop exit then dict@ again ;
: word-end dup next-h dup if nip else drop here then ;
: dbg-set-word dup dbg-h ! dup h>xt dup dbg-xt ! dbg-top !
  word-end dbg-end ! ;

\ Page 2 - Sizing & Walk - 33
: inst-len
  dict@ dup 1 = over 17 = or over 18 = or if drop 2 exit then
  2 = if 3 exit then 1 ;
: next-inst dup inst-len + ;
: prev-inst over over = if drop exit then >r begin
  dup next-inst dup r@ >= if drop r> drop exit then
  nip again ;
: /mod over over mod >r / r> swap ;
: c+ lbuf bp @ +c! 1 bp +! ;
: z+ 0 lbuf bp @ +c! ;
: s+ 0 begin over over +c@ dup 0=
  if drop drop drop exit then c+ 1+ again ;



\ Page 3 - Format Buffer - 49
: wname+ dup 1+ dict@ 63 and dup if
  0 do dup 2 + i +dict-c@ c+ loop drop else drop drop then ;
: n+ dup 0= if 48 c+ drop exit then dup <0 if 45 c+ negate then
  0 swap begin dup while 10 /mod swap 48 + swap repeat
  drop begin dup while c+ repeat drop ;
: dlit@ dup 1 + dict@ dup if 16 lshift swap 2 + dict@ or
  else drop 2 + dict@ then ;








\ Page 4 - UI Strings - 65
string s-hdr "DBG: "
string s-lvl " [L"
string s-rb "]"
string s-bar "[;/.]Move [/]In [,]Out [Q]Exit"
string s-lit "lit "
string s-dlit "dlit "
string s-jmp "jmp "
string s-0jmp "0jmp? "
string s-exit "exit"
string s-in " ->"
string s-col ": "




\ Page 5 - Disassembler - 81
: fmt-inst
  0 bp ! if 62 c+ 32 c+ else 32 c+ 32 c+ then
  dup n+ s-col s+ dup dict@
  dup 1 = if drop s-lit s+ 1+ dict@ n+ z+ exit then
  dup 2 = if drop s-dlit s+ dlit@ n+ z+ exit then
  dup 17 = if drop s-jmp s+ 1+ dict@ n+ z+ exit then
  dup 18 = if drop s-0jmp s+ 1+ dict@ n+ z+ exit then
  dup 20 = if drop drop s-exit s+ z+ exit then
  dup 56 > if dup xt>h dup if wname+ drop else
    drop 35 c+ n+ then s-in s+ drop z+ exit then
  dup prim>h dup if wname+ drop else drop 112 c+ n+ then
  drop z+ ;



\ Page 6 - Rendering - 97
: adjust-scroll
  dbg-cur @ dbg-top @ < if dbg-cur @ dbg-top ! exit then
  0 dbg-top @ begin dup dbg-cur @ < while
  next-inst swap 1+ swap repeat drop
  5 >= if dbg-top @ next-inst dbg-top ! then ;
: draw-hdr 0 bp ! s-hdr s+ dbg-h @ wname+ s-lvl s+
  nav-d @ n+ s-rb s+ z+ lbuf 0 disp-write-ml ;
: draw-body dbg-top @ 6 0 do
  dup dbg-end @ < if dup dup dbg-cur @ = fmt-inst
  lbuf i 1+ 16 * disp-write-ml next-inst
  else 0 bp ! z+ lbuf i 1+ 16 * disp-write-ml then loop drop ;
: draw-bar s-bar 112 disp-write-ml ;
: vdraw adjust-scroll draw-hdr draw-body draw-bar ;


\ Page 7 - Navigation - 113
: act-up dbg-xt @ dbg-cur @ prev-inst dbg-cur ! ;
: act-down dbg-cur @ next-inst dup dbg-end @ < if
  dbg-cur ! else drop then ;
: act-in dbg-cur @ dict@ dup 56 > if xt>h dup if
  nav-push dbg-set-word dbg-xt @ dbg-cur ! else drop then
  else drop then ;
: act-out nav-d @ if nav-pop dbg-h @ h>xt dbg-xt !
  dbg-h @ word-end dbg-end ! dbg-cur @ dbg-top ! then ;
: act-quit 0 dbg-run? ! ;






\ Page 8 - Key Dispatch - 129
: dbg-key
  dup 59 = over 218 = or if drop act-up exit then
  dup 46 = over 217 = or if drop act-down exit then
  dup 47 = over 215 = or over 13 = or if drop act-in exit then
  dup 44 = over 216 = or if drop act-out exit then
  dup 113 = over 81 = or over 27 = or if drop act-quit exit then
  drop ;
: dbg-loop begin dbg-run? @ while vdraw key dbg-key repeat ;







\ Page 9 - Initialization - 145
: dbg-init cls 2 disp-font! 0 nav-d ! 1 dbg-run? !
  dbg-pc @ pc>h dup 0= if 0 dbg-run? ! drop exit then
  dbg-set-word dbg-pc @ dbg-cur ! ;
: dbg-clean 0 nav-d ! ;
: dbg-run dbg-init dbg-run? @ if dbg-loop then dbg-clean ;
: dbg dbg-pc ! dbg-run ;









