# Abhängigkeiten für BreeZip Analyse Tool

Damit du das Projekt kompilieren kannst, benötigst du folgende Bibliotheken:

### 1. MinHook (WICHTIGSTES TOOL)
MinHook wird für das "Hooking" der Funktionen verwendet. Ohne diese Bibliothek funktioniert die DLL nicht.
- **Download**: [https://github.com/TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)
- **NuGet (Einfachster Weg)**:
  1. Rechtsklick auf dein DLL-Projekt in Visual Studio.
  2. "NuGet-Pakete verwalten..."
  3. Suche nach `minhook` und installiere es.

### 2. C++/WinRT
Dies wird für die Interaktion mit den Windows Store APIs benötigt.
- Ist normalerweise in modernen Visual Studio Versionen (ab 2019) enthalten.
- Falls Fehler auftreten: "C++/WinRT" über den Visual Studio Installer nachinstallieren.

### 3. Windows SDK
- Stelle sicher, dass ein aktuelles Windows 10 oder 11 SDK in den Projekteigenschaften ausgewählt ist.

---
**Hinweis zur Kompilierung**:
Sollten "LNK2019: nicht aufgelöstes externes Symbol" Fehler auftreten, stelle sicher, dass die `minhook.lib` unter *Projekteigenschaften -> Linker -> Eingabe -> Zusätzliche Abhängigkeiten* hinzugefügt wurde.
