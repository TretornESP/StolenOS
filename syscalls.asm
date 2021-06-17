;al char to write
kwrite:
  mov cx, [ADDR_TEST]
  call kyield
  call print_char
  ret

ADDR_TEST: dw 0x9000
