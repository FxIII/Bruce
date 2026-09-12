\\ PAGE 0 - General
marker -- 0 use display constant false 1 constant true
variable buf 127 allot variable cur
: cur+ cur @ 1 + cur ! ;  : cur- cur @ 1 - cur ! ;
: c@cur pad cur @ +c@ ;   : c!cur pad cur @ +c! ;
: pleft  cur @ 0 > if cur- then ;
: pright c@cur 0 <> cur @ 63 < and if cur+ then ;
: pwrite c@cur >r c!cur cur+
   r> 0 = cur @ 64 < and if 0 c!cur then ;
: pback cur @ 0= if exit then
   c@cur >r cur- r> 0= if 0 c!cur else 32 c!cur then ;
: >pad 64 0 do 0 pad i +c! loop 8 * buf +
  64 0 do dup i +c@ pad i +c! loop drop  0 cur ! ;
: pad> 8 * buf + 64 0 do pad i +c@ over i +c! loop drop ;
: cols disp-cols ; : showline disp-write-ml ;

\\ PAGE 1 - Line Editor
: handle-key
  dup  13 = over  27 = or   if drop         true exit then
  dup   8 = over 212 = or   if drop pback  false exit then
  dup 216 =                 if drop pleft  false exit then
  dup 215 =                 if drop pright false exit then
  dup 32 >= over 126 <= and if      pwrite false exit then
  drop false ;
: cursor cur @ cols mod  cur @ cols / disp-cursor ;
: pedit begin pad 0 showline cursor key handle-key until ;
: lst 8 * buf + type ; : list 16 0 do i . i lst cr loop ;
: read buf swap block-read ; : write buf swap block-write ;
: ledit dup >pad cls pedit pad> ;
: pread buf rot rot block-readp ;
: pwrite buf rot rot block-writep ;

\\ PAGE 2 - Line Management
2 disp-font!
variable ord 1 allot
: >ord 16 0 do i 1+ 10 * ord i +c! loop ;
: _sert >ord ord swap +c! ord buf reorder ;
: insert 10 * 5 + 15 _sert ;
: outsert 200 swap _sert ;
: .ord 16 0 do ord i +c@ . i 1+ 4 mod 0= if cr then loop ;
