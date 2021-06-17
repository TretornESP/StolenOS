;bx direccion string
print_string:
    pusha
    xor si, si
.loop:
    mov al, byte [bx+si]
    inc si
    cmp al, 0
    je .end
    call print_char
    jmp .loop
.end:
    popa
    ret

;al caracter
print_char:
  push ax
  mov ah, 0x0e
  int 0x10
  pop ax
  ret
