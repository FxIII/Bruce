\ Block 0: Visual Editor in Forth

variable cx
variable cy
variable sy
variable sx
variable blk

\ lines-buf in RAM: 16 lines of 17 cells (1 length + 16 cells for 64 chars)
variable lines-buf 272 allot

\ L returns the cell address of line n
: L ( n -- addr ) 17 * lines-buf + ;

\ dl draws line n on the screen at vertical pixel position n * 8
: dl ( n -- )
  i sy @ + L
  sx @
  i 8 *
  br.display.writeLine ;

\ cursor draws the cursor block on the screen
: cursor ( -- )
  cx @ sx @ -
  cy @ sy @ -
  at-xy
  ." █" ;

\ move shifts cursor position by dx, dy and clamps to block boundaries
: move ( dx dy -- )
  cy @ + 0 max 15 min cy !
  cx @ + 0 max 63 min cx ! ;

\ scroll shifts the viewport sx, sy to follow the cursor
: scroll ( -- )
  cy @ sy @ - 12 >= if 1 sy +! then
  cy @ sy @ - 0 < if -1 sy +! then
  cx @ sx @ - 40 >= if 1 sx +! then
  cx @ sx @ - 0 < if -1 sx +! then ;

\ ins inserts a character at current cursor position (overwrite mode)
: ins ( c -- )
  cy @ L >r ( c ) ( R: addr )
  r@ 1+ cx @ br.display.setChar
  cx @ 1+ r@ @ max r@ br.display.setLen
  1 0 move
  rdrop ;

\ del deletes character under backspace (moves left and writes a space)
: del ( -- )
  -1 0 move
  32 cy @ L 1+ cx @ br.display.setChar ;

\ draw clears display and redraws the 12 visible lines plus cursor
: draw ( -- )
  br.display.cls
  12 0 do i dl loop
  cursor ;

\ ed runs the editor event loop for block b
: ed ( b -- )
  dup blk !
  lines-buf swap br.block.load
  0 cx ! 0 cy ! 0 sy ! 0 sx !
  begin
    draw key
    dup 27 <> ( Check ESC to quit )
  while
    dup 0xDA = if drop 0 -1 move then \ Up Arrow
    dup 0xD9 = if drop 0  1 move then \ Down Arrow
    dup 0xD8 = if drop -1 0 move then \ Left Arrow
    dup 0xD7 = if drop 1  0 move then \ Right Arrow
    dup 8 = if drop del then         \ Backspace
    dup 13 = if drop 0 1 move 0 cx ! then \ Enter: move to next line start
    dup 32 >= over 126 <= and if ins else drop then
    scroll
  repeat
  drop
  lines-buf blk @ br.block.save
  br.display.cls ;
