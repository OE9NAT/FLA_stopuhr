#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <espnow.h>

extern "C"
{
  #include <user_interface.h>
}

// =====================================================
// WLAN UND WEBSERVER
// =====================================================

ESP8266WebServer server(80);

const char* WIFI_SSID = "FLA-Timer";
const char* WIFI_PASSWORD = "12345678";

const uint8_t WIFI_CHANNEL = 1;

// =====================================================
// PINBELEGUNG MASTER
// =====================================================

const uint8_t LOCAL_START_PIN = D1;
const uint8_t LOCAL_STOP_PIN  = D2;

// =====================================================
// DEEP-SLEEP-EINSTELLUNGEN
// =====================================================

// Nach 5 Minuten ohne Bedienung schlafen gehen.
const unsigned long DEEP_SLEEP_TIMEOUT_MS =
  5UL * 60UL * 1000UL;

// Während eines laufenden Rennens wird nicht geschlafen.
unsigned long lastUserActivityMillis = 0;

// =====================================================
// ESP-NOW-EVENTS
// =====================================================

enum EventType : uint8_t
{
  EVENT_START  = 1,
  EVENT_SPLIT1 = 2,
  EVENT_SPLIT2 = 3,
  EVENT_STOP   = 4
};

struct __attribute__((packed)) RemotePacket
{
  uint32_t packetId;
  uint8_t eventType;
  uint8_t senderId;
  uint32_t remoteMicros;
};

// =====================================================
// EEPROM UND DATENSTRUKTUR
// =====================================================

const uint8_t MAX_RUNS = 5;
const uint8_t COMMENT_LENGTH = 48;

const int EEPROM_SIZE = 1024;

const uint32_t EEPROM_MAGIC = 0x464C4134;
const uint8_t EEPROM_VERSION = 4;

struct RunRecord
{
  uint32_t runId;

  float raceTime;
  float split1;
  float split2;

  uint32_t startTimestamp;

  char comment[COMMENT_LENGTH];
};

struct StoredData
{
  uint32_t magic;
  uint8_t version;

  uint8_t recentCount;
  uint8_t bestCount;
  uint8_t reserved;

  uint32_t nextRunId;

  RunRecord recent[MAX_RUNS];
  RunRecord best[MAX_RUNS];
};

StoredData storedData;

// =====================================================
// LAUFSTATUS
// =====================================================

bool running = false;
bool finished = false;

uint32_t raceStartMicros = 0;
uint32_t raceStartTimestamp = 0;

float currentFinalTime = 0.0;
float currentSplit1 = 0.0;
float currentSplit2 = 0.0;

bool split1Received = false;
bool split2Received = false;

uint32_t currentRunId = 0;
bool newBest = false;

// =====================================================
// SMARTPHONE-UHRZEIT
// =====================================================

bool timeSynchronized = false;

uint32_t synchronizedEpoch = 0;
unsigned long synchronizedAtMillis = 0;

// =====================================================
// LOKALE INTERRUPTS
//
// Absichtlich keine Software-Entprellzeit.
// Es wird die erste fallende Flanke erfasst.
// Weitere Start- oder Stop-Ereignisse werden durch die
// Zustandsmaschine ignoriert.
// =====================================================

volatile bool localStartPending = false;
volatile bool localStopPending = false;

volatile uint32_t localStartCapturedMicros = 0;
volatile uint32_t localStopCapturedMicros = 0;

// =====================================================
// ESP-NOW EMPFANG
// =====================================================

volatile bool remoteEventPending = false;

volatile uint8_t pendingRemoteEventType = 0;
volatile uint8_t pendingSenderId = 0;

volatile uint32_t pendingPacketId = 0;
volatile uint32_t pendingReceiveMicros = 0;

// Sender-ID hat 256 mögliche Werte.
uint32_t lastPacketId[256];

// =====================================================
// STATUS-LED
// =====================================================

unsigned long previousLedMillis = 0;
bool ledState = false;

// =====================================================
// INTERRUPT-FUNKTIONEN
// =====================================================

ICACHE_RAM_ATTR void onLocalStart()
{
  if (!localStartPending)
  {
    localStartCapturedMicros = micros();
    localStartPending = true;
  }
}

ICACHE_RAM_ATTR void onLocalStop()
{
  if (!localStopPending)
  {
    localStopCapturedMicros = micros();
    localStopPending = true;
  }
}

// =====================================================
// AKTIVITÄT REGISTRIEREN
// =====================================================

void registerUserActivity()
{
  lastUserActivityMillis = millis();
}

// =====================================================
// AKTUELLE SMARTPHONE-ZEIT
// =====================================================

uint32_t getCurrentEpoch()
{
  if (!timeSynchronized)
  {
    return 0;
  }

  unsigned long elapsedSeconds =
    (millis() - synchronizedAtMillis) / 1000UL;

  return synchronizedEpoch + elapsedSeconds;
}

// =====================================================
// EEPROM INITIALISIEREN
// =====================================================

void initializeStorage()
{
  memset(&storedData, 0, sizeof(storedData));

  storedData.magic = EEPROM_MAGIC;
  storedData.version = EEPROM_VERSION;

  storedData.recentCount = 0;
  storedData.bestCount = 0;

  storedData.nextRunId = 1;

  EEPROM.put(0, storedData);
  EEPROM.commit();

  Serial.println("EEPROM wurde initialisiert.");
}

// =====================================================
// EEPROM LADEN
// =====================================================

void loadStorage()
{
  EEPROM.get(0, storedData);

  bool valid = true;

  if (storedData.magic != EEPROM_MAGIC)
  {
    valid = false;
  }

  if (storedData.version != EEPROM_VERSION)
  {
    valid = false;
  }

  if (storedData.recentCount > MAX_RUNS)
  {
    valid = false;
  }

  if (storedData.bestCount > MAX_RUNS)
  {
    valid = false;
  }

  if (!valid)
  {
    Serial.println(
      "Keine passenden EEPROM-Daten gefunden."
    );

    initializeStorage();
  }
  else
  {
    Serial.println("EEPROM-Daten geladen.");
  }

  if (storedData.nextRunId == 0)
  {
    storedData.nextRunId = 1;
  }

  if (storedData.recentCount > 0)
  {
    currentFinalTime =
      storedData.recent[0].raceTime;

    currentSplit1 =
      storedData.recent[0].split1;

    currentSplit2 =
      storedData.recent[0].split2;

    currentRunId =
      storedData.recent[0].runId;

    finished = true;
  }
}

// =====================================================
// EEPROM SPEICHERN
// =====================================================

void saveStorage()
{
  storedData.magic = EEPROM_MAGIC;
  storedData.version = EEPROM_VERSION;

  EEPROM.put(0, storedData);

  if (EEPROM.commit())
  {
    Serial.println("EEPROM erfolgreich gespeichert.");
  }
  else
  {
    Serial.println("FEHLER beim EEPROM-Speichern.");
  }
}

// =====================================================
// LETZTEN LAUF HINZUFÜGEN
// =====================================================

void addRecentRun(const RunRecord& newRun)
{
  for (int i = MAX_RUNS - 1; i > 0; i--)
  {
    storedData.recent[i] =
      storedData.recent[i - 1];
  }

  storedData.recent[0] = newRun;

  if (storedData.recentCount < MAX_RUNS)
  {
    storedData.recentCount++;
  }
}

// =====================================================
// BESTZEIT HINZUFÜGEN
// =====================================================

bool addBestRun(const RunRecord& newRun)
{
  bool isNewOverallBest =
    storedData.bestCount == 0 ||
    newRun.raceTime < storedData.best[0].raceTime;

  int insertPosition = -1;

  for (int i = 0; i < storedData.bestCount; i++)
  {
    if (newRun.raceTime <
        storedData.best[i].raceTime)
    {
      insertPosition = i;
      break;
    }
  }

  if (insertPosition == -1 &&
      storedData.bestCount < MAX_RUNS)
  {
    insertPosition = storedData.bestCount;
  }

  if (insertPosition == -1)
  {
    return false;
  }

  int lastPosition;

  if (storedData.bestCount < MAX_RUNS)
  {
    lastPosition = storedData.bestCount;
    storedData.bestCount++;
  }
  else
  {
    lastPosition = MAX_RUNS - 1;
  }

  for (int i = lastPosition;
       i > insertPosition;
       i--)
  {
    storedData.best[i] =
      storedData.best[i - 1];
  }

  storedData.best[insertPosition] = newRun;

  return isNewOverallBest;
}

// =====================================================
// KOMMENTAR IN BEIDEN LISTEN AKTUALISIEREN
// =====================================================

void updateCommentForRun(
  uint32_t targetRunId,
  const char* newComment)
{
  for (int i = 0;
       i < storedData.recentCount;
       i++)
  {
    if (storedData.recent[i].runId ==
        targetRunId)
    {
      strncpy(
        storedData.recent[i].comment,
        newComment,
        COMMENT_LENGTH - 1
      );

      storedData.recent[i]
        .comment[COMMENT_LENGTH - 1] = '\0';
    }
  }

  for (int i = 0;
       i < storedData.bestCount;
       i++)
  {
    if (storedData.best[i].runId ==
        targetRunId)
    {
      strncpy(
        storedData.best[i].comment,
        newComment,
        COMMENT_LENGTH - 1
      );

      storedData.best[i]
        .comment[COMMENT_LENGTH - 1] = '\0';
    }
  }
}

// =====================================================
// RENNEN STARTEN
// =====================================================

void startRace(
  uint32_t eventMicros,
  const char* source)
{
  if (running)
  {
    Serial.println(
      "START ignoriert: Lauf bereits aktiv."
    );

    return;
  }

  registerUserActivity();

  running = true;
  finished = false;
  newBest = false;

  raceStartMicros = eventMicros;
  raceStartTimestamp = getCurrentEpoch();

  currentFinalTime = 0.0;
  currentSplit1 = 0.0;
  currentSplit2 = 0.0;

  split1Received = false;
  split2Received = false;

  Serial.print("START von ");
  Serial.println(source);
}

// =====================================================
// ZWISCHENZEIT 1
// =====================================================

void recordSplit1(uint32_t eventMicros)
{
  if (!running)
  {
    Serial.println(
      "Zwischenzeit 1 ignoriert: Kein Lauf aktiv."
    );

    return;
  }

  if (split1Received)
  {
    Serial.println(
      "Zwischenzeit 1 bereits vorhanden."
    );

    return;
  }

  registerUserActivity();

  currentSplit1 =
    (uint32_t)(eventMicros - raceStartMicros)
    / 1000000.0;

  split1Received = true;

  Serial.print("ZWISCHENZEIT 1: ");
  Serial.print(currentSplit1, 3);
  Serial.println(" s");
}

// =====================================================
// ZWISCHENZEIT 2
// =====================================================

void recordSplit2(uint32_t eventMicros)
{
  if (!running)
  {
    Serial.println(
      "Zwischenzeit 2 ignoriert: Kein Lauf aktiv."
    );

    return;
  }

  if (split2Received)
  {
    Serial.println(
      "Zwischenzeit 2 bereits vorhanden."
    );

    return;
  }

  registerUserActivity();

  currentSplit2 =
    (uint32_t)(eventMicros - raceStartMicros)
    / 1000000.0;

  split2Received = true;

  Serial.print("ZWISCHENZEIT 2: ");
  Serial.print(currentSplit2, 3);
  Serial.println(" s");
}

// =====================================================
// RENNEN STOPPEN
// =====================================================

void stopRace(
  uint32_t eventMicros,
  const char* source)
{
  if (!running)
  {
    Serial.println(
      "STOP ignoriert: Kein Lauf aktiv."
    );

    return;
  }

  registerUserActivity();

  currentFinalTime =
    (uint32_t)(eventMicros - raceStartMicros)
    / 1000000.0;

  running = false;
  finished = true;

  RunRecord newRun;

  memset(&newRun, 0, sizeof(newRun));

  newRun.runId = storedData.nextRunId++;

  newRun.raceTime = currentFinalTime;

  newRun.split1 =
    split1Received ? currentSplit1 : 0.0;

  newRun.split2 =
    split2Received ? currentSplit2 : 0.0;

  newRun.startTimestamp =
    raceStartTimestamp;

  newRun.comment[0] = '\0';

  addRecentRun(newRun);

  newBest = addBestRun(newRun);

  currentRunId = newRun.runId;

  saveStorage();

  Serial.print("STOP von ");
  Serial.print(source);
  Serial.print(": ");
  Serial.print(currentFinalTime, 3);
  Serial.println(" s");

  if (newBest)
  {
    Serial.println("NEUE BESTZEIT!");
  }
}

// =====================================================
// AKTUELLEN LAUF ABBRECHEN
// =====================================================

void resetCurrentRace()
{
  registerUserActivity();

  running = false;
  finished = false;
  newBest = false;

  currentFinalTime = 0.0;
  currentSplit1 = 0.0;
  currentSplit2 = 0.0;

  split1Received = false;
  split2Received = false;

  raceStartMicros = 0;
  raceStartTimestamp = 0;

  Serial.println("Aktueller Lauf wurde abgebrochen.");
}

// =====================================================
// ESP-NOW EMPFANG
// =====================================================

void onEspNowReceive(
  uint8_t* mac,
  uint8_t* incomingData,
  uint8_t length)
{
  if (length != sizeof(RemotePacket))
  {
    return;
  }

  RemotePacket packet;

  memcpy(
    &packet,
    incomingData,
    sizeof(packet)
  );

  if (packet.senderId == 0)
  {
    return;
  }

  // Wiederholte Pakete erkennen.
  if (lastPacketId[packet.senderId] ==
      packet.packetId)
  {
    return;
  }

  lastPacketId[packet.senderId] =
    packet.packetId;

  // Nur ein Ereignis gleichzeitig puffern.
  if (!remoteEventPending)
  {
    pendingRemoteEventType =
      packet.eventType;

    pendingSenderId =
      packet.senderId;

    pendingPacketId =
      packet.packetId;

    // Empfangszeitpunkt des Masters.
    pendingReceiveMicros = micros();

    remoteEventPending = true;
  }
}

// =====================================================
// REMOTE-EREIGNIS VERARBEITEN
// =====================================================

void processRemoteEvent()
{
  if (!remoteEventPending)
  {
    return;
  }

  noInterrupts();

  uint8_t eventType =
    pendingRemoteEventType;

  uint8_t senderId =
    pendingSenderId;

  uint32_t receiveMicros =
    pendingReceiveMicros;

  remoteEventPending = false;

  interrupts();

  char source[32];

  snprintf(
    source,
    sizeof(source),
    "Remote %u",
    senderId
  );

  switch (eventType)
  {
    case EVENT_START:
      startRace(receiveMicros, source);
      break;

    case EVENT_SPLIT1:
      recordSplit1(receiveMicros);
      break;

    case EVENT_SPLIT2:
      recordSplit2(receiveMicros);
      break;

    case EVENT_STOP:
      stopRace(receiveMicros, source);
      break;

    default:
      Serial.println(
        "Unbekanntes Remote-Ereignis."
      );
      break;
  }
}

// =====================================================
// LOKALE INTERRUPT-EREIGNISSE VERARBEITEN
// =====================================================

void processLocalButtons()
{
  if (localStartPending)
  {
    noInterrupts();

    uint32_t capturedMicros =
      localStartCapturedMicros;

    localStartPending = false;

    interrupts();

    startRace(
      capturedMicros,
      "Master-Taster"
    );
  }

  if (localStopPending)
  {
    noInterrupts();

    uint32_t capturedMicros =
      localStopCapturedMicros;

    localStopPending = false;

    interrupts();

    stopRace(
      capturedMicros,
      "Master-Taster"
    );
  }
}

// =====================================================
// HTML-WEBSEITE
// =====================================================

String createPage()
{
  return R"rawliteral(
<!DOCTYPE html>
<html lang="de">

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>FLA Timer</title>

<style>

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  padding: 16px;
  font-family: Arial, Helvetica, sans-serif;
  text-align: center;
  background: #eeeeee;
  color: #222222;
}

.container {
  max-width: 780px;
  margin: auto;
}

.header {
  background: linear-gradient(135deg,#b71c1c,#d32f2f);
  color: white;
  padding: 22px;
  border-radius: 16px;
  box-shadow: 0 3px 12px rgba(0,0,0,.20);
}

.header h1 {
  margin: 0;
  font-size: 34px;
}

.subtitle {
  margin-top: 8px;
  opacity: .9;
}

.box {
  margin-top: 15px;
  padding: 20px;
  background: white;
  border-radius: 16px;
  box-shadow: 0 3px 10px rgba(0,0,0,.12);
}

#status {
  display: inline-block;
  padding: 10px 24px;
  color: white;
  background: #2e7d32;
  border-radius: 25px;
  font-size: 22px;
  font-weight: bold;
}

.timer {
  margin: 18px 0 8px 0;
  color: #0066cc;
  font-size: clamp(60px,17vw,115px);
  font-weight: bold;
  font-variant-numeric: tabular-nums;
}

.seconds-label {
  color: #666666;
  margin-bottom: 18px;
}

.reset-button {
  width: 100%;
  min-height: 72px;
  padding: 16px;
  border: 0;
  border-radius: 14px;
  background: #d32f2f;
  color: white;
  font-size: 24px;
  font-weight: bold;
  cursor: pointer;
}

.reset-button:active {
  background: #8e0000;
  transform: scale(.98);
}

.split-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
  margin-top: 18px;
}

.split {
  padding: 16px;
  background: #f5f5f5;
  border-radius: 12px;
}

.split-value {
  display: block;
  margin-top: 8px;
  font-size: 23px;
  font-weight: bold;
  color: #1565c0;
}

.comment-input {
  width: 100%;
  padding: 14px;
  border: 1px solid #bbbbbb;
  border-radius: 10px;
  font-size: 17px;
}

.comment-button {
  width: 100%;
  margin-top: 10px;
  padding: 14px;
  border: 0;
  border-radius: 10px;
  background: #1565c0;
  color: white;
  font-size: 18px;
  font-weight: bold;
}

.message {
  margin-top: 10px;
  min-height: 20px;
  color: #2e7d32;
}

table {
  width: 100%;
  border-collapse: collapse;
}

th,
td {
  padding: 10px 6px;
  border-bottom: 1px solid #dddddd;
  vertical-align: top;
}

th {
  background: #eeeeee;
}

.time-value {
  font-weight: bold;
  white-space: nowrap;
}

.comment-cell {
  color: #555555;
  font-size: 14px;
  text-align: left;
}

.footer {
  margin: 20px;
  color: #777777;
  font-size: 13px;
}

@media(max-width:600px) {

  .split-grid {
    grid-template-columns: 1fr;
  }

  th,
  td {
    padding: 8px 4px;
    font-size: 13px;
  }
}

</style>

</head>

<body>

<div class="container">

  <div class="header">
    <h1>🚒 FLA TIMER</h1>
    <div class="subtitle">
      Feuerwehr-Trainingszeitmessung
    </div>
  </div>

  <div class="box">

    <div id="status">
      READY
    </div>

    <div id="time"
         class="timer">
      0.000
    </div>

    <div class="seconds-label">
      Sekunden
    </div>

    <button
      class="reset-button"
      onclick="abortRace()">
      RESET / LAUF ABBRECHEN
    </button>

    <div class="split-grid">

      <div class="split">
        <b>Zwischenzeit 1</b>

        <span
          id="split1"
          class="split-value">
          --
        </span>
      </div>

      <div class="split">
        <b>Zwischenzeit 2</b>

        <span
          id="split2"
          class="split-value">
          --
        </span>
      </div>

    </div>

    <div
      id="syncStatus"
      class="message">
      Smartphone-Uhrzeit wird synchronisiert ...
    </div>

  </div>

  <div class="box">

    <h2>Kommentar zum letzten Lauf</h2>

    <input
      id="comment"
      class="comment-input"
      type="text"
      maxlength="47"
      placeholder="z. B. Fehler beim Kuppeln">

    <button
      class="comment-button"
      onclick="saveComment()">
      Kommentar speichern
    </button>

    <div
      id="commentMessage"
      class="message">
    </div>

  </div>

  <div class="box">

    <h2>Letzte 5 Läufe</h2>

    <table>

      <thead>
        <tr>
          <th>#</th>
          <th>Start</th>
          <th>Z1</th>
          <th>Z2</th>
          <th>Gesamt</th>
        </tr>
      </thead>

      <tbody id="recent">
        <tr>
          <td colspan="5">
            Noch keine Läufe vorhanden
          </td>
        </tr>
      </tbody>

    </table>

  </div>

  <div class="box">

    <h2>Top 5 Bestzeiten</h2>

    <table>

      <thead>
        <tr>
          <th>Platz</th>
          <th>Start</th>
          <th>Z1</th>
          <th>Z2</th>
          <th>Gesamt</th>
        </tr>
      </thead>

      <tbody id="best">
        <tr>
          <td colspan="5">
            Noch keine Bestzeiten vorhanden
          </td>
        </tr>
      </tbody>

    </table>

  </div>

  <div class="footer">
    FLA Timer · ESP8266 · ESP-NOW
  </div>

</div>

<script>

let shownBestRunId = 0;
let loadedCommentRunId = 0;

function formatTime(value)
{
  if (!value || Number(value) <= 0)
  {
    return "--";
  }

  return Number(value).toFixed(3) + " s";
}

function formatTimestamp(timestamp)
{
  if (!timestamp || timestamp === 0)
  {
    return "--";
  }

  const date = new Date(timestamp * 1000);

  return date.toLocaleDateString("de-AT") +
         " " +
         date.toLocaleTimeString("de-AT");
}

function escapeHtml(text)
{
  const element =
    document.createElement("div");

  element.textContent = text || "";

  return element.innerHTML;
}

function createRows(runs)
{
  if (!runs || runs.length === 0)
  {
    return '<tr>' +
           '<td colspan="5">' +
           'Noch keine Zeiten vorhanden' +
           '</td>' +
           '</tr>';
  }

  let html = "";

  runs.forEach(function(run,index)
  {
    html += "<tr>";

    html += "<td>" +
            (index + 1) +
            "</td>";

    html += "<td>" +
            formatTimestamp(run.timestamp) +
            "</td>";

    html += "<td>" +
            formatTime(run.split1) +
            "</td>";

    html += "<td>" +
            formatTime(run.split2) +
            "</td>";

    html += '<td class="time-value">' +
            formatTime(run.time) +
            "</td>";

    html += "</tr>";

    if (run.comment)
    {
      html += "<tr>";

      html += '<td colspan="5" ' +
              'class="comment-cell">' +
              "Kommentar: " +
              escapeHtml(run.comment) +
              "</td>";

      html += "</tr>";
    }
  });

  return html;
}

function synchronizeTime()
{
  const epochSeconds =
    Math.floor(Date.now() / 1000);

  fetch("/sync?epoch=" + epochSeconds)
    .then(function(response)
    {
      return response.text();
    })
    .then(function()
    {
      document.getElementById(
        "syncStatus"
      ).innerHTML =
        "Smartphone-Uhrzeit aktiv";
    })
    .catch(function()
    {
      document.getElementById(
        "syncStatus"
      ).innerHTML =
        "Uhrzeit konnte nicht synchronisiert werden";
    });
}

function abortRace()
{
  if (!confirm(
    "Aktuellen Lauf wirklich abbrechen?"
  ))
  {
    return;
  }

  fetch("/reset",
  {
    method: "POST"
  })
  .then(function(response)
  {
    return response.text();
  })
  .then(function()
  {
    updateStatus();
  });
}

function saveComment()
{
  const comment =
    document.getElementById("comment").value;

  fetch("/comment",
  {
    method: "POST",

    headers:
    {
      "Content-Type":
        "application/x-www-form-urlencoded"
    },

    body:
      "text=" +
      encodeURIComponent(comment)
  })
  .then(function(response)
  {
    if (!response.ok)
    {
      throw new Error(
        "Kommentar konnte nicht gespeichert werden"
      );
    }

    return response.text();
  })
  .then(function()
  {
    document.getElementById(
      "commentMessage"
    ).innerHTML =
      "Kommentar gespeichert";

    updateStatus();
  })
  .catch(function(error)
  {
    document.getElementById(
      "commentMessage"
    ).innerHTML =
      error.message;
  });
}

function updateStatus()
{
  fetch("/status")
    .then(function(response)
    {
      return response.json();
    })
    .then(function(data)
    {
      const status =
        document.getElementById("status");

      status.innerHTML = data.status;

      if (data.status === "RUNNING")
      {
        status.style.background = "#ef6c00";
      }
      else if (data.status === "FINISHED")
      {
        status.style.background = "#1565c0";
      }
      else
      {
        status.style.background = "#2e7d32";
      }

      document.getElementById(
        "time"
      ).innerHTML =
        Number(data.current).toFixed(3);

      document.getElementById(
        "split1"
      ).innerHTML =
        formatTime(data.split1);

      document.getElementById(
        "split2"
      ).innerHTML =
        formatTime(data.split2);

      document.getElementById(
        "recent"
      ).innerHTML =
        createRows(data.recent);

      document.getElementById(
        "best"
      ).innerHTML =
        createRows(data.best);

      if (
        data.recent &&
        data.recent.length > 0 &&
        data.recent[0].runId !==
          loadedCommentRunId
      )
      {
        loadedCommentRunId =
          data.recent[0].runId;

        document.getElementById(
          "comment"
        ).value =
          data.recent[0].comment || "";
      }

      if (
        data.newBest &&
        data.runId !== shownBestRunId
      )
      {
        shownBestRunId = data.runId;

        alert(
          "🏆 NEUE BESTZEIT! 🏆\n\n" +
          Number(data.last).toFixed(3) +
          " Sekunden"
        );
      }
    })
    .catch(function()
    {
      document.getElementById(
        "syncStatus"
      ).innerHTML =
        "Keine Verbindung zum ESP8266";
    });
}

synchronizeTime();

setInterval(
  synchronizeTime,
  60000
);

setInterval(
  updateStatus,
  100
);

updateStatus();

</script>

</body>
</html>
)rawliteral";
}

// =====================================================
// HTML HAUPTSEITE
// =====================================================

void handleRoot()
{
  server.sendHeader(
    "Cache-Control",
    "no-cache"
  );

  server.send(
    200,
    "text/html; charset=utf-8",
    createPage()
  );
}

// =====================================================
// JSON-LAUFLISTE ERZEUGEN
// =====================================================

void appendRunsJson(
  String& json,
  RunRecord* runs,
  uint8_t count)
{
  json += "[";

  for (uint8_t i = 0; i < count; i++)
  {
    if (i > 0)
    {
      json += ",";
    }

    json += "{";

    json += "\"runId\":";
    json += String(runs[i].runId);
    json += ",";

    json += "\"time\":";
    json += String(runs[i].raceTime, 3);
    json += ",";

    json += "\"split1\":";
    json += String(runs[i].split1, 3);
    json += ",";

    json += "\"split2\":";
    json += String(runs[i].split2, 3);
    json += ",";

    json += "\"timestamp\":";
    json += String(runs[i].startTimestamp);
    json += ",";

    json += "\"comment\":\"";

    // Einfache JSON-Bereinigung.
    for (uint8_t c = 0;
         c < COMMENT_LENGTH &&
         runs[i].comment[c] != '\0';
         c++)
    {
      char character = runs[i].comment[c];

      if (character == '"' ||
          character == '\\')
      {
        json += '\\';
      }

      if (character >= 32)
      {
        json += character;
      }
    }

    json += "\"";

    json += "}";
  }

  json += "]";
}

// =====================================================
// STATUS-JSON
// =====================================================

void handleStatus()
{
  float visibleTime = currentFinalTime;

  if (running)
  {
    visibleTime =
      (uint32_t)(micros() - raceStartMicros)
      / 1000000.0;
  }

  String statusText = "READY";

  if (running)
  {
    statusText = "RUNNING";
  }
  else if (finished)
  {
    statusText = "FINISHED";
  }

  String json;

  json.reserve(3000);

  json = "{";

  json += "\"status\":\"";
  json += statusText;
  json += "\",";

  json += "\"current\":";
  json += String(visibleTime, 3);
  json += ",";

  json += "\"last\":";
  json += String(currentFinalTime, 3);
  json += ",";

  json += "\"split1\":";
  json += String(currentSplit1, 3);
  json += ",";

  json += "\"split2\":";
  json += String(currentSplit2, 3);
  json += ",";

  json += "\"runId\":";
  json += String(currentRunId);
  json += ",";

  json += "\"newBest\":";
  json += newBest ? "true" : "false";
  json += ",";

  json += "\"recent\":";

  appendRunsJson(
    json,
    storedData.recent,
    storedData.recentCount
  );

  json += ",\"best\":";

  appendRunsJson(
    json,
    storedData.best,
    storedData.bestCount
  );

  json += "}";

  server.sendHeader(
    "Cache-Control",
    "no-cache"
  );

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// RESET ÜBER WEBSEITE
// =====================================================

void handleReset()
{
  resetCurrentRace();

  server.send(
    200,
    "text/plain",
    "Lauf abgebrochen"
  );
}

// =====================================================
// KOMMENTAR SPEICHERN
// =====================================================

void handleSaveComment()
{
  registerUserActivity();

  if (storedData.recentCount == 0)
  {
    server.send(
      400,
      "text/plain",
      "Noch kein Lauf vorhanden"
    );

    return;
  }

  if (!server.hasArg("text"))
  {
    server.send(
      400,
      "text/plain",
      "Kommentar fehlt"
    );

    return;
  }

  String comment = server.arg("text");

  comment.trim();

  if (comment.length() >= COMMENT_LENGTH)
  {
    comment =
      comment.substring(
        0,
        COMMENT_LENGTH - 1
      );
  }

  uint32_t targetRunId =
    storedData.recent[0].runId;

  updateCommentForRun(
    targetRunId,
    comment.c_str()
  );

  saveStorage();

  Serial.print("Kommentar gespeichert: ");
  Serial.println(comment);

  server.send(
    200,
    "text/plain",
    "Kommentar gespeichert"
  );
}

// =====================================================
// SMARTPHONE-UHRZEIT ÜBERNEHMEN
// =====================================================

void handleTimeSync()
{
  if (!server.hasArg("epoch"))
  {
    server.send(
      400,
      "text/plain",
      "Epoch fehlt"
    );

    return;
  }

  uint32_t receivedEpoch =
    (uint32_t)strtoul(
      server.arg("epoch").c_str(),
      nullptr,
      10
    );

  // Zeitstempel vor 2020 ablehnen.
  if (receivedEpoch < 1577836800UL)
  {
    server.send(
      400,
      "text/plain",
      "Ungueltige Uhrzeit"
    );

    return;
  }

  synchronizedEpoch = receivedEpoch;
  synchronizedAtMillis = millis();
  timeSynchronized = true;

  Serial.print("Uhrzeit synchronisiert: ");
  Serial.println(receivedEpoch);

  server.send(
    200,
    "text/plain",
    "Zeit synchronisiert"
  );
}

// =====================================================
// STATUS-LED
// =====================================================

void updateStatusLED()
{
  unsigned long interval;

  if (running)
  {
    // Schnelles Blinken während eines Laufes.
    interval = 100;
  }
  else
  {
    // Langsames Lebenszeichen.
    interval = 1000;
  }

  if (millis() - previousLedMillis >= interval)
  {
    previousLedMillis = millis();

    ledState = !ledState;

    // Onboard-LED des ESP8266 ist active LOW.
    digitalWrite(
      LED_BUILTIN,
      ledState ? LOW : HIGH
    );
  }
}

// =====================================================
// DEEP SLEEP
// =====================================================

void checkDeepSleep()
{
  // Nie während eines laufenden Rennens schlafen.
  if (running)
  {
    return;
  }

  if (
    millis() - lastUserActivityMillis
    < DEEP_SLEEP_TIMEOUT_MS
  )
  {
    return;
  }

  Serial.println();
  Serial.println("==============================");
  Serial.println("5 Minuten ohne Bedienung.");
  Serial.println("ESP8266 geht in Deep Sleep.");
  Serial.println(
    "Aufwecken durch RESET oder Power-Cycle."
  );
  Serial.println("==============================");

  digitalWrite(LED_BUILTIN, HIGH);

  saveStorage();

  delay(250);

  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);

  delay(250);

  // 0 = unbegrenzt schlafen.
  // Aufwecken nur über Reset oder Power-Cycle.
  ESP.deepSleep(0);
}

// =====================================================
// WLAN UND ESP-NOW STARTEN
// =====================================================

void startCommunication()
{
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);

  delay(500);

  // AP für Webseite und STA für ESP-NOW.
  WiFi.mode(WIFI_AP_STA);

  delay(500);

  bool apStarted = WiFi.softAP(
    WIFI_SSID,
    WIFI_PASSWORD,
    WIFI_CHANNEL
  );

  if (apStarted)
  {
    Serial.println(
      "WLAN Access Point gestartet."
    );
  }
  else
  {
    Serial.println(
      "FEHLER beim WLAN Access Point."
    );
  }

  wifi_set_channel(WIFI_CHANNEL);

  Serial.print("Web-IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("Master STA MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Master AP MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  if (esp_now_init() != 0)
  {
    Serial.println(
      "ESP-NOW Initialisierung fehlgeschlagen."
    );

    return;
  }

  esp_now_set_self_role(
    ESP_NOW_ROLE_COMBO
  );

  esp_now_register_recv_cb(
    onEspNowReceive
  );

  Serial.println(
    "ESP-NOW Empfaenger bereit."
  );
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("FLA MASTER STARTET");
  Serial.println("==============================");

  memset(
    lastPacketId,
    0,
    sizeof(lastPacketId)
  );

  pinMode(
    LOCAL_START_PIN,
    INPUT_PULLUP
  );

  pinMode(
    LOCAL_STOP_PIN,
    INPUT_PULLUP
  );

  pinMode(
    LED_BUILTIN,
    OUTPUT
  );

  digitalWrite(
    LED_BUILTIN,
    HIGH
  );

  attachInterrupt(
    digitalPinToInterrupt(
      LOCAL_START_PIN
    ),
    onLocalStart,
    FALLING
  );

  attachInterrupt(
    digitalPinToInterrupt(
      LOCAL_STOP_PIN
    ),
    onLocalStop,
    FALLING
  );

  EEPROM.begin(EEPROM_SIZE);

  loadStorage();

  startCommunication();

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/status",
    HTTP_GET,
    handleStatus
  );

  server.on(
    "/reset",
    HTTP_POST,
    handleReset
  );

  server.on(
    "/comment",
    HTTP_POST,
    handleSaveComment
  );

  server.on(
    "/sync",
    HTTP_GET,
    handleTimeSync
  );

  server.onNotFound([]()
  {
    server.sendHeader(
      "Location",
      "/"
    );

    server.send(
      302,
      "text/plain",
      ""
    );
  });

  server.begin();

  lastUserActivityMillis = millis();

  Serial.println("Webserver bereit.");
  Serial.println("WLAN: FLA-Timer");
  Serial.println("Passwort: 12345678");
  Serial.println("Webseite: http://192.168.4.1");
  Serial.println();
  Serial.println("D1 = lokaler START");
  Serial.println("D2 = lokaler STOPP");
  Serial.println("==============================");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  server.handleClient();

  processLocalButtons();
  processRemoteEvent();

  updateStatusLED();
  checkDeepSleep();

  yield();
}