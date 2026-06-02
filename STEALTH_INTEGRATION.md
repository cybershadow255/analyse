# Advanced Evasion: Syscall & Unhooking Integration

Your project now includes an advanced stealth module capable of bypassing modern EDRs (CrowdStrike, SentinelOne, Defender for Endpoint).

## 1. Build Requirements (Visual Studio)

To compile the indirect syscalls, you must enable **MASM** in your project:
1.  Right-click your Project in Solution Explorer.
2.  Select **Build Dependencies** > **Build Customizations...**.
3.  Check the box for **masm (.targets, .props)**.
4.  Add `IndirectSyscall.asm` to your project.
5.  Right-click `IndirectSyscall.asm` > **Properties**.
6.  Ensure **Item Type** is set to **Microsoft Macro Assembler**.

## 2. Stealth Features Implemented

### 🛡️ Indirect Syscalls (Gatekeeper)
Bypasses EDR hooks by executing system calls directly via the kernel, but jumping to a `syscall; ret` gadget inside `ntdll.dll`. This makes the call appear legitimate to the kernel's stack-walking checks.

### 💉 Ntdll Unhooking
Before the main logic starts, the program reloads a clean copy of `ntdll.dll` from `C:\Windows\System32\ntdll.dll`. It then suspends all other threads and overwrites the hooked `.text` section in memory with the clean code from disk, effectively "blinding" the EDR.

### 🚫 AMSI Bypass
Patches the `AmsiScanBuffer` function in `amsi.dll` to always return `AMSI_RESULT_CLEAN`. This prevents Windows Defender from scanning your memory buffers and strings.

### 🧬 Halo's Gate (Neighbor Scanning)
If an EDR has already hooked the syscall stub in `ntdll.dll`, the program will scan neighboring functions to find an unhooked stub and calculate the correct System Service Number (SSN) automatically.

### 🎭 PEB-based Dynamic Resolution
The program no longer has an Import Address Table (IAT) for suspicious functions. It resolves every DLL and function by traversing the Process Environment Block (PEB) and using DJB2 hashes.

## 3. Usage Guide

- **Strings**: Use `DECRYPT_W` for any new URLs or paths you add.
- **APIs**: Use `CALL_API(Hashes::module, Hashes::function, function, args...)` for any new Windows APIs.
- **Initialization**: Ensure `Stealth::Initialize()` remains the very first call in `wmain`.

```cpp
#include "Stealth.h"

int wmain(int argc, wchar_t* argv[]) {
    Stealth::Initialize(); // <--- Critical
    // ...
}
```
