\\ Page 0
use i2c use display
100 i2c-init
: enc@ 0x41 i2c@ ;
: enc! 0x41 i2c! ;
: led ( index rgb -- )
  swap 3 * 0x70 + 3 enc! drop ;
: encoder ( index )
  4 * 4 enc@ ;
: button ( index )
  0x50 + 1 enc@ ;
: reset ( index )
  0x40 + 1 swap  1 enc! drop ;
  
  
  
\\ Page 2
variable scaleRead 1 allot
: scale@ 0x26 i2c@ ;
: scale! 0x26 i2c! ;
: scale-c@
  0x70 8 scale@ scaleRead !
  0x78 8 scale@ scaleRead 1 + ! ;
: scale.
  scale-c@ scaleRead s. ;
: scaleLed ( index rgb)
  swap 3 * 0x30 + 3 scale! drop ;  





\\ Page 3
: rgb ( r g b )
  8 lshift + 8 lshift + ;
: h>rgb ( h[0-1536] -- r g b)
  1536 mod
  dup 0xFF and >r
  8 rshift
  dup 0 = if drop 255 r> 0 exit then
  dup 1 = if drop 255 r> - 255 0 exit then
  dup 2 = if drop 0 255 r> exit then
  dup 3 = if drop 0 255 r> - 255 exit then
  dup 4 = if drop r> 0 255 exit then
  drop 255 0 255 r> - ;



\\ Page 2
: follow ( index )
 dup encoder drop 4 * h>rgb rgb led ;
 
: keepfollow
  0 reset 2 reset 4 reset 6 reset
  10000 0 do 0 follow 2 follow 4 follow 6 follow  loop ;