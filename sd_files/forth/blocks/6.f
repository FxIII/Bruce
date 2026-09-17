\ PAGE 0 - General
marker -- 0 use display constant false 1 constant true
variable buf 127 allot variable cur
: cur+ cur @ 1 + cur ! ;  : cur- cur @ 1 - cur ! ;
: c@cur pad cur @ +c@ ;   : c!cur pad cur @ +c! ;
: pleft  cur @ 0 > if cur- then ;
: pright c@cur 0 <> cur @ 63 < and if cur+ then ;
: pput c@cur >r c!cur cur+
   r> 0 = cur @ 64 < and if 0 c!cur then ;
: pback cur @ 0= if exit then
   c@cur >r cur- r> 0= if 0 c!cur else 32 c!cur then ;
: >pad 64 0 do 0 pad i +c! loop 8 * buf +
  64 0 do dup i +c@ pad i +c! loop drop  0 cur ! ;
: pad> 8 * buf + 64 0 do pad i +c@ over i +c! loop drop ;
: cols disp-cols ; : showline disp-write-ml ;

\ PAGE 1 - Line Editor - 17
: handle-key
  dup  13 = over  27 = or   if drop         true exit then
  dup   8 = over 212 = or   if drop pback  false exit then
  dup 216 =                 if drop pleft  false exit then
  dup 215 =                 if drop pright false exit then
  dup 32 >= over 126 <= and if      pput   false exit then
  drop false ;
: cursor cur @ cols mod  cur @ cols / disp-cursor ;
: pedit begin pad 0 showline cursor key handle-key until ;
: lst 8 * buf + type ; : list 16 0 do i . i lst cr loop ;
: read buf swap block-read ; : write buf swap block-write ;
: ledit dup >pad cls pedit pad> ;
: readp buf rot rot block-readp ;
: writep buf rot rot block-writep ;

\ PAGE 2 - Line Management - 33
2 disp-font!
variable ord 1 allot
: >ord 16 0 do i 1+ 10 * ord i +c! loop ;
: _sert >ord ord swap +c! ord buf reorder ;
: insert 10 * 5 + 15 _sert ;
: outsert 200 swap _sert ;
: .ord 16 0 do ord i +c@ . i 1+ 4 mod 0= if cr then loop ;
variable vblk variable page variable vline variable vscr
string sbar "B:00 P:00 L:00  [H]elp"
: s!2d dup 10 / 48 + swap 10 mod 48 + ;
: vstatus
  vblk @ s!2d sbar 3 +c! sbar 2 +c!
  page @ s!2d sbar 8 +c! sbar 7 +c!
  vline @ s!2d sbar 13 +c! sbar 12 +c!
  sbar disp-rows 1- disp-row-height * showline ;
\ PAGE 3 - Viewer Helpers -  49
: laddr 8 * buf + ;
: lrow dup if 1- ord swap +c@ else drop 0 then ;
: >vord 0 16 0 do i laddr disp-line-rows +
  dup ord i +c! loop drop ;
: vscr! vline @ lrow
  dup vscr @ < if vscr ! exit then drop
  ord vline @ +c@ 1-
  dup vscr @ 9 + >= if 9 - vscr ! else drop then ;
: vdraw cls >vord vscr! 16 0 do ord i +c@ 1- vscr @ -
  dup 0 >= if i lrow vscr @ - disp-row-height * i laddr
  swap disp-write-ml then disp-rows 1- >=
  if unloop vstatus exit then loop vstatus ;
: vcursor 0 vline @ lrow vscr @ - disp-cursor ;


\ PAGE 4 - Viewer Controls - 65
: vc-reset 0 vline ! 0 vscr ! ;
: vc-read  vblk @ page @ readp ;
: vc-up    vline @ 0 > if -1 vline +! then ;
: vc-down  vline @ 15 < if 1 vline +! then ;
: vc-edit  vline @ ledit ;
: vc-next 1 page +! vc-reset vc-read ;
: vc-prev page @ 0 > if -1 page +! vc-reset vc-read then ;
: vc-save vblk @ page @ writep ;







\ PAGE 5 - Help Screen - 81
string ht0 "=== FORTH BLOCK EDIT ==="
string ht1 "UP/DOWN or ;/.  Move line"
string ht2 "LEFT/RG or ,//  Prev/Next"
string ht3 "ENTER           Edit line"
string ht4 "S               Save SD"
string ht5 "H               Help"
string ht6 "ESC or ~        Exit"
string htp "--- press any key ---"
: vc-help
  cls
  ht0  0 showline
  ht1 16 showline  ht2 32 showline
  ht3 48 showline  ht4 64 showline
  ht5 80 showline  ht6 96 showline
  htp 112 showline key drop ;
\ PAGE 6 - Viewer - 97
variable cbs 7 allot
: vc-cb! swap >r r@ cbs + ! r@ cbs + 5 +c!
  r@ cbs + 6 +c! r@ cbs + 7 +c! r> drop ;
: vc-ret@ cbs + 5 +c@ ;
: vc-c1@ cbs + 7 +c@ ;  : vc-c2@ cbs + 6 +c@ ;
: vc-cb dup cbs + @ 0xFFFFFFFF and exec vc-ret@ ;

177 96 1 0 ' nop     vc-cb!   218 59 0 1 ' vc-up   vc-cb!
217 46 0 2 ' vc-down vc-cb!   215 47 0 3 ' vc-next vc-cb!
216 44 0 4 ' vc-prev vc-cb!    13 13 0 5 ' vc-edit vc-cb!
115 83 0 6 ' vc-save vc-cb!   104 72 0 7 ' vc-help vc-cb!
: vkey 8 0 do dup i vc-c1@ = over i vc-c2@ = or
   if i vc-cb unloop exit then loop false ;
: view vblk ! 0 page ! vc-reset vc-read begin
   vdraw vcursor key vkey swap drop until ;
\ PAGE 7 - Reserved for Line Ops - 113















