save_ctx:
  mov [SPs], sp
  mov [BPs], bp
  mov [AXs], ax
  mov [BXs], bx
  mov [CXs], cx
  mov [DXs], dx
  mov [SSs], ss
  mov [ESs], es
  mov [CSs], cs
  mov [DSs], ds
  mov [SIs], si
  mov [DIs], di
  pushf
  pop ax
  mov [FRs], ax
  call .trick
.trick:
  pop ax
  mov [IPs], ax
  ret

;ax base of registers
restore_ctx:
  push word[bx]
  mov sp, [bx+2]
  mov bp, [bx+4]
  mov bx, [bx+8]
  mov cx, [bx+10]
  mov dx, [bx+12]
  mov ss, [bx+14]
  mov es, [bx+16]
  mov cs, [bx+18]
  mov ds, [bx+20]
  mov si, [bx+22]
  mov di, [bx+24]
  mov ax, [bx+26]
  push ax
  popf
  mov ax, [bx+4]
  ret

;cx address of populating program
kyield:
  mov ax, [READY]
  jz .populate
  mov bl, 13
.loop:
  mov ax, [SAVED_CTX+bx]
  mov [SAVED_TMP+bx], ax
  dec bl
  jnz .loop
  call save_ctx
  mov bx, [SAVED_TMP]
  call restore_ctx
  jmp .error
.error:
  mov bx, ERROR_STR
  call print_string
.populate:
  call save_ctx
  mov byte[READY], 1
  call cx
  ret

ERROR_STR: db "Error scheduling", 0
READY: db 0
SAVED_TMP: times 13 dw 0
SAVED_CTX:
IPs: dw 0
SPs: dw 0
BPs: dw 0
AXs: dw 0
BXs: dw 0
CXs: dw 0
DXs: dw 0
SSs: dw 0
ESs: dw 0
CSs: dw 0
DSs: dw 0
SIs: dw 0
DIs: dw 0
FRs: dw 0
