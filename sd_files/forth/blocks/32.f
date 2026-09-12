: get-char ( col -- char ) eb-buf eb-cy @ 64 * + swap +c@ ;
: set-char ( char col -- ) eb-buf eb-cy @ 64 * + swap +c! ;
: eb-line-empty? ( -- flag ) 1 64 0 do
    i get-char 32 <> if drop 0 leave then loop ;
: eb-shift-right ( -- ) eb-cx @ 63 < if 62 begin
    dup eb-cx @ >= while dup get-char over 1+ set-char 1-
    repeat drop then ;
: eb-shift-left ( -- ) eb-cx @ begin dup 62 <= while
    dup 1+ get-char over set-char 1+ repeat drop 32 63 set-char ;
: eb-insert-line ( -- ) eb-cy @ 15 < if 14 begin
    dup eb-cy @ 1+ >= while dup dup 1+ copy-line 1-
  repeat drop eb-cy @ 1+ clear-line then ;
: eb-delete-line ( -- ) eb-cy @ 1+ begin dup 15 <= while
    dup dup 1- copy-line 1+ repeat drop 15 clear-line ;
