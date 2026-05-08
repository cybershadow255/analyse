# BreeZip Analyse & Manipulation Tool v3.2 (Final Release)

Dies ist die stabilste und am weitesten entwickelte Version des Tools.

## 🌟 Was ist neu in v3.2?
- **Korrekte VTable-Pfade**: Die internen Adressen für die Lizenzprüfung wurden präzisiert, um Abstürze zu verhindern und die Zuverlässigkeit zu erhöhen.
- **Dual-API Support**: Die DLL erkennt nun automatisch, ob BreeZip die moderne `Windows.Services.Store` API oder die ältere `Windows.ApplicationModel.Store` API nutzt und hookt den entsprechenden Pfad.
- **Detailliertes Reporting**: Das Log zeigt nun genau an, welche WinRT-Klassen die App anfordert (`[SCANNER] App fordert an...`).

## 🛠 Anleitung

1. **Einrichtung**: Kopiere die `breezip_hook.dll` nach `C:\temp\`.
2. **Watchdog**: Starte den `injector.exe` als Administrator.
3. **Start**: Öffne BreeZip.
4. **Ergebnis**:
   - Schau in das Log: `C:\temp\breezip_analysis.log`.
   - Wenn du dort siehst: `[HOOK] get_IsActive (Index 7) installiert`, dann hat das Tool die Lizenzprüfung erfolgreich im Griff.
5. **Manipulation**: Schreibe `true` in `C:\temp\breezip_config.txt`, um Premium freizuschalten.

## 📘 Fehlerbehebung
- **Keine Logs nach "RoGetActivationFactory aktiv"**: Das bedeutet, BreeZip hat noch keine Lizenzprüfung gestartet. Klicke in der App auf ein Premium-Feature, um die Prüfung auszulösen.
- **App stürzt ab**: Stelle sicher, dass du die DLL als **x64 Release** kompiliert hast.

---
*Viel Erfolg bei deiner Analyse! Diese Version deckt alle offiziellen Lizenzpfade von Microsoft Store Apps ab.*
