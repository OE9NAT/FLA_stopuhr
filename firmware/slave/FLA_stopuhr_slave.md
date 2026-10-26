## Slave Station

The Slave Station is a lightweight event node designed to generate timing events and transmit them wirelessly to the Master Controller using ESP-NOW.

Each slave runs the same firmware image. Its functionality is determined solely by the connected input pin, allowing identical hardware and firmware to be used for all timing stations.

The slave does not perform any timing calculations, race management, storage, or result processing. All official timing decisions are made by the Master Controller.

### Core Responsibilities

#### Event Detection

- Detect button presses or external trigger events
- Monitor START, SPLIT, or STOP inputs
- Capture the first valid trigger edge
- Generate event notifications for the master

#### Wireless Communication

- Create ESP-NOW event packets
- Transmit events to the master
- Support packet retransmission
- Preserve event information for future synchronization features

#### User Feedback

- Activity indication using onboard LED
- Visual confirmation of transmitted events
- Future support for buzzer feedback

### Event Types

A slave can generate four event types:

| Event | Purpose |
|---------|----------|
| START | Begins a run |
| SPLIT 1 | First intermediate checkpoint |
| SPLIT 2 | Second intermediate checkpoint |
| STOP | Finishes a run |

### Hardware

Current hardware platform:

- ESP8266 NodeMCU compatible board
- One or more push buttons
- Onboard status LED
- USB power source or power bank

### Input Assignment

The firmware automatically determines the slave role from the connected input.

```text
D1 / GPIO5   → START
D2 / GPIO4   → SPLIT 1
D5 / GPIO14  → SPLIT 2
D6 / GPIO12  → STOP
```

The same firmware can therefore be flashed to every slave station.

No DIP switches, jumpers, configuration files, or dedicated firmware versions are required.

### Communication Workflow

```text
Button Press
     │
     ▼
 Slave detects event
     │
     ▼
 Create ESP-NOW packet
     │
     ▼
 Send packet to Master
     │
     ▼
 Master validates and processes event
```

### Design Principles

- One firmware for all slave stations
- No local timing authority
- No local data storage
- No Wi-Fi access point
- No web interface
- Minimal processing load
- Fast event transmission
- Easy deployment and maintenance

### Future Features

Planned extensions:

- Application-level acknowledgements (ACK)
- Heartbeat packets
- Battery monitoring
- Online/offline status reporting
- Audible buzzer feedback
- Enhanced transmission diagnostics

### Architecture Role

The slave acts as a remote trigger station.

Its sole purpose is to detect events and forward them to the Master Controller as quickly and reliably as possible.

The master remains the only official timing authority of the FLA Timer system.