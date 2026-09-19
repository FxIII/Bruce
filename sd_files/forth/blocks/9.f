\ Page 1 - Private data
variable kbuf 0 kbuf !       : kcurr@ kbuf @ @ ;
: kcurr+ 1 kbuf @ +! ;       : kslot  kbuf @ 1+ + ;
: kflg   kslot 4 +c@ ;       : kret   kslot 5 +c@ ;
: kc2    kslot 6 +c@ ;       : kc1    kslot 7 +c@ ;
: kcb    dup >r kslot @ 0xFFFFFFFF and exec r> kret ;
: kchor  swap >r dup kc1 r@ = swap kc2 r@ = or r> swap ;
: kbtwn  swap >r dup kc1 r@ <= swap kc2 r@ >= and r> swap ;
: kcheck dup kflg if kbtwn else kchor then ;

: kcb! ( c1 c2 ret flag cb -- )
  kcurr@ kslot !     kcurr@ kslot 4 +c!   kcurr@ kslot 5 +c!
  kcurr@ kslot 6 +c! kcurr@ kslot 7 +c!   kcurr+ ;



\ Page 2 - Public API
: k-map  variable allot ;
: k-use  kbuf ! ;
: k-init 0 kbuf @ ! ;

: k-bind  >r dup 0 0 r> kcb! ;
: k-binde >r dup 1 0 r> kcb! ;
: k-2bind >r 0 0 r> kcb! ;
: k-range >r 0 1 r> kcb! ;

: k-do ( key -- exit? )
  kcurr@ 0 do
    i kcheck if i kcb nip unloop exit then
  loop drop 0 ;
