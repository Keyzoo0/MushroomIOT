/* ============================================================================
 *  MONITORING & KONTROL BUDIDAYA JAMUR  -  ESP32 DevKit V1 (Arduino framework)
 * ----------------------------------------------------------------------------
 *  Sensor : GY-SHT31 (suhu & kelembapan udara, I2C)
 *           Capacitive Soil Moisture (ADC 0-4095) -> dipakai utk kelembapan
 *           media tanam + estimasi kadar air serbuk gergaji (baglog)
 *  Output : Relay FAN (pendingin), Relay LAMPU (pemanas), Relay MIST MAKER
 *           LED Hijau (WiFi connected), LED Merah (disconnected/reconnecting)
 *  Web    : ESPAsyncWebServer, UI dari LittleFS (Bootstrap 5 + Chart.js via CDN)
 *
 *  LIBRARY YANG HARUS DIINSTALL (Library Manager / GitHub):
 *    - ESP Async WebServer  (ESP32Async/ESPAsyncWebServer)
 *    - Async TCP            (ESP32Async/AsyncTCP)
 *    - ArduinoJson          (Benoit Blanchon)  v6/v7
 *    - Adafruit SHT31       (+ Adafruit Unified Sensor + Adafruit BusIO)
 *    LittleFS & Preferences sudah bawaan core ESP32.
 *
 *  UPLOAD UI: taruh file di folder /data lalu jalankan
 *    Tools -> ESP32 LittleFS Data Upload
 * ==========================================================================*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "Adafruit_SHT31.h"

// ----------------------------- KONFIG WIFI ---------------------------------
const char* WIFI_SSID = "ROSI1";
const char* WIFI_PASS = "20517420";

// ------------------------------- PIN MAP -----------------------------------
#define PIN_SDA        21      // I2C SHT31
#define PIN_SCL        22
#define PIN_SOIL       34      // ADC1 (input-only, aman dgn WiFi)
#define PIN_RELAY_FAN  25
#define PIN_RELAY_LAMP 26
#define PIN_RELAY_MIST 27
#define PIN_LED_GREEN  16
#define PIN_LED_RED    17

// --------------------------- INTERVAL / FIFO -------------------------------
#define SENSOR_INTERVAL_MS  2000    // baca sensor tiap 2 dtk
#define GRAPH_INTERVAL_MS   3000    // sampling grafik tiap 3 dtk
#define GRAPH_SAVE_MS       30000   // simpan grafik.json tiap 30 dtk (hemat flash)
#define WIFI_CHECK_MS       5000
#define GRAPH_POINTS        60      // 60 x 3dtk = 3 menit

// ------------------------------ OBJEK GLOBAL -------------------------------
Adafruit_SHT31  sht31 = Adafruit_SHT31();
AsyncWebServer  server(80);
Preferences     prefs;

// ------------------------------ PENGATURAN ---------------------------------
struct Settings {
  float tempMin   = 22.0;   // < ini -> LAMPU pemanas ON  (auto)
  float tempMax   = 30.0;   // > ini -> FAN pendingin ON  (auto)
  float humMin    = 80.0;   // < ini -> MIST ON           (auto)
  int   soilDryADC = 3500;  // pembacaan ADC saat kering (udara)
  int   soilWetADC = 1200;  // pembacaan ADC saat basah (tercelup air)
  float sawDryMax  = 50.0;  // <= ini -> serbuk gergaji "Kering"
  float sawWetMin  = 70.0;  // >= ini -> serbuk gergaji "Basah"
  bool  revFan  = false;    // reverse logika relay (default Active-LOW)
  bool  revLamp = false;
  bool  revMist = false;
  bool  autoFan  = true;    // mode tiap aktuator: true=AUTO, false=MANUAL
  bool  autoLamp = true;
  bool  autoMist = true;
  bool  manFan  = false;    // state manual ON/OFF (dipakai saat MANUAL)
  bool  manLamp = false;
  bool  manMist = false;
} cfg;

// ------------------------------ STATE RUNTIME ------------------------------
float  temperature = 0;     // °C
float  humidity    = 0;     // %RH
int    soilADC     = 0;
float  soilPercent = 0;     // % kelembapan media
float  sawPercent  = 0;     // % estimasi kadar air serbuk gergaji
String sawCond     = "-";
bool   sensorOK    = false;

bool   relayFan = false, relayLamp = false, relayMist = false;  // state logis (true=ON)

// FIFO grafik (parallel arrays, ring buffer sederhana)
float gTemp[GRAPH_POINTS], gHum[GRAPH_POINTS], gSoil[GRAPH_POINTS], gSaw[GRAPH_POINTS];
int   gCount = 0;           // jumlah titik terisi (max GRAPH_POINTS)

unsigned long tSensor = 0, tGraph = 0, tSave = 0, tWifi = 0;
bool   graphDirty = false;

// =============================== SETTINGS ==================================
void loadSettings() {
  prefs.begin("cfg", true);
  cfg.tempMin   = prefs.getFloat("tempMin",  cfg.tempMin);
  cfg.tempMax   = prefs.getFloat("tempMax",  cfg.tempMax);
  cfg.humMin    = prefs.getFloat("humMin",   cfg.humMin);
  cfg.soilDryADC= prefs.getInt  ("soilDry",  cfg.soilDryADC);
  cfg.soilWetADC= prefs.getInt  ("soilWet",  cfg.soilWetADC);
  cfg.sawDryMax = prefs.getFloat("sawDry",   cfg.sawDryMax);
  cfg.sawWetMin = prefs.getFloat("sawWet",   cfg.sawWetMin);
  cfg.revFan    = prefs.getBool ("revFan",   cfg.revFan);
  cfg.revLamp   = prefs.getBool ("revLamp",  cfg.revLamp);
  cfg.revMist   = prefs.getBool ("revMist",  cfg.revMist);
  cfg.autoFan   = prefs.getBool ("autoFan",  cfg.autoFan);
  cfg.autoLamp  = prefs.getBool ("autoLamp", cfg.autoLamp);
  cfg.autoMist  = prefs.getBool ("autoMist", cfg.autoMist);
  cfg.manFan    = prefs.getBool ("manFan",   cfg.manFan);
  cfg.manLamp   = prefs.getBool ("manLamp",  cfg.manLamp);
  cfg.manMist   = prefs.getBool ("manMist",  cfg.manMist);
  prefs.end();
}

void saveSettings() {
  prefs.begin("cfg", false);
  prefs.putFloat("tempMin",  cfg.tempMin);
  prefs.putFloat("tempMax",  cfg.tempMax);
  prefs.putFloat("humMin",   cfg.humMin);
  prefs.putInt  ("soilDry",  cfg.soilDryADC);
  prefs.putInt  ("soilWet",  cfg.soilWetADC);
  prefs.putFloat("sawDry",   cfg.sawDryMax);
  prefs.putFloat("sawWet",   cfg.sawWetMin);
  prefs.putBool ("revFan",   cfg.revFan);
  prefs.putBool ("revLamp",  cfg.revLamp);
  prefs.putBool ("revMist",  cfg.revMist);
  prefs.putBool ("autoFan",  cfg.autoFan);
  prefs.putBool ("autoLamp", cfg.autoLamp);
  prefs.putBool ("autoMist", cfg.autoMist);
  prefs.putBool ("manFan",   cfg.manFan);
  prefs.putBool ("manLamp",  cfg.manLamp);
  prefs.putBool ("manMist",  cfg.manMist);
  prefs.end();
}

// =============================== SENSOR ====================================
float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }

void readSensors() {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (!isnan(t) && !isnan(h)) { temperature = t; humidity = h; sensorOK = true; }
  else sensorOK = false;

  // Soil: kering = ADC besar, basah = ADC kecil
  long acc = 0;
  for (int i = 0; i < 8; i++) { acc += analogRead(PIN_SOIL); delay(2); }
  soilADC = acc / 8;

  float pct = (float)(cfg.soilDryADC - soilADC) * 100.0 /
              (float)(cfg.soilDryADC - cfg.soilWetADC);
  soilPercent = clampf(pct, 0, 100);

  // Estimasi kadar air serbuk gergaji (riset asal: baglog menahan air lebih
  // baik dari tanah mineral -> kurva sedikit digeser ke atas, dibatasi 0-100)
  sawPercent = clampf(soilPercent * 0.85 + 12.0, 0, 100);

  if      (sawPercent <= cfg.sawDryMax) sawCond = "Kering";
  else if (sawPercent >= cfg.sawWetMin) sawCond = "Basah";
  else                                  sawCond = "Ideal";
}

// =============================== KONTROL ===================================
void writeRelay(int pin, bool on, bool reversed) {
  // default Active-LOW: LOW = ON. 'reversed' membalik logika.
  bool level = on ? LOW : HIGH;
  if (reversed) level = !level;
  digitalWrite(pin, level);
}

void applyControl() {
  // FAN (pendingin): suhu terlalu panas
  relayFan  = cfg.autoFan  ? (temperature > cfg.tempMax) : cfg.manFan;
  // LAMPU (pemanas): suhu terlalu dingin
  relayLamp = cfg.autoLamp ? (temperature < cfg.tempMin) : cfg.manLamp;
  // MIST: kelembapan udara terlalu kering
  relayMist = cfg.autoMist ? (humidity    < cfg.humMin)  : cfg.manMist;

  writeRelay(PIN_RELAY_FAN,  relayFan,  cfg.revFan);
  writeRelay(PIN_RELAY_LAMP, relayLamp, cfg.revLamp);
  writeRelay(PIN_RELAY_MIST, relayMist, cfg.revMist);
}

// =============================== GRAFIK ====================================
void pushGraph() {
  if (gCount < GRAPH_POINTS) {
    gTemp[gCount]=temperature; gHum[gCount]=humidity;
    gSoil[gCount]=soilPercent; gSaw[gCount]=sawPercent;
    gCount++;
  } else {
    for (int i = 1; i < GRAPH_POINTS; i++) {
      gTemp[i-1]=gTemp[i]; gHum[i-1]=gHum[i];
      gSoil[i-1]=gSoil[i]; gSaw[i-1]=gSaw[i];
    }
    gTemp[GRAPH_POINTS-1]=temperature; gHum[GRAPH_POINTS-1]=humidity;
    gSoil[GRAPH_POINTS-1]=soilPercent; gSaw[GRAPH_POINTS-1]=sawPercent;
  }
  graphDirty = true;
}

void buildGraphJson(Print& out) {
  out.print("{\"temp\":[");
  for (int i=0;i<gCount;i++){ out.print(gTemp[i],1); if(i<gCount-1)out.print(','); }
  out.print("],\"hum\":[");
  for (int i=0;i<gCount;i++){ out.print(gHum[i],1);  if(i<gCount-1)out.print(','); }
  out.print("],\"soil\":[");
  for (int i=0;i<gCount;i++){ out.print(gSoil[i],1); if(i<gCount-1)out.print(','); }
  out.print("],\"saw\":[");
  for (int i=0;i<gCount;i++){ out.print(gSaw[i],1);  if(i<gCount-1)out.print(','); }
  out.print("]}");
}

void saveGraph() {
  File f = LittleFS.open("/grafik.json", "w");
  if (!f) return;
  buildGraphJson(f);
  f.close();
  graphDirty = false;
}

void loadGraph() {
  File f = LittleFS.open("/grafik.json", "r");
  if (!f) return;
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;
  JsonArray t=doc["temp"], h=doc["hum"], s=doc["soil"], w=doc["saw"];
  int n = t.size();
  if (n > GRAPH_POINTS) n = GRAPH_POINTS;
  gCount = n;
  for (int i=0;i<n;i++){
    gTemp[i]=t[i]|0.0f; gHum[i]=h[i]|0.0f;
    gSoil[i]=s[i]|0.0f; gSaw[i]=w[i]|0.0f;
  }
}

void resetGraph() {
  gCount = 0;
  saveGraph();
}

// ============================== API HANDLER ================================
void handleData(AsyncWebServerRequest *req) {
  AsyncResponseStream *res = req->beginResponseStream("application/json");
  StaticJsonDocument<512> d;
  d["temp"] = round(temperature*10)/10.0;
  d["hum"]  = round(humidity*10)/10.0;
  d["soilADC"] = soilADC;
  d["soil"] = round(soilPercent*10)/10.0;
  d["saw"]  = round(sawPercent*10)/10.0;
  d["sawCond"] = sawCond;
  d["sensorOK"] = sensorOK;
  JsonObject act = d.createNestedObject("act");
  JsonObject f = act.createNestedObject("fan");
  f["on"]=relayFan;  f["auto"]=cfg.autoFan;
  JsonObject l = act.createNestedObject("lamp");
  l["on"]=relayLamp; l["auto"]=cfg.autoLamp;
  JsonObject m = act.createNestedObject("mist");
  m["on"]=relayMist; m["auto"]=cfg.autoMist;
  d["rssi"] = WiFi.RSSI();
  d["ip"]   = WiFi.localIP().toString();
  d["wifi"] = (WiFi.status()==WL_CONNECTED);
  serializeJson(d, *res);
  req->send(res);
}

void handleGetSettings(AsyncWebServerRequest *req) {
  AsyncResponseStream *res = req->beginResponseStream("application/json");
  StaticJsonDocument<512> d;
  d["tempMin"]=cfg.tempMin; d["tempMax"]=cfg.tempMax; d["humMin"]=cfg.humMin;
  d["soilDryADC"]=cfg.soilDryADC; d["soilWetADC"]=cfg.soilWetADC;
  d["sawDryMax"]=cfg.sawDryMax;   d["sawWetMin"]=cfg.sawWetMin;
  d["revFan"]=cfg.revFan; d["revLamp"]=cfg.revLamp; d["revMist"]=cfg.revMist;
  serializeJson(d, *res);
  req->send(res);
}

void handlePostSettings(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject o = json.as<JsonObject>();
  if (o.containsKey("tempMin"))    cfg.tempMin   = o["tempMin"];
  if (o.containsKey("tempMax"))    cfg.tempMax   = o["tempMax"];
  if (o.containsKey("humMin"))     cfg.humMin    = o["humMin"];
  if (o.containsKey("soilDryADC")) cfg.soilDryADC= o["soilDryADC"];
  if (o.containsKey("soilWetADC")) cfg.soilWetADC= o["soilWetADC"];
  if (o.containsKey("sawDryMax"))  cfg.sawDryMax = o["sawDryMax"];
  if (o.containsKey("sawWetMin"))  cfg.sawWetMin = o["sawWetMin"];
  if (o.containsKey("revFan"))     cfg.revFan    = o["revFan"];
  if (o.containsKey("revLamp"))    cfg.revLamp   = o["revLamp"];
  if (o.containsKey("revMist"))    cfg.revMist   = o["revMist"];
  saveSettings();
  applyControl();
  req->send(200, "application/json", "{\"ok\":true}");
}

// {actuator:"fan|lamp|mist", auto:true/false}
void handleMode(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject o = json.as<JsonObject>();
  String a = o["actuator"] | "";
  bool au = o["auto"] | true;
  if (a=="fan")  cfg.autoFan = au;
  else if (a=="lamp") cfg.autoLamp = au;
  else if (a=="mist") cfg.autoMist = au;
  saveSettings();
  applyControl();
  req->send(200, "application/json", "{\"ok\":true}");
}

// {actuator:"fan|lamp|mist", on:true/false}  (hanya berlaku saat MANUAL)
void handleActuator(AsyncWebServerRequest *req, JsonVariant &json) {
  JsonObject o = json.as<JsonObject>();
  String a = o["actuator"] | "";
  bool on = o["on"] | false;
  if (a=="fan")  cfg.manFan = on;
  else if (a=="lamp") cfg.manLamp = on;
  else if (a=="mist") cfg.manMist = on;
  saveSettings();
  applyControl();
  req->send(200, "application/json", "{\"ok\":true}");
}

void handleGraph(AsyncWebServerRequest *req) {
  AsyncResponseStream *res = req->beginResponseStream("application/json");
  buildGraphJson(*res);
  req->send(res);
}

// ============================== SETUP / LOOP ===============================
void setupServer() {
  server.on("/api/data",     HTTP_GET, handleData);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/graph",    HTTP_GET, handleGraph);
  server.on("/api/graph/reset", HTTP_POST, [](AsyncWebServerRequest *req){
    resetGraph();
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/settings", handlePostSettings));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/mode",     handleMode));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/actuator", handleActuator));

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *req){ req->send(404, "text/plain", "Not found"); });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_RELAY_FAN, OUTPUT);
  pinMode(PIN_RELAY_LAMP, OUTPUT);
  pinMode(PIN_RELAY_MIST, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  loadSettings();
  // matikan semua relay sesuai logika awal
  writeRelay(PIN_RELAY_FAN,  false, cfg.revFan);
  writeRelay(PIN_RELAY_LAMP, false, cfg.revLamp);
  writeRelay(PIN_RELAY_MIST, false, cfg.revMist);

  analogReadResolution(12);                 // ADC 0-4095
  analogSetPinAttenuation(PIN_SOIL, ADC_11db);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!sht31.begin(0x44)) {
    if (!sht31.begin(0x45)) Serial.println("SHT31 tidak terdeteksi!");
  }

  if (!LittleFS.begin(true)) Serial.println("LittleFS gagal mount!");
  else loadGraph();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  unsigned long t0 = millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t0 < 15000) {
    digitalWrite(PIN_LED_RED, !digitalRead(PIN_LED_RED));  // kedip merah
    delay(300); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED) Serial.println("IP: " + WiFi.localIP().toString());

  // mDNS -> akses lewat http://jamur.local
  if (MDNS.begin("jamur")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS aktif: http://jamur.local");
  } else {
    Serial.println("mDNS gagal start");
  }

  setupServer();
  readSensors();
  applyControl();
}

void loop() {
  unsigned long now = millis();

  if (now - tSensor >= SENSOR_INTERVAL_MS) {
    tSensor = now;
    readSensors();
    applyControl();
  }

  if (now - tGraph >= GRAPH_INTERVAL_MS) {
    tGraph = now;
    pushGraph();
  }

  if (now - tSave >= GRAPH_SAVE_MS) {
    tSave = now;
    if (graphDirty) saveGraph();
  }

  if (now - tWifi >= WIFI_CHECK_MS) {
    tWifi = now;
    bool ok = (WiFi.status()==WL_CONNECTED);
    digitalWrite(PIN_LED_GREEN, ok ? HIGH : LOW);
    if (!ok) { WiFi.reconnect(); }
  }

  // LED merah kedip saat tidak terhubung
  if (WiFi.status()!=WL_CONNECTED) {
    digitalWrite(PIN_LED_RED, (now/400)%2);
  } else {
    digitalWrite(PIN_LED_RED, LOW);
  }
}
