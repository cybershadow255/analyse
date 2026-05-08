# Security Research Utility: BreeZip Analysis (v7.1)

Dieses Toolset dient der wissenschaftlichen Untersuchung von Lizenzprüfmechanismen in modernen Windows Store (UWP) Applikationen am Beispiel von BreeZip.

## 🔬 Forschungshintergrund
Das Tool nutzt Techniken der dynamischen Laufzeitanalyse, um die Interaktion zwischen Applikation und Betriebssystem (COM/WinRT) zu protokollieren. Ein besonderer Fokus liegt auf der Verarbeitung von JSON-Datenströmen, die oft für serverbasierte Berechtigungsprüfungen genutzt werden.

## 🔍 Features v7.1
- **Dynamische Pfade**: Nutzt das System-Temp-Verzeichnis für Logs und Konfiguration.
- **Nexus-Hooking**: Interzeptiert JSON-Objekte direkt beim Parsen (`IJsonObjectStatics::Parse`), was eine 100%ige Abdeckung aller aus Strings erstellten Daten garantiert.
- **Stabilitäts-Tracker**: Verhindert doppeltes Hooking von Adressen zur Laufzeit.

## 🛠 Verwendung
1. **Kompilierung**: DLL und Injector als x64 Release erstellen.
2. **Setup**: Die DLL muss im lokalen Dateisystem für den Injector erreichbar sein.
3. **Analyse**:
   - Die Logs findest du unter `%TEMP%\breezip_analysis.log`.
   - Die Konfiguration erfolgt über `%TEMP%\breezip_config.txt`.
4. **Interaktion**: Setze `true` in der Konfigurationsdatei, um die Auswirkungen von manipulierten Rückgabewerten auf die App-Logik zu untersuchen.

---
**Rechtlicher Hinweis**: Dieses Tool wurde ausschließlich für Bildungs- und Forschungszwecke im Bereich der Software-Sicherheit entwickelt. Jede Verwendung zur Umgehung von Urheberrechtsschutzmaßnahmen geschieht auf eigene Verantwortung.
