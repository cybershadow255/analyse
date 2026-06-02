; IndirectSyscall.asm (x64)
; Supporting up to 10 arguments for NT syscalls

PUBLIC IndirectSyscall

.code

IndirectSyscall proc
    ; RCX: SSN
    ; RDX: Gadget Address
    ; R8:  Arg1
    ; R9:  Arg2
    ; [RSP+40]: Arg3
    ; [RSP+48]: Arg4
    ; [RSP+56]: Arg5
    ; [RSP+64]: Arg6
    ; [RSP+72]: Arg7
    ; [RSP+80]: Arg8
    ; [RSP+88]: Arg9
    ; [RSP+96]: Arg10

    mov eax, ecx            ; Set SSN in EAX
    mov r10, r8             ; Syscall Arg1 (R10) = C++ Arg1 (R8)

    ; We need to move the Gadget address to a temporary register
    mov r11, rdx            ; R11 = Gadget Address

    ; Setup Syscall Arg2 (RDX)
    mov rdx, r9             ; Syscall Arg2 (RDX) = C++ Arg2 (R9)

    ; Setup Syscall Arg3 (R8)
    mov r8, [rsp + 40]      ; Syscall Arg3 (R8) = C++ Arg3 ([RSP+40])

    ; Setup Syscall Arg4 (R9)
    mov r9, [rsp + 48]      ; Syscall Arg4 (R9) = C++ Arg4 ([RSP+48])

    ; For 5th to 10th arguments, they are on the stack.
    ; C++ Arg5 is at [RSP+56], Syscall Arg5 expects [RSP+40]
    ; We need to shift the stack arguments.
    ; However, we cannot modify the caller's stack frame safely.
    ; A better way is to copy the arguments and jump.
    ; Since we are jumping to a 'syscall; ret' gadget in ntdll,
    ; the 'ret' will return to our C++ caller.

    ; For functions with many args like NtMapViewOfSection, we must shift:
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

    jmp r11                 ; Jump to 'syscall; ret'
IndirectSyscall endp

end
