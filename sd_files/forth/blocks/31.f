variable eb-buf 127 allot  variable eb-st 5 allot
: eb-cx   eb-st ;            : eb-cy   eb-st 1+ ;
: eb-sx   eb-st 2 + ;        : eb-sy   eb-st 3 + ;
: eb-zoom eb-st 4 + ;        : eb-drty eb-st 5 + ;
: eb-rows br.display.rows ;  : eb-cols br.display.cols ;
: eb-heig br.display.rowHeight ; : eb-line br.display.writeLine ;
: min 2dup > if swap then drop ;
: max 2dup < if swap then drop ;  : clamp rot min max ;
: copy-line ( src dst -- ) 64 * eb-buf + swap 64 * eb-buf +
  64 0 do dup i +c@ 0 3 pick i + +c! loop 2drop ;
: clear-line ( line -- ) 64 * eb-buf +
  64 0 do 32 over i + +c! loop drop ;
