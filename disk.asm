;Load dh sectors since cl from drive dl into EX:BX
disk_load:
  pusha
  push dx
  mov ah, 0x02
  mov al, dh
  mov ch, 0x00
  mov dh, 0x00
  int 0x13
  jc disk_error
  pop dx
  cmp al, dh
  jne sectors_error
  popa
  ret

disk_error:
  mov bx, DISK_ERROR
  call print_string
  jmp disk_loop

sectors_error:
  mov bx, SECTORS_ERROR
  call print_string

disk_loop:
  mov bx, LOOPING
  call print_string
  jmp $

LOOPING: db "ERROR STATE",0
SECTORS_ERROR: db "Sectors error", 0
DISK_ERROR: db "Disk error detected", 0
MAIN_DISK: db 0
