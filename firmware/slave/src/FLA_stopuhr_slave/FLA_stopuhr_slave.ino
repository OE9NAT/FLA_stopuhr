#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
#include <user_interface.h>
}

/*
  FLA Timer - START/STOP Slave fuer ESP8266 NodeMCU

  Pinout:
    D1 / GPIO5 -> START-Taster -> GND
    D2 / GPIO4 -> STOPP-Taster -> GND

  Kommunikation:
    ESP-NOW, Kanal 1
    senderId 4, damit der aktuelle Master den Controller als Ende-Slave erkennt
    START eventType 1
    STOPP eventType 4
    HEARTBEAT eventType 100, alle 5 Sekunden

  WICHTIG:
    Der Master muss ebenfalls auf WLAN/ESP-NOW-Kanal 1 laufen.
    Zum Testen zuerst START druecken. STOPP wird vom Master nur verarbeitet,
    wenn der Lauf bereits RUNNING ist.
*/

constexpr uint8_t PIN_START = D1;
constexpr uint8_t PIN_STOP = D2;
constexpr uint8_t SENDER_ID = 4;
constexpr uint8_t WIFI_CHANNEL = 1;

constexpr uint8_t EVENT_START = 1;
constexpr uint8_t EVENT_STOP = 4;
constexpr uint8_t EVENT_HEARTBEAT = 100;

constexpr uint32_t DEBOUNCE_US = 20000;
constexpr uint32_t HEARTBEAT_MS = 5000;
constexpr uint8_t EVENT_REPETITIONS = 3;

struct __attribute__((packed)) RemotePacket {
  uint32_t packetId;
  uint8_t eventType;
  uint8_t senderId;
  uint32_t remoteMicros;
};

// Broadcast funktioniert ohne Eintragen der Master-MAC.
// Fuer eine spaetere Punkt-zu-Punkt-Verbindung hier die Master-MAC einsetzen.
uint8_t masterMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

volatile bool startPending = false;
volatile bool stopPending = false;
volatile uint32_t startCapturedUs = 0;
volatile uint32_t stopCapturedUs = 0;
volatile uint32_t lastStartEdgeUs = 0;
volatile uint32_t lastStopEdgeUs = 0;

uint32_t packetCounter = 0;
uint32_t lastHeartbeatMs = 0;
bool espNowReady = false;

void IRAM_ATTR startInterrupt() {
  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastStartEdgeUs) < DEBOUNCE_US) return;
  lastStartEdgeUs = nowUs;
  startCapturedUs = nowUs;
  startPending = true;
}

void IRAM_ATTR stopInterrupt() {
  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastStopEdgeUs) < DEBOUNCE_US) return;
  lastStopEdgeUs = nowUs;
  stopCapturedUs = nowUs;
  stopPending = true;
}

void blink(uint8_t count, uint16_t onMs = 40, uint16_t offMs = 60) {
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(onMs);
    digitalWrite(LED_BUILTIN, HIGH);
    if (i + 1 < count) delay(offMs);
  }
}

void onSend(uint8_t* mac, uint8_t status) {
  (void)mac;
  Serial.print(F("ESP-NOW Zustellung: "));
  Serial.println(status == 0 ? F("OK") : F("FEHLER"));
}

bool sendPacket(uint8_t eventType, uint32_t capturedUs, uint8_t repetitions) {
  if (!espNowReady) return false;

  RemotePacket packet;
  packet.packetId = ++packetCounter;
  packet.eventType = eventType;
  packet.senderId = SENDER_ID;
  packet.remoteMicros = capturedUs;

  bool queued = false;

  for (uint8_t i = 0; i < repetitions; i++) {
    const int result = esp_now_send(
      masterMac,
      reinterpret_cast<uint8_t*>(&packet),
      sizeof(packet)
    );

    if (result == 0) queued = true;
    if (i + 1 < repetitions) delay(15);
  }

  return queued;
}

bool startEspNow() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);

  wifi_set_channel(WIFI_CHANNEL);

  if (esp_now_init() != 0) return false;

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onSend);

  // Beim ESP8266 muss auch die Broadcast-Adresse als Peer registriert werden.
  const int peerResult = esp_now_add_peer(
    masterMac,
    ESP_NOW_ROLE_SLAVE,
    WIFI_CHANNEL,
    nullptr,
    0
  );

  // Falls der Peer nach einem Neustart bereits existiert, ESP-NOW trotzdem nutzen.
  return peerResult == 0 || esp_now_is_peer_exist(masterMac);
}

void printConfiguration() {
  Serial.println();
  Serial.println(F("FLA Timer START/STOP Slave"));
  Serial.println(F("--------------------------------"));
  Serial.println(F("D1 / GPIO5 = START gegen GND"));
  Serial.println(F("D2 / GPIO4 = STOPP gegen GND"));
  Serial.print(F("Sender-ID   = "));
  Serial.println(SENDER_ID);
  Serial.print(F("Funkkanal  = "));
  Serial.println(WIFI_CHANNEL);
  Serial.print(F("Slave MAC   = "));
  Serial.println(WiFi.macAddress());
  Serial.print(F("ESP-NOW     = "));
  Serial.println(espNowReady ? F("BEREIT") : F("FEHLER"));
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_STOP, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_START), startInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_STOP), stopInterrupt, FALLING);

  espNowReady = startEspNow();
  printConfiguration();

  if (espNowReady) {
    blink(2, 80, 80);
    sendPacket(EVENT_HEARTBEAT, micros(), 1);
    lastHeartbeatMs = millis();
  } else {
    blink(5, 100, 100);
  }
}

void loop() {
  bool sendStart = false;
  bool sendStop = false;
  uint32_t startUs = 0;
  uint32_t stopUs = 0;

  noInterrupts();
  if (startPending) {
    sendStart = true;
    startUs = startCapturedUs;
    startPending = false;
  }
  if (stopPending) {
    sendStop = true;
    stopUs = stopCapturedUs;
    stopPending = false;
  }
  interrupts();

  if (sendStart) {
    Serial.println(F("Taster D1: sende START"));
    if (sendPacket(EVENT_START, startUs, EVENT_REPETITIONS)) blink(1);
    else blink(3, 80, 80);
  }

  if (sendStop) {
    Serial.println(F("Taster D2: sende STOPP"));
    if (sendPacket(EVENT_STOP, stopUs, EVENT_REPETITIONS)) blink(1);
    else blink(3, 80, 80);
  }

  const uint32_t nowMs = millis();
  if ((uint32_t)(nowMs - lastHeartbeatMs) >= HEARTBEAT_MS) {
    lastHeartbeatMs = nowMs;
    Serial.println(F("Sende HEARTBEAT"));
    sendPacket(EVENT_HEARTBEAT, micros(), 1);
  }

  yield();
}
