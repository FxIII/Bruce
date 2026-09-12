: eb-init ( -- ) 0 eb-cx ! 0 eb-cy ! 0 eb-sx !
  0 eb-sy ! 2 eb-zoom ! 0 eb-drty ! ;
: eb-scroll ( -- ) eb-cy @ eb-sy @ < if eb-cy @ eb-sy ! then
  eb-cy @ eb-sy @ eb-rows + >= if
    eb-cy @ eb-rows - 1+ 0 max eb-sy ! then
  eb-cx @ eb-sx @ < if eb-cx @ eb-sx ! then
  eb-cx @ eb-sx @ eb-cols + >= if
    eb-cx @ eb-cols - 1+ 0 max eb-sx ! then ;
: eb-draw ( -- ) eb-zoom @ br.display.fontSize! cls
  eb-rows 0 do i eb-sy @ + dup 16 < if
      eb-buf swap eb-sx @ i eb-heig * eb-line
    else drop then loop
  eb-cx @ eb-sx @ - eb-cy @ eb-sy @ - br.display.drawCursor ;
