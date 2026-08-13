\ Block 1: Visual Editor - Commands & REPL Loop
: ins ( c -- ) cy @ L >r r@ 1+ cx @ set-char
  cx @ 1+ r@ @ max r@ set-len 1 0 move rdrop ;
: del ( -- ) -1 0 move 32 cy @ L 1+ cx @ set-char ;
: edk ( c -- )
  dup 0xDA = if drop 0 -1 move exit then
  dup 0xD9 = if drop 0 1 move exit then
  dup 0xD8 = if drop -1 0 move exit then
  dup 0xD7 = if drop 1 0 move exit then
  dup 8 = if drop del exit then
  dup 13 = if drop 0 1 move 0 cx ! exit then
  dup 32 >= over 126 <= and if ins exit then drop ;
: edi ( b -- ) dup blk ! lines-buf swap block-load
  0 cx ! 0 cy ! 0 sy ! 0 sx !
  begin draw key dup 27 <> while edk scroll repeat
  drop lines-buf blk @ block-save cls ; ['] edi is ed
