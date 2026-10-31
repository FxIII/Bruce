\ Page 0 - Setup & BLE MIDI Definitions
marker --  use ble
1 1 gatt-table: midi-gatt
string dev-name "Cardputer-MIDI"
variable mpkt 5 allot

: init-pkt
  0x80 mpkt 0 +c!  0x80 mpkt 1 +c! ;

: send-midi ( note vel status -- )
  mpkt 2 +c!  mpkt 4 +c!  mpkt 3 +c!
  0 ble-sub? if mpkt 5 0 ble-notify then ;

: play-synth ( note -- )
  dup 100 0x90 send-midi 80 ms 0 0x90 send-midi ;

\ Page 1 - Drums & GATT Server Setup
: play-drum ( note -- )
  dup 110 0x99 send-midi 40 ms 0 0x99 send-midi ;

: setup-midi
  init-pkt
  midi-gatt gatt-use
  midi-svc gatt-srv
  midi-chr BLE-R BLE-W or BLE-N or gatt-chr+
  midi-gatt gatt-reg drop
  dev-name ble-name!
  ble-adv ;

' ble-adv is on-ble-disc


\ Page 2 - Scale (1-8) & Drums (Space, a, s, d)
: play-scale ( k -- )
  dup 49 = if 60 play-synth then
  dup 50 = if 62 play-synth then
  dup 51 = if 64 play-synth then
  dup 52 = if 65 play-synth then
  dup 53 = if 67 play-synth then
  dup 54 = if 69 play-synth then
  dup 55 = if 71 play-synth then
  dup 56 = if 72 play-synth then drop ;
: play-drums ( k -- )
  dup 32 = if 36 play-drum then
  dup 97 = if 38 play-drum then
  dup 115 = if 42 play-drum then
  dup 100 = if 46 play-drum then drop ;

\ Page 3 - Main Loop & Keyboard Handler
: handle-key ( k -- )
  dup play-scale dup play-drums drop ;
: midi-stop ble-adv-stop ;

: midi-run
  setup-midi
  begin
    ble-update
    key? dup if
      dup 113 = over 27 = or if
        drop midi-stop exit
      then handle-key
    else drop then
    10 ms again ;

