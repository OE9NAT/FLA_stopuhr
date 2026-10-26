## Master Controller

The Master Controller is the central authority of the FLA Timer system.

It is responsible for all timing, race management, communication, storage, and user interaction. Every local or remote event is processed by the master, making it the single source of truth for all recorded results.

### Core Responsibilities

#### Timing and Race Control
- Local START button handling
- Local STOP button handling
- Central timing clock
- Race state management
- Start, Split and Stop processing
- Final time calculation
- Split time calculation

#### Wireless Communication
- ESP-NOW packet reception
- Remote station management
- Duplicate packet detection and rejection
- Future ACK and heartbeat support

#### Web Dashboard
- Wi-Fi Access Point
- HTTP Web Server
- Live timing display
- Smartphone connectivity
- Status monitoring
- Run reset functionality

#### Data Management
- EEPROM persistence
- Last 5 run history
- Top 5 leaderboard
- Run comments
- Start timestamps
- New best-time detection

#### Power Management
- Activity monitoring
- Automatic deep sleep mode
- Wake-up handling

### Architecture Role

The master is the official timing authority.

Remote slave stations only generate events and transmit them via ESP-NOW. The master validates, timestamps, processes, and stores these events.

```text
         Smartphone
              │
              ▼
      ESP8266 MASTER
              │
         ESP-NOW
              │
    ┌─────────┼─────────┐
    ▼         ▼         ▼
  START     SPLIT     STOP
  SLAVE     SLAVE     SLAVE
```

Every recorded time shown in the web interface, saved in EEPROM, or displayed on future display devices originates from the Master Controller.

### Hardware

Current hardware platform:

- ESP8266 NodeMCU (ESP-12E)
- Local START button
- Local STOP button
- Status LED
- Wi-Fi Access Point
- EEPROM Flash Storage

### Future Extensions

Planned future features:

- ESP-NOW acknowledgements
- Remote heartbeat monitoring
- Battery status monitoring
- Generic checkpoint system
- External LCD display
- DS3231 RTC integration
- Additional remote timing stations