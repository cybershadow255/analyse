# BreeZip Analyse Tool v4.2 (Final Hunter)

Dieses Tool kombiniert tiefe System-Analyse mit gezielter Lizenz-Manipulation.

## ⚠️ WICHTIGER HINWEIS ZU ANTIVIRUS
Da dieses Tool Techniken nutzt, die auch von Software-Analysten und Sicherheitsforschern verwendet werden (Injection & Hooking), werden **Avira** und **Malwarebytes** das Tool sehr wahrscheinlich blockieren oder löschen.
- **Empfehlung**: Deaktiviere deine AV-Software kurzzeitig während der Analyse oder füge `C:\temp\` und den Ordner des Injectors zu den Ausnahmen hinzu.
- Die DLL enthält eine Verzögerung von 2 Sekunden beim Start, um einfache Scans zu umgehen.

## 🔍 Funktionen in v4.2
- **Universal Activation Hook**: Überwacht sowohl WinRT (`RoActivateInstance`) als auch klassisches COM (`CoCreateInstance`).
- **JSON-Manipulation**: Erkennt und manipuliert Lizenz-Daten in JSON-Objekten (z.B. `isPremium`).
- **Performance-Cache**: Die Konfiguration wird effizient gelesen, um die App nicht zu verlangsamen.

## 🛠 Verwendung
1. **Einrichtung**: Kopiere `breezip_hook.dll` nach `C:\temp\`.
2. **Injector**: Starte `injector.exe` als Administrator.
3. **BreeZip**: Starte die App.
4. **Analyse**: Prüfe `C:\temp\breezip_analysis.log`. Dort siehst du nun alle internen Objekt-Erstellungen der App.
5. **Manipulation**: Setze `true` in `C:\temp\breezip_config.txt`, um die automatische Korrektur der Lizenzwerte zu aktivieren.

---
**Warum v4.2?**
Wenn v3.2 Logs geliefert hat, aber v4.0 nicht, lag es wahrscheinlich an den AV-Scannern oder an einer geänderten Ladereihenfolge der App. v4.2 ist "stiller" beim Start und deckt durch den COM-Hook noch mehr potenzielle Lizenz-Pfade ab.
