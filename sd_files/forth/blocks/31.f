\ Page 0 - Remote State & Loader
marker -tv-nav use ir use display
20 load
string s-id "ID: 000 [TX]"
variable tv-id variable tv-buf 16 allot
: tv-load ( id -- )
  dup 0 max 199 min tv-id !
  tv-id @ 7 lshift tv-buf 31 rot 128 block-readb drop ;
: tv-cmd ( slot -- )
           tv-buf + ir@ ir-tx drop           30 ms ;
: do-pwr  0 tv-cmd ; : do-vol+ 1 tv-cmd ;
: do-vol- 2 tv-cmd ; : do-mute 3 tv-cmd ;
: do-ch+  4 tv-cmd ; : do-ch-  5 tv-cmd ;
: do-num  ( c -- c ) dup 48 - 6 + tv-cmd ;
: do-exit ;
: tx-ing 32 * s-id 7 +c!  ;
\ Page 1 - Strings & UI Display
string s-hdr "      === TV BROWSER ==="
string s-nav "[ / ] : Prev/Next TV"
string s-vol "LF/RG : Vol -/+"
string s-ch  "UP/DN : Ch  +/-"
string s-mut "m / ' : Mute | p: Pwr"
string s-num "0 - 9 : Digits"
string s-esc "ESC / ` : Exit"

: sid tv-id @
  dup 10 mod 48 + s-id 6 +c! 10 /
  dup 10 mod 48 + s-id 5 +c! 10 /
  10 mod 48 + s-id 4 +c! s-id ;



\ Page 2 - 
: tv-show ( -- )
  cls 2 disp-font!
  s-hdr 0 disp-write-ml  sid   16 disp-write-ml
  s-nav 32 disp-write-ml s-vol 48 disp-write-ml
  s-ch  64 disp-write-ml s-mut 80 disp-write-ml
  s-num 96 disp-write-ml s-esc 112 disp-write-ml ;
: tv-next tv-id @ 1+ tv-load tv-show ;
: tv-prev tv-id @ 1- tv-load tv-show ;







\ Page 3 - Keymap Bindings & Main
32 k-map tv-keys tv-keys k-use
91  ' tv-prev  k-bind  93  ' tv-next  k-bind
218 59 ' do-ch+  k-2bind  217 46 ' do-ch-  k-2bind
215 44 ' do-vol- k-2bind  216 47 ' do-vol+ k-2bind
109 39 ' do-mute k-2bind  112 8  ' do-pwr   k-2bind
48 57 ' do-num k-range
96    ' do-exit k-binde
27    ' do-exit k-binde
: tv-browser
  13 tv-load tv-show
  begin key k-do until cls ;

tv-browser -tv-nav


