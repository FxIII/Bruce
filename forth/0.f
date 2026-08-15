\ Block 0: Visual Editor - Viewport & Draw
variable cx variable cy variable sy variable sx variable blk
variable lines-buf 256 allot
: L ( n -- addr ) 8 * lines-buf + ;
: dl ( n --   >r lines-buf
  r@ sy @ + sx @ r> 8 * write-line ;
: cursor ( -- ) cx @ sx @ - cy @ sy @ - draw-cursor ;
: move ( dx dy -- )
  cy @ + 0 max 15 min cy ! cx @ + 0 max 63 min cx ! ;
: scroll ( -- )
  cy @ sy @ - 12 >= if 1 sy +! then
  cy @ sy @ - 0 < if -1 sy +! then
  cx @ sx @ - 40 >= if 1 sx +! then
  cx @ sx @ - 0 < if -1 sx +! then ;
: draw ( -- ) cls 12 0 do i dl loop cursor ;
