31 load 32 load 33 load 34 load
: eb-put-ret ( c -- c_out ) dup 13 = if drop 0 eb-cx !
    eb-cy @ 1+ 15 min eb-cy ! 0 exit then ;
: eb-put-char ( c -- c_out ) dup 32 >= over 126 <= and if
    eb-shift-right eb-buf eb-cy @ 64 * eb-cx @ + +c!
    eb-cx @ 1+ 63 min eb-cx ! 1 eb-drty ! 0 exit then ;
: eb-put ( c -- ) dup 0= if drop exit then
  eb-put-bs eb-put-tab eb-put-ret eb-put-char drop ;
: eb-edit ( -- ) eb-init begin eb-scroll eb-draw key
  dup 27 <> while eb-arrow eb-move eb-put repeat drop cls ;
