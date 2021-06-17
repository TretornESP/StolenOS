;Install keyboard handler, for the handler bx points to the word and cl tells the size (max 64B)
;ax handler address
install_keyboard:
	push word 0
	pop ds
	cli
	mov [4 * KEYBOARD_INTERRUPT], word keyboardHandler
	mov [4 * KEYBOARD_INTERRUPT + 2], cs
  mov word [HANDLER], ax
	sti
	ret

keyboardHandler:
	pusha

	in al, 0x60
	test al, 0x80
	jnz .end

	mov bl, al
	xor bh, bh
	mov al, [cs:bx + keymap]
  cmp al, 13
  je .send
  mov bl, [WORD_SIZE]
  mov [WRD+bx], al
  inc bx
  mov [WORD_SIZE], bl
  mov ah, 0x0e
	int 0x10
.end:
	mov al, 0x61
	out 0x20, al
	popa
	iret
.send:
  mov bx, WRD
  mov cl, [WORD_SIZE]
  mov dx, [HANDLER]
  call dx
  mov byte[WORD_SIZE], 0
  jmp .end

WORD_SIZE: db 0
WRD: times 64 db 0
HANDLER: dw 0
keymap:
%include "keymap.inc"

KEYBOARD_INTERRUPT EQU 9
