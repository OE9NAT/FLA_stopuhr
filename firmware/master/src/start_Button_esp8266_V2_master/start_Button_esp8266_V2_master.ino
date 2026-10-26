#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <espnow.h>

// FLA Timer MASTER only
// Board: NodeMCU 1.0 (ESP-12E Module)
// Keeps: timer, two splits, reset, EEPROM, last 5, Top 5, comments,
// smartphone time sync, best-time popup, LED and deep sleep.
// Adds: connection status for three slaves only: Split 1, Split 2 and Finish.

constexpr char AP_SSID[]="FLA-Timer";
constexpr char AP_PASSWORD[]="12345678";
constexpr uint8_t WIFI_CHANNEL=1;
constexpr uint8_t PIN_START=D1, PIN_STOP=D2;
constexpr uint32_t SLAVE_OFFLINE_MS=15000;
constexpr uint32_t BUTTON_DEBOUNCE_US=15000;
constexpr uint32_t SLEEP_MS=300000;
constexpr uint16_t EEPROM_SIZE=2048;
constexpr uint32_t EEPROM_MAGIC=0x464C4132;
constexpr uint16_t EEPROM_VERSION=2;
constexpr uint8_t MAX_RUNS=5;

enum EventType:uint8_t{EVENT_START=1,EVENT_SPLIT1=2,EVENT_SPLIT2=3,EVENT_STOP=4,EVENT_HEARTBEAT=100};
enum RaceState:uint8_t{READY,RUNNING,FINISHED};

struct __attribute__((packed)) RemotePacket{uint32_t packetId;uint8_t eventType;uint8_t senderId;uint32_t remoteMicros;};
struct RunRecord{uint32_t runId;float raceTime;float split1;float split2;uint32_t startTimestamp;char comment[48];};
struct StoredData{uint32_t magic;uint16_t version;uint8_t lastCount;uint8_t topCount;uint32_t nextRunId;RunRecord last[MAX_RUNS];RunRecord top[MAX_RUNS];};
struct SlaveStatus{uint8_t senderId;const char* name;const char* role;uint32_t lastSeenMs;uint32_t packets;uint32_t lastPacketId;uint8_t mac[6];bool macKnown;};

// No START slave is displayed because START is on the master.
SlaveStatus slaves[]={
 {2,"Slave 1","ZWISCHENZEIT 1",0,0,0,{0},false},
 {3,"Slave 2","ZWISCHENZEIT 2",0,0,0,{0},false},
 {4,"Slave 3","ENDE",0,0,0,{0},false}
};
constexpr size_t SLAVE_COUNT=sizeof(slaves)/sizeof(slaves[0]);

ESP8266WebServer server(80);
StoredData store; RaceState state=READY;
uint32_t startUs=0,stopUs=0,split1Us=0,split2Us=0; bool split1Set=false,split2Set=false;
volatile bool startPending=false,stopPending=false;
volatile uint32_t startIrqUs=0,stopIrqUs=0,lastStartEdgeUs=0,lastStopEdgeUs=0;
uint32_t epochBase=0,epochBaseMillis=0,lastActivityMs=0,lastPopupRunId=0; bool newBest=false;

void IRAM_ATTR onStart(){uint32_t n=micros();if((uint32_t)(n-lastStartEdgeUs)<BUTTON_DEBOUNCE_US)return;lastStartEdgeUs=n;startIrqUs=n;startPending=true;}
void IRAM_ATTR onStop(){uint32_t n=micros();if((uint32_t)(n-lastStopEdgeUs)<BUTTON_DEBOUNCE_US)return;lastStopEdgeUs=n;stopIrqUs=n;stopPending=true;}
float elapsed(uint32_t a,uint32_t b){return (uint32_t)(a-b)/1000000.0f;}
const char* stateName(){return state==RUNNING?"RUNNING":state==FINISHED?"FINISHED":"READY";}
uint32_t nowEpoch(){return epochBase?epochBase+(millis()-epochBaseMillis)/1000:0;}
float currentTime(){return state==RUNNING?elapsed(micros(),startUs):state==FINISHED?elapsed(stopUs,startUs):0;}
void activity(){lastActivityMs=millis();}
void initStore(){memset(&store,0,sizeof(store));store.magic=EEPROM_MAGIC;store.version=EEPROM_VERSION;store.nextRunId=1;}
void saveStore(){EEPROM.put(0,store);EEPROM.commit();}
void loadStore(){EEPROM.get(0,store);if(store.magic!=EEPROM_MAGIC||store.version!=EEPROM_VERSION||store.lastCount>5||store.topCount>5){initStore();saveStore();}}
void copyRecord(RunRecord& d,const RunRecord& s){memcpy(&d,&s,sizeof(RunRecord));}

void insertLast(const RunRecord& r){for(int i=min<int>(store.lastCount,4);i>0;i--)copyRecord(store.last[i],store.last[i-1]);copyRecord(store.last[0],r);if(store.lastCount<5)store.lastCount++;}
void rebuildTop(const RunRecord& r){
 RunRecord temp[6];uint8_t n=0;for(uint8_t i=0;i<store.topCount;i++)copyRecord(temp[n++],store.top[i]);copyRecord(temp[n++],r);
 for(uint8_t i=0;i<n;i++)for(uint8_t j=i+1;j<n;j++)if(temp[j].raceTime<temp[i].raceTime){RunRecord x=temp[i];temp[i]=temp[j];temp[j]=x;}
 store.topCount=min<uint8_t>(n,5);for(uint8_t i=0;i<store.topCount;i++)copyRecord(store.top[i],temp[i]);
}
void completeRun(){
 RunRecord r{};r.runId=store.nextRunId++;r.raceTime=elapsed(stopUs,startUs);r.split1=split1Set?elapsed(split1Us,startUs):-1;r.split2=split2Set?elapsed(split2Us,startUs):-1;r.startTimestamp=nowEpoch();r.comment[0]=0;
 float oldBest=store.topCount?store.top[0].raceTime:1e9;insertLast(r);rebuildTop(r);newBest=r.raceTime<oldBest;lastPopupRunId=r.runId;saveStore();
}
void processEvent(uint8_t type,uint32_t t){activity();if(type==EVENT_START&&state!=RUNNING){state=RUNNING;startUs=t;stopUs=split1Us=split2Us=0;split1Set=split2Set=false;newBest=false;}else if(type==EVENT_SPLIT1&&state==RUNNING&&!split1Set){split1Us=t;split1Set=true;}else if(type==EVENT_SPLIT2&&state==RUNNING&&!split2Set){split2Us=t;split2Set=true;}else if(type==EVENT_STOP&&state==RUNNING){stopUs=t;state=FINISHED;completeRun();}}
SlaveStatus* findSlave(uint8_t id){for(auto &s:slaves)if(s.senderId==id)return &s;return nullptr;}
bool online(const SlaveStatus&s){return s.lastSeenMs&&(uint32_t)(millis()-s.lastSeenMs)<SLAVE_OFFLINE_MS;}
void onReceive(uint8_t* mac,uint8_t* data,uint8_t len){if(len!=sizeof(RemotePacket))return;RemotePacket p;memcpy(&p,data,sizeof(p));SlaveStatus*s=findSlave(p.senderId);if(!s)return;s->lastSeenMs=millis();s->packets++;memcpy(s->mac,mac,6);s->macKnown=true;if(p.eventType==EVENT_HEARTBEAT)return;if(s->lastPacketId==p.packetId)return;s->lastPacketId=p.packetId;processEvent(p.eventType,micros());}
String esc(const char*s){String o;for(;*s;s++){if(*s=='"')o+="\\\"";else if(*s=='\\')o+="\\\\";else if(*s=='\n')o+="\\n";else o+=*s;}return o;}
String macOf(const SlaveStatus&s){if(!s.macKnown)return "noch nicht gesehen";char b[18];snprintf(b,sizeof(b),"%02X:%02X:%02X:%02X:%02X:%02X",s.mac[0],s.mac[1],s.mac[2],s.mac[3],s.mac[4],s.mac[5]);return b;}
String recordJson(const RunRecord&r){String j="{\"id\":"+String(r.runId)+",\"time\":"+String(r.raceTime,3)+",\"s1\":"+String(r.split1,3)+",\"s2\":"+String(r.split2,3)+",\"ts\":"+String(r.startTimestamp)+",\"comment\":\""+esc(r.comment)+"\"}";return j;}
String statusJson(){
 String j="{\"state\":\""+String(stateName())+"\",\"time\":"+String(currentTime(),3)+",\"split1\":"+String(split1Set?elapsed(split1Us,startUs):-1,3)+",\"split2\":"+String(split2Set?elapsed(split2Us,startUs):-1,3)+",\"newBest\":"+String(newBest?"true":"false")+",\"popupRunId\":"+String(lastPopupRunId)+",\"last\":[";
 for(uint8_t i=0;i<store.lastCount;i++){if(i)j+=',';j+=recordJson(store.last[i]);}j+="],\"top\":[";for(uint8_t i=0;i<store.topCount;i++){if(i)j+=',';j+=recordJson(store.top[i]);}j+="],\"slaves\":[";
 for(size_t i=0;i<SLAVE_COUNT;i++){if(i)j+=',';auto&s=slaves[i];j+="{\"id\":"+String(s.senderId)+",\"name\":\""+s.name+"\",\"role\":\""+s.role+"\",\"online\":"+String(online(s)?"true":"false")+",\"age\":"+String(s.lastSeenMs?(millis()-s.lastSeenMs)/1000:0)+",\"packets\":"+String(s.packets)+",\"mac\":\""+macOf(s)+"\"}";}return j+"]}";
}

const char PAGE[] PROGMEM=R"HTML(
<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>FLA Timer</title><style>
:root{--r:#b71c1c;--g:#168447;--bg:#edf1f5;--ink:#172033}*{box-sizing:border-box}body{margin:0;background:var(--bg);font:15px system-ui;color:var(--ink)}main{max-width:980px;margin:auto;padding:14px}header,.card{border-radius:16px;padding:18px;margin-bottom:14px}header{background:var(--r);color:#fff;text-align:center}.card{background:#fff;box-shadow:0 4px 16px #0002}h1,h2{margin:0 0 10px}.state{text-align:center;font-weight:800;letter-spacing:.12em}.timer{text-align:center;font-size:clamp(52px,13vw,96px);font-weight:850;font-variant-numeric:tabular-nums}.btn{width:100%;border:0;border-radius:11px;padding:15px;background:#c62828;color:white;font-weight:800}.splits,.slaves{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-top:12px}.box{border:1px solid #dce2ea;border-radius:12px;padding:12px}.dot{display:inline-block;width:11px;height:11px;border-radius:50%;margin-right:7px}.on{background:var(--g)}.off{background:#d32f2f}.pill{float:right;background:#e8edf3;border-radius:999px;padding:3px 8px;font-size:12px}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px 6px;border-bottom:1px solid #e5e9ef}.muted{color:#667085;font-size:12px}.comment{display:flex;gap:7px}.comment input{min-width:120px;flex:1;padding:8px}.comment button{padding:8px}.popup{position:fixed;inset:0;background:#0008;display:none;place-items:center}.popup>div{background:#fff;padding:30px;border-radius:16px;text-align:center;font-size:24px}@media(max-width:650px){th:nth-child(3),td:nth-child(3),th:nth-child(4),td:nth-child(4){display:none}}
</style></head><body><main><header><h1>🚒 FLA TIMER</h1><div>Feuerwehr-Trainingszeitmessung</div></header>
<section class="card"><div id="state" class="state">READY</div><div id="timer" class="timer">0.000</div><button class="btn" onclick="resetRun()">RESET / LAUF ABBRECHEN</button><div class="splits"><div class="box">Zwischenzeit 1<br><b id="s1">--</b></div><div class="box">Zwischenzeit 2<br><b id="s2">--</b></div></div></section>
<section class="card"><h2>Letzter Lauf</h2><div id="latest"></div></section>
<section class="card"><h2>Top 5 Bestzeiten</h2><div id="top"></div></section>
<section class="card"><h2>Letzte 5 Durchgänge</h2><div id="last"></div></section>
<section class="card"><h2>Verbindungsstatus Slaves</h2><div id="slaves" class="slaves"></div></section></main><div id="popup" class="popup" onclick="this.style.display='none'"><div>🏆 <b>NEUE BESTZEIT!</b><br><span id="best"></span> s</div></div>
<script>
let shown=0,latestRenderedId=0,clockState='READY',clockValue=0,clockAt=performance.now();
const ft=n=>n<0?'--':Number(n).toFixed(3)+' s';const fd=t=>t?new Date(t*1000).toLocaleString():'Zeit nicht synchronisiert';
function safeComment(v){return String(v||'').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;')}
function latestRun(a){if(!a.length)return '<p class="muted">Noch kein Durchgang gespeichert.</p>';const r=a[0];return `<div class="box"><div style="font-size:34px;font-weight:850">${ft(r.time)}</div><div class="muted">${fd(r.ts)} · Split 1: ${ft(r.s1)} · Split 2: ${ft(r.s2)}</div><div class="comment" style="margin-top:12px"><input id="c${r.id}" maxlength="47" autocomplete="off" placeholder="Kommentar zum letzten Lauf" value="${safeComment(r.comment)}"><button onclick="saveComment(${r.id})">Speichern</button></div></div>`}
function table(a){if(!a.length)return '<p class="muted">Noch keine Durchgänge gespeichert.</p>';return '<table><tr><th>Zeit</th><th>Start</th><th>Split 1</th><th>Split 2</th><th>Kommentar</th></tr>'+a.map(r=>`<tr><td><b>${ft(r.time)}</b></td><td>${fd(r.ts)}</td><td>${ft(r.s1)}</td><td>${ft(r.s2)}</td><td>${safeComment(r.comment)||'--'}</td></tr>`).join('')+'</table>'}
function animateClock(now){let shownTime=clockValue;if(clockState==='RUNNING')shownTime+=(now-clockAt)/1000;timer.textContent=shownTime.toFixed(3);requestAnimationFrame(animateClock)}
async function update(){try{let d=await fetch('/status',{cache:'no-store'}).then(x=>x.json());clockState=d.state;clockValue=Number(d.time);clockAt=performance.now();state.textContent=d.state;s1.textContent=ft(d.split1);s2.textContent=ft(d.split2);const newest=d.last.length?d.last[0].id:0;const typing=document.activeElement&&document.activeElement.id===('c'+newest);if(!typing&&(latestRenderedId!==newest||document.activeElement?.tagName!=='INPUT')){latest.innerHTML=latestRun(d.last);latestRenderedId=newest}top.innerHTML=table(d.top);last.innerHTML=table(d.last);slaves.innerHTML=d.slaves.map(x=>`<div class="box"><span class="dot ${x.online?'on':'off'}"></span><b>${x.name}</b><span class="pill">${x.online?'ONLINE':'OFFLINE'}</span><div>${x.role}</div><div class="muted">ID ${x.id} · ${x.mac}<br>${x.online?'zuletzt vor '+x.age+' s':'keine aktuelle Verbindung'} · Pakete ${x.packets}</div></div>`).join('');if(d.newBest&&d.popupRunId&&shown!=d.popupRunId){shown=d.popupRunId;best.textContent=Number(d.time).toFixed(3);popup.style.display='grid'}}catch(e){state.textContent='VERBINDUNG FEHLT'}}
async function saveComment(id){const input=document.getElementById('c'+id);if(!input)return;const v=input.value;await fetch('/comment?id='+id,{method:'POST',headers:{'Content-Type':'text/plain'},body:v});input.blur();latestRenderedId=0;update()}
async function resetRun(){if(confirm('Lauf wirklich zurücksetzen?'))await fetch('/reset',{method:'POST'});update()}
fetch('/time?epoch='+Math.floor(Date.now()/1000),{method:'POST'});setInterval(update,1000);requestAnimationFrame(animateClock);update();
</script></body></html>)HTML";

void handleRoot(){server.send_P(200,"text/html; charset=utf-8",PAGE);}
void handleReset(){state=READY;startUs=stopUs=split1Us=split2Us=0;split1Set=split2Set=false;newBest=false;activity();server.send(200,"text/plain","OK");}
void handleTime(){if(server.hasArg("epoch")){epochBase=strtoul(server.arg("epoch").c_str(),nullptr,10);epochBaseMillis=millis();}server.send(200,"text/plain","OK");}
void handleComment(){
 if(!server.hasArg("id")){server.send(400,"text/plain","missing id");return;}
 if(store.lastCount==0){server.send(404,"text/plain","no run");return;}
 uint32_t id=strtoul(server.arg("id").c_str(),nullptr,10);
 if(id!=store.last[0].runId){server.send(403,"text/plain","comment only allowed for latest run");return;}
 String c=server.arg("plain");c.trim();if(c.length()>47)c.remove(47);
 c.toCharArray(store.last[0].comment,48);
 for(uint8_t i=0;i<store.topCount;i++)if(store.top[i].runId==id)c.toCharArray(store.top[i].comment,48);
 saveStore();activity();server.send(200,"text/plain","OK");
}
void setup(){
 Serial.begin(115200);EEPROM.begin(EEPROM_SIZE);loadStore();pinMode(PIN_START,INPUT_PULLUP);pinMode(PIN_STOP,INPUT_PULLUP);pinMode(LED_BUILTIN,OUTPUT);attachInterrupt(digitalPinToInterrupt(PIN_START),onStart,FALLING);attachInterrupt(digitalPinToInterrupt(PIN_STOP),onStop,FALLING);
 WiFi.mode(WIFI_AP_STA);WiFi.softAP(AP_SSID,AP_PASSWORD,WIFI_CHANNEL);if(esp_now_init()==0){esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);esp_now_register_recv_cb(onReceive);}
 server.on("/",HTTP_GET,handleRoot);server.on("/status",HTTP_GET,[]{server.send(200,"application/json",statusJson());});server.on("/reset",HTTP_POST,handleReset);server.on("/time",HTTP_POST,handleTime);server.on("/comment",HTTP_POST,handleComment);server.begin();
 Serial.println("HTTP: http://192.168.4.1");activity();
}
void loop(){
 server.handleClient();noInterrupts();bool a=startPending;uint32_t at=startIrqUs;startPending=false;bool b=stopPending;uint32_t bt=stopIrqUs;stopPending=false;interrupts();if(a)processEvent(EVENT_START,at);if(b)processEvent(EVENT_STOP,bt);
 static uint32_t ledAt=0;static bool led=false;uint32_t iv=state==RUNNING?150:800;if(millis()-ledAt>=iv){ledAt=millis();led=!led;digitalWrite(LED_BUILTIN,led?LOW:HIGH);}if(state!=RUNNING&&millis()-lastActivityMs>=SLEEP_MS){saveStore();digitalWrite(LED_BUILTIN,HIGH);ESP.deepSleep(0);}yield();
}
