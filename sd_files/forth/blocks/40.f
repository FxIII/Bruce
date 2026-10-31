variable c variable lc

: c0 0 c ! ;  : c+ c @ 1+ c ! ;  : c- c @ 1- c ! ;  : c. c @ . ;
: ch c @ 30 mod 0= c @ lc @ <> and
  if c @ 30 / . cr c @ lc ! then ;

1000 ' c+ every ' ch attach-service

868 lora-init
: print-rx pad 1 + type cr ; ' print-rx is on-lora

gps-init
: print-gps c @ 10 mod 0= if pad gps-pos-str pad type cr then ;
' print-gps is on-gps

react