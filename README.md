# FLA Timer

A wireless timing system for Feuerwehrleistungsabzeichen (FLA) training and competitions based on ESP8266 microcontrollers and ESP-NOW communication.

The system consists of a central **Master Controller**, multiple **Slave Stations**, and a separate **ESP32 Remote Display**. The master is responsible for all timing calculations, data storage, and communication. Slave stations generate timing events, while the display unit is used only for visualization.

---

# Features

- Wireless timing using ESP-NOW
- ESP8266 Master Controller
- Multiple ESP8266 Slave Stations
- Start, Split and Stop timing points
- Real-time web interface accessible from smartphone or tablet
- Storage of the last 5 runs
- Top 5 best times leaderboard
- Run comments and notes
- Automatic time synchronization from smartphone
- EEPROM data storage
- Deep Sleep support for reduced power consumption
- Expandable architecture for additional timing points and display units

---

# System Architecture

```text
                 Smartphone
                      │
                WiFi / HTTP
                      │
                      ▼
               ESP8266 MASTER
                      │
                ESP-NOW BUS
                      │
      ┌───────────────┼───────────────┐
      ▼               ▼               ▼
 ESP8266 SLAVE   ESP8266 SLAVE   ESP8266 SLAVE
    START           SPLIT            STOP

                      │
                      ▼
             ESP32 REMOTE DISPLAY
                (visualization)
```

The Master Controller is the only device responsible for:

- Timing calculations
- Data storage
- Race management
- Web interface
- Statistics generation

Slave stations only send timing events to the master.

The ESP32 display is not part of the timing process and serves purely as a remote visualization unit.

---

# Hardware

## Master Controller

- ESP8266 NodeMCU
- WiFi Access Point
- Local timing engine
- EEPROM storage
- Start and stop control logic

## Slave Stations

Each slave station consists of:

- ESP8266 NodeMCU
- Trigger button or sensor input
- ESP-NOW communication
- Status LED

Current slave types:

- Start Slave
- Split Slave
- Stop Slave

All slaves run nearly identical firmware and are configured according to their role.

## Remote Display

The display system uses a dedicated ESP32.

Functions:

- Live runtime display
- Split time display
- Best-times leaderboard
- System status
- Remote monitoring

The display never generates timing events.

---

# Communication

## ESP-NOW

Communication between master and slave stations is performed using ESP-NOW.

Advantages:

- No internet connection required
- Low latency
- Reliable communication
- Low power consumption
- Direct device-to-device communication

## Event Types

| Event | Description |
|---------|-------------|
| START | Run started |
| SPLIT | Intermediate checkpoint |
| STOP | Run finished |

---

# Web Interface

The Master creates its own WiFi network.

Example:

```text
SSID: FLA-Timer
IP:   192.168.4.1
```

The web interface provides:

- Current runtime
- Split times
- Completed runs
- Top 5 leaderboard
- Run comments
- Device status
- Historical results

---

# Data Management

For every run the system stores:

- Run ID
- Start timestamp
- Total runtime
- Split times
- User comments

Additional calculations include:

```text
Section A = Start → Split

Section B = Split → Stop

Total Time = Start → Stop
```

---

# Power Management

The master supports:

- Automatic inactivity detection
- Deep Sleep mode
- EEPROM backup before sleep

This allows battery powered operation if required.

---

# Roadmap

Planned improvements:

- Acknowledgement protocol for higher reliability
- Battery monitoring
- Heartbeat monitoring of slave stations
- CSV export
- Extended statistics
- Additional checkpoint support
- Standalone display enhancements

---

# Project Goals

The goal of this project is to provide a low-cost, robust and portable timing system for firefighter training and FLA competitions.

The architecture separates timing, event generation and visualization into dedicated devices to ensure reliable measurements and future scalability.

---

**Hardware:** ESP8266 / ESP32  
**Communication:** ESP-NOW  
**Application:** Firefighter training and FLA competitions
