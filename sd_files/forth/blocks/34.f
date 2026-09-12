: eb-arrow ( c -- c_out dx dy )
  dup 0xDA = if drop 0  0 -1 exit then
  dup 0xD9 = if drop 0  0  1 exit then
  dup 0xD8 = if drop 0 -1  0 exit then
  dup 0xD7 = if drop 0  1  0 exit then 0 0 ;
: eb-move ( dx dy -- ) >r eb-cx @ + dup 64 + 64 mod eb-cx !
  64 + 64 / 1 - r> + eb-cy @ + 0 15 clamp eb-cy ! ;
: eb-put-bs ( c -- c_out ) dup 8 = if drop eb-cx @ 0 > if
      eb-cx @ 1- eb-cx ! eb-shift-left 1 eb-drty ! 0 exit then
    eb-cx @ 0 = eb-cy @ 0 > and if eb-line-empty? if
        eb-cy @ 1- eb-cy ! eb-delete-line 0 eb-cx !
        1 eb-drty ! 0 exit then then 0 exit then ;
: eb-put-tab ( c -- c_out ) dup 9 = over 179 = or if drop
    eb-insert-line 0 eb-cx ! eb-cy @ 1+ 15 min eb-cy !
    1 eb-drty ! 0 exit then ;
