# BreeZip Pro Analyse & Manipulation Tool (Final)

Dieses Tool ermöglicht die Analyse und Manipulation der Lizenzprüfung von BreeZip.

## 🛠 Einrichtung & Kompilierung

### 1. Visual Studio Vorbereitung
- Installiere **Visual Studio 2022** mit "Desktopentwicklung mit C++".
- Erstelle zwei Projekte:
  - **DLL-Projekt**: `breezip_hook` (Konfiguration: DLL, x64).
  - **Injector-Projekt**: `injector` (Konfiguration: Konsolenanwendung, x64).

### 2. MinHook integrieren (Für die DLL)
- Öffne die **NuGet-Paket-Manager-Konsole** in Visual Studio.
- Gib ein: `Install-Package minhook`.
- Stelle sicher, dass die `minhook.lib` in den Linker-Einstellungen deines DLL-Projekts unter "Zusätzliche Abhängigkeiten" eingetragen ist.

### 3. Kompilieren
- Kompiliere beide Projekte im Modus **Release** und **x64**.
- Benenne die fertige DLL in `breezip_hook.dll` um.

---

## 🚀 Nutzung

1. **Vorbereitung**: Kopiere die `breezip_hook.dll` nach `C:\temp\`.
2. **Start**: Starte die BreeZip App.
3. **Injection**: Starte den `injector.exe` als **Administrator**.
4. **Analyse**:
   - Die App protokolliert nun alle relevanten Aufrufe in `C:\temp\breezip_analysis.log`.
   - Suche nach Einträgen wie `[ANALYSE] get_IsActive...`.
5. **Manipulation**:
   - Erstelle die Datei `C:\temp\breezip_config.txt`.
   - Schreibe `true` in die Datei und speichere sie.
   - BreeZip wird nun bei der nächsten Prüfung denken, dass die Pro-Version aktiv ist.

---

## 🔍 Technische Details für Experten

- **AppContainer-Bypass**: Der Injector setzt die SID `S-1-15-2-1` (ALL APPLICATION PACKAGES), damit die UWP-Sandbox die DLL laden darf.
- **Hooking-Strategie**: Wir nutzen **MinHook**, um `RoGetActivationFactory` zu überwachen. Sobald BreeZip nach dem `StoreContext` fragt, klinken wir uns ein.
- **VTable-Manipulation**: Die DLL sucht nach dem Interface `IStoreAppLicense` und manipuliert die Methode `get_IsActive` (Index 8), welche den Lizenzstatus zurückgibt.

---
**Wichtiger Hinweis**: Dieses Tool wurde für Bildungszwecke im Bereich Software-Analyse entwickelt.
