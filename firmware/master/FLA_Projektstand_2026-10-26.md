# FLA Stoppuhr Projektstand

## Projektziel
Aufbau einer Feuerwehr-FLA-Stoppuhr auf Basis eines ESP8266-Masters mit mehreren ESP-NOW-Remotes (Slaves).

## Architektur

### Master (ESP8266)
- WLAN Access Point für Smartphone-Anzeige
- Webserver für Bedienung und Statusanzeige
- Lokaler START-Taster am Master
- EEPROM-Speicherung der Ergebnisse
- Verwaltung der Durchgänge und Bestzeiten
- ESP-NOW Empfänger für Zwischenzeiten und Ziel

### Slaves
- Slave 1 = Zwischenzeit 1
- Slave 2 = Zwischenzeit 2
- Slave 3 = Ende
- Online-/Offline-Erkennung über Heartbeat vorgesehen

## Umgesetzte Funktionen

### Zeitmessung
- Start
- Zwischenzeit 1
- Zwischenzeit 2
- Stop
- Reset
- Interrupt-basierte Tastererfassung

### Datenspeicherung
- Letzte 5 Durchgänge
- Top-5-Bestzeiten
- EEPROM-Speicherung
- Lauf-ID pro Durchgang

### Kommentare
- Kommentare bleiben erhalten
- Kommentare nur für den neuesten Durchgang editierbar
- Kommentare werden auch in Top-5-Einträgen angezeigt

### Weboberfläche
Aktuelle Reihenfolge:
1. Laufende Zeit
2. Letzter Lauf
3. Top 5 Bestzeiten
4. Letzte 5 Durchgänge
5. Slave-Verbindungsstatus

### Slave Status
Anzeige von:
- Online / Offline
- Sender-ID
- Letzter Kontakt
- Paketanzahl

Der Starter wird nicht angezeigt, da er direkt am Master angeschlossen ist.

## Behobene Probleme

### Doppelte Arduino-Dateien
Problem:
- Zwei vollständige .ino-Dateien im gleichen Sketchordner
- Mehrfachdefinitionen von setup(), loop(), server usw.

Lösung:
- Nur eine Hauptdatei im Sketchordner verwenden.

### Kommentarfeld
Problem:
- Smartphone-Tastatur verschwand beim Schreiben

Lösung:
- Verhindert Neuzeichnen während Eingabe aktiv ist.

### HTTPS
Versuch:
- HTTPS mit BearSSL implementiert

Ergebnis:
- Webseite wurde instabil bzw. nicht mehr erreichbar
- HTTPS wieder entfernt
- Projekt verwendet aktuell ausschließlich HTTP

## Aktueller Zugriff

WLAN:
- SSID: FLA-Timer
- Passwort: 12345678

Webseite:
- http://192.168.4.1

## Aktueller Status

Zufriedenstellender Zwischenstand:
- Bedienung funktioniert
- Layout entspricht den Anforderungen
- Top 5 vorhanden
- Letzte 5 vorhanden
- Kommentare vorhanden
- Slave-Status vorhanden
- ESP-NOW integriert

## Mögliche spätere Optimierungen

### Performance
- /status in Live- und Daten-Endpunkt aufteilen
- Aktualisierungsintervall reduzieren
- DOM nur bei Änderungen aktualisieren
- MAC-Adressen aus Statusseite entfernen
- Slave-Daten nur bei Änderungen übertragen

### Erweiterungen
- Erweiterte Fehlerdiagnose
- Export der Ergebnisse
- Mehrere Trainingsmodi
- Verbesserte Mobile-Ansicht

## GitHub Hinweis

Dieser Stand dient als Referenzpunkt und kann als Basis für weitere Entwicklungsschritte verwendet werden.
