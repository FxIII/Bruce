\ Page 0 - Setup & Strings
marker -tv use ir use display 20 load
string t-hdr "=== TV REMOTE ==="
string t-pwr "DEL: Power OFF"
string t-ch  "UP/DN: Prog +/-"
string t-vol "LF/RG: Vol  +/-"
string t-mut "': Mute"
string t-esc "ESC: Exit"
string s-rdy "Status: Ready"
string s-tx  "Status: Sending..."
: status 100 disp-write-ml ;
: zap-nec  ir-nec  ir-tx drop 30 ms ;
: zap-rca  ir-rca  ir-tx drop 30 ms ;
: zap-sony ir-sony ir-tx drop 30 ms ;
: zap-rc5  ir-rc5  ir-tx drop 30 ms ;
: zap-sam  ir-samsung ir-tx drop 30 ms ;
\ Page 1 - Swipe Actions

: do-pwr s-tx status
 15 -1 42 zap-rca 4 -1 8 zap-nec 7  7  2 zap-sam 
 1 -1 21 zap-sony 0 -1 12 zap-rc5  s-rdy status ;

: do-ch- s-tx status
 15 -1 44 zap-rca 4 -1 1 zap-nec 7 7 19 zap-sam 1 -1 17 zap-sony
 0 -1 33 zap-rc5 s-rdy status ;

: do-vol+ s-tx status
 15 -1 47 zap-rca 4 -1 2 zap-nec 7 7 7 zap-sam 1 -1 18 zap-sony
 0 -1 16 zap-rc5 s-rdy status ;



\ Page 2 - Swipe Actions
: do-vol- s-tx status
 15 -1 46 zap-rca 4 -1 3 zap-nec 7 7 11 zap-sam 1 -1 19 zap-sony
 0 -1 17 zap-rc5 s-rdy status ;

: do-mute s-tx status
 15 -1 63 zap-rca 4 -1 9 zap-nec 7 7 15 zap-sam 1 -1 20 zap-sony
 0 -1 13 zap-rc5 s-rdy status ;

: do-ch+ s-tx status
 15 -1 45 zap-rca 4 -1 0 zap-nec 7 7 18 zap-sam 1 -1 16 zap-sony
 0 -1 32 zap-rc5 s-rdy status ;




\ Page 2 - Swipe Actions
: do-digit ( c -- c )
  s-tx status
  dup 15 -1 rot zap-rca                 
  dup 48 - >r                           
  4 -1 r@ dup if 15 + else drop 25 then zap-nec 
  7 7  r@ dup if  3 + else drop 17 then zap-sam 
  1 -1 r@ dup if  1 - else drop  9 then zap-sony
  0 -1 r> zap-rc5
  s-rdy status ;






\ Page 3 - Keys & Remote UI
16 k-map tv-keys tv-keys k-use
218 59 ' do-ch+  k-2bind  217 46 ' do-ch-  k-2bind
215 47 ' do-vol+ k-2bind  216 44 ' do-vol- k-2bind
39     ' do-mute k-bind   8      ' do-pwr   k-bind
48 57 ' do-digit k-range 96     ' do-exit k-binde
: tv-ui cls 2 disp-font!
  t-hdr 0 disp-write-ml   t-pwr 16 disp-write-ml
  t-ch 32 disp-write-ml   t-vol 48 disp-write-ml
  t-mut 64 disp-write-ml  t-esc 80 disp-write-ml
  s-rdy status ;
: remote tv-ui begin key k-do until cls -tv ;





