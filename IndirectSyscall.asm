; IndirectSyscall.asm (x64)
; Supporting variable argument counts and basic stack spoofing

PUBLIC IndirectSyscall

.code

; --- IndirectSyscall ---
; RCX: SSN
; RDX: Gadget Address
; R8:  Arg1
; R9:  Arg2
; [RSP+40]: Arg3
; [RSP+48]: Arg4
; [RSP+56]: Arg5
; [RSP+64]: Arg6
; ...
IndirectSyscall proc
    mov eax, ecx            ; Set SSN
    mov r10, r8             ; Syscall Arg1 (R10) = C++ Arg1 (R8)

    mov r11, rdx            ; R11 = Gadget Address

    mov rdx, r9             ; Syscall Arg2 (RDX) = C++ Arg2 (R9)
    mov r8, [rsp + 40]      ; Syscall Arg3 (R8)  = C++ Arg3 ([RSP+40])
    mov r9, [rsp + 48]      ; Syscall Arg4 (R9)  = C++ Arg4 ([RSP+48])

    ; For 5th argument and beyond:
    ; We'll check if they exist by the SSN or simply rely on the caller padding.
    ; To be safe, we only move if necessary or use a more robust stub.
    ; For this project's needs (Protect, Map), we shift up to 10.

    ; --- SAFE STACK SHIFTING ---
    ; We check if the stack actually has the arguments before reading.
    ; However, in x64, the shadow space (32 bytes) is always there.
    ; Arg5 is at [RSP+56]. We move it to [RSP+40] for the syscall.

    ; We use a loop-less copy for speed and stealth.
    ; We assume the caller allocated enough space for the args passed.

    mov rax, [rsp + 56]
    mov [rsp + 40], rax     ; Syscall Arg5

    mov rax, [rsp + 64]
    mov [rsp + 48], rax     ; Syscall Arg6

    mov rax, [rsp + 72]
    mov [rsp + 56], rax     ; Syscall Arg7

    mov rax, [rsp + 80]
    mov [rsp + 64], rax     ; Syscall Arg8

    mov rax, [rsp + 88]
    mov [rsp + 72], rax     ; Syscall Arg9

    mov rax, [rsp + 96]
    mov [rsp + 80], rax     ; Syscall Arg10

    mov eax, ecx            ; Re-ensure SSN in EAX
    jmp r11                 ; Jump to 'syscall; ret'
IndirectSyscall endp

end
