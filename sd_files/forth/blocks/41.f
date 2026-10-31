\\ Page 0 - Hardware I2C, UUIDs & Strings
marker -ble-enc use ble use i2c
100 i2c-init
: enc@ 0x41 i2c@ ;  : enc! 0x41 i2c! ;
: led ( index rgb -- ) swap 3 * 0x70 + 3 enc! drop ;
: encoder ( index -- val ) 4 * 4 enc@ ;
: button  ( index -- val ) 0x50 + 1 enc@ ;
: reset   ( index -- ) 0x40 + 1 swap 1 enc! drop ;
uuid: SVC-UUID 10da4dd0-8eaa-4c69-9003-676174747277
uuid: CHR-BTN  10da4dd5-8eaa-4c69-9003-676174747277
uuid: CHR-ENC  10da4dd7-8eaa-4c69-9003-676174747277
uuid: CHR-RGB  10da4dd9-8eaa-4c69-9003-676174747277
string dev-name "EspBle Encoder"



\\ Page 1 - GATT Server Definition & Setup
1 3 gatt-table: enc-gatt  enc-gatt gatt-use
SVC-UUID gatt-srv
CHR-BTN BLE-R BLE-N or gatt-chr+
CHR-ENC BLE-R BLE-N or gatt-chr+
CHR-RGB BLE-W gatt-chr+

: ble-setup
  dev-name ble-name!
  enc-gatt gatt-reg drop
  ble-adv ;

: on-disc ble-adv ;  ' on-disc is on-ble-disc



\\ Page 2 - Buffers, State & RGB Write Handler
variable curr-btns 8 allot  variable last-encs 32 allot
variable nbuf 8 allot
: init-hw
  8 0 do 1 curr-btns i +c! i reset
    0 last-encs i 4 * + ! i 0 led loop ;

: do-rgb-write ( cid addr len -- )
  dup 4 >= if 4 / 0 do dup i 4 * + >r
    r@ 0 +c@ r@ 1 +c@ r@ 2 +c@ 8 lshift or
    r> 3 +c@ 16 lshift or led loop then drop 2drop ;
' do-rgb-write is on-chr-write




\\ Page 3 - Polling & Notification Loop
: poll-buttons
  0 ble-sub? if 8 0 do i button dup curr-btns i +c@ <> if
    dup curr-btns i +c! i nbuf 0 +c! nbuf 1 +c!
    nbuf 2 0 ble-notify else drop then loop then ;

: poll-encoders
  1 ble-sub? if 8 0 do i encoder dup last-encs i 4 * + @ <> if
    dup last-encs i 4 * + ! i nbuf 0 +c! dup nbuf 1 +c!
    dup 8 rshift nbuf 2 +c! dup 16 rshift nbuf 3 +c!
    24 rshift nbuf 4 +c! nbuf 5 1 ble-notify
    else drop then loop then ;

: enc-step ble-update poll-buttons poll-encoders ;
: enc-run init-hw ble-setup begin enc-step 5 ms key? until ble-adv-stop ;

