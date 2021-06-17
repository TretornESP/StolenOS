[ORG 0x7c00]
[BITS 16]

mov [MAIN_DISK], dl
mov bp, 0x1000
mov sp, bp

mov dh, 0x01
mov cl, 0x02
mov dl, [MAIN_DISK]
mov bx, 0x8000
call disk_load

mov dh, 0x01
mov cl, 0x03
mov dl, [MAIN_DISK]
mov bx, 0x9000
call disk_load

call 0x8000

%include "string.asm"
%include "disk.asm"
%include "process.asm"
%include "syscalls.asm"
;%include "keyboard.asm"

PROMPT: db "$", 0
times 510-($-$$) db 0
dw 0xaa55

;PROGRAM 1 prints 10 a
proga:
  mov bl, 10
  mov al, 'a'
.loop:
  call print_char
  dec bl
  jnz .loop
  ret
times 512 db 0
;PROGRAM 2 prints 10 b
progb:
  mov bl, 10
  mov al, 'b'
.loop:
  call kwrite
  dec bl
  jnz .loop
  ret
