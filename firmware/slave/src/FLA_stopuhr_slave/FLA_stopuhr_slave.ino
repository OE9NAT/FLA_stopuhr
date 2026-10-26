/*
 * FLA Timer Slave
 * ESP8266 NodeMCU
 *
 * Sends timing events to Master via ESP-NOW
 */

#include <ESP8266WiFi.h>
#include <espnow.h>

#define EVENT_PIN D1
#define LED_PIN LED_BUILTIN

enum EventType
{
    EVENT_START = 1,
    EVENT_SPLIT1 = 2,
    EVENT_SPLIT2 = 3,
    EVENT_STOP = 4
};

// Ändern pro Station
const uint8_t EVENT_ID = EVENT_START;

typedef struct
{
    uint8_t eventType;
    uint32_t counter;
} EventPacket;

uint8_t masterAddress[] =
{
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

volatile bool triggerEvent = false;
uint32_t eventCounter = 0;

void ICACHE_RAM_ATTR buttonISR()
{
    triggerEvent = true;
}

void onDataSent(uint8_t *mac_addr, uint8_t sendStatus)
{
    digitalWrite(LED_PIN, LOW);
    delay(50);
    digitalWrite(LED_PIN, HIGH);
}

void setup()
{
    Serial.begin(9600);

    pinMode(EVENT_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);

    digitalWrite(LED_PIN, HIGH);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != 0)
    {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);

    esp_now_register_send_cb(onDataSent);

    esp_now_add_peer(
        masterAddress,
        ESP_NOW_ROLE_SLAVE,
        1,
        NULL,
        0);

    attachInterrupt(
        digitalPinToInterrupt(EVENT_PIN),
        buttonISR,
        FALLING);

    Serial.println("Slave Ready");
}

void sendEvent()
{
    EventPacket packet;

    packet.eventType = EVENT_ID;
    packet.counter = ++eventCounter;

    esp_now_send(
        masterAddress,
        (uint8_t *)&packet,
        sizeof(packet));

    Serial.print("Event sent: ");
    Serial.println(EVENT_ID);
}

void loop()
{
    if (triggerEvent)
    {
        triggerEvent = false;

        sendEvent();

        delay(300); // debounce
    }
}