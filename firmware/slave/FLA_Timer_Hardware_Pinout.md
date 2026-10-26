# FLA Timer – Hardware & Pinout Documentation

## Projektübersicht

FLA Timer basiert auf ESP8266 (NodeMCU) Controllern.

Bestandteile:

- 1x Master ESP8266
- 1x START/STOP Slave ESP8266
- optional LC-204VL Anzeige über MAX7219 Treiber
- Smartphone Weboberfläche
- ESP-NOW Kommunikation

---

# WLAN Zugang

SSID:

```text
FLA-Timer
```

Passwort:

```text
12345678
```

Webseite:

```text
http://192.168.4.1
```

---

# Master Pinbelegung

## Taster

| Funktion | NodeMCU Pin | GPIO | Anschluss |
|-----------|------------|------|-----------|
| START | D1 | GPIO5 | Taster gegen GND |
| STOP | D2 | GPIO4 | Taster gegen GND |

Verwendet:

```cpp
INPUT_PULLUP
```

Daher:

- offen = HIGH
- gedrückt = LOW

---

## LC204VL Anzeige

Ansteuerung erfolgt über MAX7219.

| Funktion | NodeMCU Pin | GPIO |
|-----------|------------|------|
| CLK | D5 | GPIO14 |
| LOAD / CS | D6 | GPIO12 |
| DIN | D7 | GPIO13 |

Zusätzlich:

| Signal | Anschluss |
|----------|----------|
| VCC | 5V |
| GND | GND |

---

# Nicht verwendete Pins

| Pin | Grund |
|------|------|
| D3 | Boot-Pin GPIO0 |
| D4 | Boot-Pin GPIO2 |
| D8 | Boot-Pin GPIO15 |

Diese Pins sollten für Taster vermieden werden.

---

# Slave Pinbelegung

## START/STOP Slave

| Funktion | NodeMCU Pin | GPIO |
|-----------|------------|------|
| START | D1 | GPIO5 |
| STOP | D2 | GPIO4 |

Anschluss:

```text
Pin ---- Taster ---- GND
```

---

# ESP-NOW Kommunikation

WLAN Kanal:

```text
1
```

Heartbeat:

```text
alle 5 Sekunden
```

Offline Timeout:

```text
15 Sekunden
```

---

# Webseiten Funktionen

## Vorhanden

- Live Zeit
- Zwischenzeit 1
- Zwischenzeit 2
- Letzter Lauf
- Kommentar zum letzten Lauf
- Top 5 Bestzeiten
- Letzte 5 Durchgänge
- Slave Status
- EEPROM Speicherung

---

# Aktuelles Webseiten Layout

1. Laufende Zeit
2. Letzter Lauf
3. Top 5 Bestzeiten
4. Letzte 5 Durchgänge
5. Slave Verbindungsstatus

---

# Slave Status

Angezeigt werden:

- Online / Offline
- Sender ID
- Letzter Kontakt
- Paketzähler

---

# Projektstatus

Stand: zufriedenstellend

Aktuell funktionsfähig:

- Webserver
- Liveanzeige
- EEPROM Speicherung
- Top 5
- Letzte 5
- Kommentare
- ESP-NOW
- Slave Überwachung
- LC204VL Vorbereitung

