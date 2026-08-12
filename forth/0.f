\ Block 0: Visual Editor - Viewport & Draw
variable cx variable cy variable sy variable sx variable blk
variable lines-buf 272 allot
: L ( n -- addr ) 17 * lines-buf + ;
: dl ( n -- ) lines-buf swap dup sy @ +
  swap sx @ swap 8 * br.display.writeLine ;
: cursor ( -- ) cx @ sx @ - cy @ sy @ - at-xy ." █" ;
: move ( dx dy -- )
  cy @ + 0 max 15 min cy ! cx @ + 0 max 63 min cx ! ;
: scroll ( -- )
  cy @ sy @ - 12 >= if 1 sy +! then
  cy @ sy @ - 0 < if -1 sy +! then
  cx @ sx @ - 40 >= if 1 sx +! then
  cx @ sx @ - 0 < if -1 sx +! then ;
: draw ( -- ) cls 12 0 do i dl loop cursor ;
