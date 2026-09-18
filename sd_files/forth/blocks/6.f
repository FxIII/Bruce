\\ Page 0 - Setup & Var - 1
marker -- use display 0 constant false 1 constant true
2 disp-font! : cols disp-cols ; : showline disp-write-ml ;
: >2d dup 10 / 48 + swap 10 mod 48 + ;
variable buf 127 allot variable cur variable ord 1 allot
variable vblk variable page variable vline variable vscr
variable vact
: laddr 8 * buf + ;








\\ Page 1 - CB Router - 17
variable cbs 19 allot variable ccurr
: ccurr@ ccurr @ ; : ccurr+ 1 ccurr +! ;
: vc-ret@ cbs + 5 +c@ ;
: vc-c1@ cbs + 7 +c@ ; : vc-c2@ cbs + 6 +c@ ;
: vc-cb! ccurr@ cbs + ! ccurr@ cbs + 5 +c! ccurr@ cbs + 6 +c!
  ccurr@ cbs + 7 +c! ccurr+ ;
: vc-chor swap >r dup vc-c1@ r@ = swap vc-c2@ r@ = or r> swap ;
: vc-btwn swap >r dup vc-c1@ r@ <=
  swap vc-c2@ r@ >= and r> swap ;
: vc-cb dup >r cbs + @ 0xFFFFFFFF and exec r> vc-ret@ ;





\\ Page 2 - Line Prim - 33
: cur+ cur @ 1+ cur ! ;     : cur- cur @ 1- cur ! ;
: c@cur pad cur @ +c@ ;     : c!cur pad cur @ +c! ;
: pleft  cur @ 0> if cur- then ;
: pright c@cur 0<> cur @ 63 < and if cur+ then ;
: pput c@cur >r c!cur cur+
  r> 0= cur @ 64 < and if 0 c!cur then ;
: pback cur @ 0= if exit then
  c@cur >r cur- r> 0= if 0 c!cur else 32 c!cur then ;
: >pad 64 0 do 0 pad i +c! loop laddr
  64 0 do dup i +c@ pad i +c! loop drop 0 cur ! ;
: pad> laddr 64 0 do pad i +c@ over i +c! loop drop ;
: cursor cur @ cols mod cur @ cols / disp-cursor ;



\\ Page 3 - Key Handle - 49
ccurr@ constant hk-from
 13  27 1 ' nop   vc-cb!  212   8 0 ' pback vc-cb!
216 216 0 ' pleft vc-cb!  215 215 0 ' pright vc-cb!
ccurr@ constant hk-to  32 126 0 ' pput  vc-cb!

: handle-key
  hk-to hk-from do i vc-chor
  if drop i vc-cb unloop exit then loop
  hk-to vc-btwn if hk-to vc-cb exit then drop false ;






\\ Page 4 - Edit & IO - 65
: pedit begin pad 0 showline cursor key handle-key until ;
: ledit dup >pad cls pedit pad> ;

: lst laddr type ;
: list 16 0 do i . i lst cr loop ;

: read buf swap block-read ;
: write buf swap block-write ;

: readp buf rot rot block-readp ;
: writep buf rot rot block-writep ;




\\ Page 5 - Order & Bar - 81
: >ord 16 0 do i 1+ 10 * ord i +c! loop ;
: _sert >ord ord swap +c! ord buf reorder ;
: insert 10 * 5 + 15 _sert ;
: outsert 200 swap _sert ;
: .ord 16 0 do ord i +c@ . i 1+ 4 mod 0= if cr then loop ;

string sbar "B:00 P:00 L:00  [H]elp"
: vstatus
  vblk @ >2d sbar 3 +c! sbar 2 +c!
  page @ >2d sbar 8 +c! sbar 7 +c!
  vline @ >2d sbar 13 +c! sbar 12 +c!
  sbar disp-rows 1- disp-row-height * showline ;



\\ Page 6 - Rendering - 97
: lrow dup if 1- ord swap +c@ else drop 0 then ;
: >vord 0 16 0 do i laddr disp-line-rows +
  dup ord i +c! loop drop ;

: vscr! vline @ lrow dup vscr @ < if vscr ! exit then drop
  ord vline @ +c@ 1-
  dup vscr @ 9 + >= if 9 - vscr ! else drop then ;

: vdraw cls >vord vscr! 16 0 do ord i +c@ 1- vscr @ -
  dup 0 >= if i lrow vscr @ - disp-row-height * i laddr
  swap disp-write-ml then disp-rows 1- >=
  if unloop vstatus exit then loop vstatus ;

: vcursor 0 vline @ lrow vscr @ - disp-cursor ;

\\ Page 7 - Controls - 113
: vc-reset 0 vline ! 0 vscr ! ; : vc-save vblk @ page @ writep ;
: vc-read  vblk @ page @ readp ; : vc-edit vline @ ledit ;
: vc-up    vline @ 0> if -1 vline +! then ;
: vc-down  vline @ 15 < if 1 vline +! then ;
: vc-next  1 page +! vc-reset vc-read ;
: vc-prev  page @ 0> if -1 page +! vc-reset vc-read then ;
: lclear laddr 8 0 do 0 over i + ! loop drop ;
: lswap  swap laddr swap laddr pad 64 mem-swap ;
: vc-lup   vline @ if vline @ dup 1- lswap -1 vline +! then ;
: vc-ldown vline @ 15 <
  if vline @ dup 1+ lswap 1 vline +! then ;
: vc-lnew  vline @ insert ;
: vc-lkill vline @ 15 = if 15 lclear else vline @ outsert then ;
: vc-xline 1 vact ! ; : vc-xpage 2 vact ! ; : vc-xblk 3 vact ! ;

\\ Page 8 - Help Screen - 129
string ht0 "=== FORTH BLOCK EDIT ==="
string ht1 "UP/DN (; .) or U/D Shift"
string ht2 "LF/RG (, /)     Page -/+"
string ht3 "ENTER           Edit line"
string ht4 "N / K           New/Kill"
string ht5 "L/P/B           Run L/P/B"
string ht6 "S / H / ESC     Save/Help/Exit"
string htp "--- press any key ---"

: vc-help
  cls ht0 0 showline ht1 16 showline
  ht2 32 showline ht3 48 showline ht4 64 showline
  ht5 80 showline ht6 96 showline htp 112 showline key drop ;


\\ Page 9 - Key Dispatch - 145
ccurr@ constant vk-from
 177 96 1 ' nop     vc-cb!  218 59 0 ' vc-up    vc-cb!
 217 46 0 ' vc-down vc-cb!  215 47 0 ' vc-next  vc-cb!
 216 44 0 ' vc-prev vc-cb!   13 13 0 ' vc-edit  vc-cb!
 115 83 0 ' vc-save vc-cb!  104 72 0 ' vc-help  vc-cb!
 117 85 0 ' vc-lup  vc-cb!  100 68 0 ' vc-ldown vc-cb!
 110 78 0 ' vc-lnew vc-cb!  107 75 0 ' vc-lkill vc-cb!
 108 76 1 ' vc-xline vc-cb! 112 80 1 ' vc-xpage vc-cb!
  98 66 1 ' vc-xblk vc-cb!  ccurr@ constant vk-to

: vkey
  vk-to vk-from do i vc-chor
  if drop i vc-cb unloop exit then loop drop false ;


\\ Page 10 - Viewer Loop - 161
: veval vact @ 0= if exit then cls
  vact @ 1 = if vline @ laddr eval-line then
  vact @ 2 = if buf eval-page then
  vact @ 3 = if vblk @ load then
  disp-refresh htp 112 showline key drop ;

: vloop begin vdraw vcursor key vkey until ;

: view vblk ! 0 page ! vc-reset vc-read
  begin 0 vact ! vloop veval vact @ 0= until ;





