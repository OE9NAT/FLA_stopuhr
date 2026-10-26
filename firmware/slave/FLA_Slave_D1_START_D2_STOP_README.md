# FLA Timer START/STOP Slave

Passende Slave-Firmware für den aktuellen ESP8266-Master.

## Pinout

| Funktion | NodeMCU | GPIO | Verdrahtung |
|---|---:|---:|---|
| START | D1 | GPIO5 | Taster zwischen D1 und GND |
| STOPP | D2 | GPIO4 | Taster zwischen D2 und GND |

Die Eingänge verwenden `INPUT_PULLUP`:

- Taster offen: HIGH
- Taster gedrückt: LOW

## ESP-NOW-Protokoll

| Nachricht | `eventType` | `senderId` |
|---|---:|---:|
| START | 1 | 4 |
| STOPP | 4 | 4 |
| Heartbeat | 100 | 4 |

Der Funkkanal ist fest auf Kanal 1 eingestellt. Der Master muss ebenfalls auf Kanal 1 laufen.

## Upload

1. Arduino IDE öffnen.
2. Board `NodeMCU 1.0 (ESP-12E Module)` auswählen.
3. `FLA_Slave_D1_START_D2_STOP_compatible.ino` öffnen.
4. Firmware hochladen.
5. Seriellen Monitor mit 115200 Baud öffnen.
6. Master einschalten.
7. Innerhalb von fünf Sekunden muss der Slave auf der Master-Webseite als online erscheinen.
8. D1 drücken, danach D2 drücken. STOPP wird vom Master nur im Zustand `RUNNING` verarbeitet.

## Diagnose

Erwartete Startmeldung:

```text
FLA Timer START/STOP Slave
D1 / GPIO5 = START gegen GND
D2 / GPIO4 = STOPP gegen GND
Sender-ID   = 4
Funkkanal   = 1
ESP-NOW     = BEREIT
```

Beim Tastendruck:

```text
Taster D1: sende START
Taster D2: sende STOPP
```

Wenn `ESP-NOW Zustellung: FEHLER` erscheint, müssen Funkkanal und Master-Empfang geprüft werden. Für eine zuverlässigere Punkt-zu-Punkt-Verbindung kann statt der Broadcast-Adresse die WLAN-STA-MAC des Masters in `masterMac` eingetragen werden.
