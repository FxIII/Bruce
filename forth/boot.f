\ uForth Boot Script
\ Set up standard aliases for native C++ functions

: cls br.display.cls ;
: include br.file.include ;
: load-block br.block.load ;
: save-block br.block.save ;

\ Load the visual editor block and define edit
: edit ( num -- )
  0 br.block.load
  ed ;
