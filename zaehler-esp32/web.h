// ─────────────────────────────────────────────────────────────────────────────
//  web.h — JSON-API, Web-Konfig-Handler, Routen, Web-OTA
//
//  Wird aus zaehler-esp32.ino NACH web_pages.h, heat.h und strom.h inkludiert.
//  CSS + HTML-Seiten liegen als PROGMEM in web_pages.h.
//  setupWeb() registriert alle Routen und startet den Server.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

String jsonEscape(const String& s) {
  String o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
}

// Grund des letzten Resets (esp_reset_reason() ist über die gesamte Laufzeit
// stabil abrufbar) — als Klartext fürs /api, zur Reboot-Diagnose.
//   brownout/poweron -> Stromversorgung (W5500/5-V-Netzteil-Kandidat)
//   panic/*_wdt      -> Firmware-Crash/Hänger (Ethernet hilft nicht)
//   sw               -> gewollter Reboot (OTA)
const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "ext";
    case ESP_RST_SW:        return "sw";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default:                return "unknown";
  }
}

// Wurde der AKTUELLE Boot durch einen Crash (Panic/Watchdog) ausgelöst? Nur dann
// stammt ein im Flash liegender Core-Dump aus DIESEM Boot. Bei jedem anderen Grund
// (sw/poweron/brownout/…) ist ein vorhandener Dump von einem FRÜHEREN Absturz und
// wurde nur nie gelöscht -> in /api als "stale":true markiert (sonst sieht ein alter
// Backtrace nach einem Reflash fälschlich wie ein aktueller aus).
bool bootFromCrash() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT: case ESP_RST_WDT: return true;
    default: return false;
  }
}

// Beim Boot EINMAL die Core-Dump-Summary aus der `coredump`-Partition lesen und als
// JSON in lastCrashJson cachen (Aufruf in setup()). Der Arduino-Build hat Core-Dump-
// to-Flash im ELF-Format aktiv -> bei jedem Panic liegt eine Summary im Flash; sie
// bleibt bis zum nächsten Crash stehen (überlebt auch normale Reboots). Kein Dump
// -> {"present":false}. Die Adressen (pc, bt[]) offline dekodieren:
//   xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf <pc> <bt…>
// Nur bei ELF-Coredump verfügbar; sonst bleibt der Default-JSON stehen.
void captureLastCrash() {
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
  esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
  if (!s) return;
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    char task[17]; memcpy(task, s->exc_task, 16); task[16] = '\0';
    size_t shaLen = sizeof(s->app_elf_sha256); if (shaLen > 16) shaLen = 16;
    char sha[17]; memcpy(sha, s->app_elf_sha256, shaLen); sha[shaLen] = '\0';
    String j = "{\"present\":true";
    j += ",\"stale\":" + String(bootFromCrash() ? "false" : "true");   // gehört der Dump zum aktuellen Boot?
    j += ",\"task\":\"" + jsonEscape(task) + "\"";
    j += ",\"pc\":\"0x" + String(s->exc_pc, HEX) + "\"";
    j += ",\"cause\":" + String(s->ex_info.exc_cause);
    j += ",\"vaddr\":\"0x" + String(s->ex_info.exc_vaddr, HEX) + "\"";
    j += ",\"corrupted\":" + String(s->exc_bt_info.corrupted ? "true" : "false");
    j += ",\"depth\":" + String(s->exc_bt_info.depth);
    j += ",\"bt\":[";
    uint32_t n = s->exc_bt_info.depth; if (n > 16) n = 16;
    for (uint32_t i = 0; i < n; i++) {
      if (i) j += ",";
      j += "\"0x" + String(s->exc_bt_info.bt[i], HEX) + "\"";
    }
    j += "],\"elf_sha\":\"" + jsonEscape(sha) + "\"}";
    lastCrashJson = j;
  }
  free(s);
#endif
}

// Core-Dump aus dem Flash löschen und den /api-Cache zurücksetzen (Endpunkt /clearcrash).
// Nur aus loop() aufrufen — Flash-Schreibzugriff; der Web-Handler setzt nur clearCrashReq.
void clearLastCrash() {
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
  esp_core_dump_image_erase();
#endif
  lastCrashJson = "{\"present\":false}";
  Serial.println("[CRASH] Core-Dump gelöscht (/clearcrash)");
}

void handleApi(AsyncWebServerRequest* req) {
  // Sekunden bis zur nächsten Wärme-Abfrage + nächste Uhrzeit als HH:MM.
  unsigned long nextS = 0;
  char nextAt[6] = "--:--";
  bool timeOk = timeValid();
  char startHHMM[6];
  snprintf(startHHMM, sizeof startHHMM, "%02u:%02u", heatStartMin / 60, heatStartMin % 60);
  if (heatEnabled) {
    if (timeOk) {
      time_t nowT = time(nullptr);
      time_t nxt  = heatNextSlot();
      if (nxt > nowT) nextS = (unsigned long)(nxt - nowT);
      struct tm nt; localtime_r(&nxt, &nt);
      strftime(nextAt, sizeof nextAt, "%H:%M", &nt);
    } else if (lastHeat != 0) {                 // Fallback ohne NTP: millis()-Intervall
      unsigned long el = millis() - lastHeat;
      nextS = (el >= heatIntervalMs()) ? 0 : (heatIntervalMs() - el) / 1000;
    }
  }

  String j = "{";
  j += "\"uptime_s\":" + String(millis() / 1000);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"wifi_ssid\":\"" + jsonEscape(wifiSsid) + "\"";
  j += ",\"mqtt\":" + String(mqtt.connected() ? "true" : "false");
  j += ",\"mqtt_en\":" + String(mqttEnabled ? "true" : "false");
  j += ",\"mqtt_root\":\"" + jsonEscape(mqttRoot) + "\"";
  j += ",\"mqtt_host\":\"" + jsonEscape(mqttServer) + "\"";
  j += ",\"mqtt_port\":" + String(mqttPort);
  j += ",\"mqtt_user\":\"" + jsonEscape(mqttUser) + "\"";
  j += ",\"mqtt_haspw\":" + String(mqttPass.length() ? "true" : "false");
  j += ",\"fw_ver\":\"" + jsonEscape(FW_VERSION) + "\"";
  j += ",\"fw_build\":\"" + jsonEscape(FW_BUILD) + "\"";
  j += ",\"reset_reason\":\"" + String(resetReasonStr()) + "\"";
  j += ",\"reboot_by\":\"" + jsonEscape(rebootBy) + "\"";   // "net-watchdog" nach Selbstheilung, sonst "none"
  j += ",\"lastcrash\":" + lastCrashJson;

  // Strom
  j += ",\"strom\":{";
  j += "\"enabled\":" + String(stromEnabled ? "true" : "false");
  j += ",\"gpio\":" + String(stromRxPin);
  j += ",\"send_s\":" + String(stromMqttS);
  j += ",\"maxw\":" + String(stromMaxW);
  j += ",\"crc_ok\":" + String(stromCrcOk);
  j += ",\"crc_err\":" + String(stromCrcErr);
  j += ",\"implausible\":" + String(stromImplaus);
  j += ",\"status\":\"" + jsonEscape(stromStatus) + "\"";
  if (!isnan(stromBezugWh))   j += ",\"bezug_kwh\":" + String(stromBezugWh / 1000.0, 3);
  if (!isnan(stromEinspWh))   j += ",\"einspeisung_kwh\":" + String(stromEinspWh / 1000.0, 3);
  if (!isnan(stromLeistungW)) j += ",\"leistung_w\":" + String(stromLeistungW, 1);
  j += ",\"codes\":[";
  for (int i = 0; i < stromCount; i++) {
    if (i) j += ",";
    j += "{\"code\":\"" + jsonEscape(stromCode[i]) + "\",";
    j += "\"value\":\"" + jsonEscape(stromValStr[i]) + "\",";
    j += "\"unit\":\"" + jsonEscape(stromUnitStr[i]) + "\"}";
  }
  j += "]}";

  // Wärme
  j += ",\"sendled\":{\"enabled\":" + String(sendledEnabled ? "true" : "false") + ",\"gpio\":" + String(sendledPin) + ",\"level\":" + String(sendledLevel ? 1 : 0) + "}";
  j += ",\"heat\":{";
  j += "\"enabled\":" + String(heatEnabled ? "true" : "false");
  j += ",\"interval_h\":" + String(heatIntervalH);
  j += ",\"start_hhmm\":\"" + String(startHHMM) + "\"";
  j += ",\"next_read_s\":" + String(nextS);
  j += ",\"next_at\":\"" + String(nextAt) + "\"";
  j += ",\"time_ok\":" + String(timeOk ? "true" : "false");
  j += ",\"tx\":" + String(heatTxPin);
  j += ",\"rx\":" + String(heatRxPin);
  j += ",\"request\":\"" + String(HEAT_REQ_NAMES[reqIdx]) + "\"";
  j += ",\"ident\":\"" + jsonEscape(heatIdent) + "\"";
  j += ",\"reads\":" + String(heatReads);
  j += ",\"ok\":" + String(heatOk);
  j += ",\"last_len\":" + String(heatLastLen);
  j += ",\"status\":\"" + jsonEscape(heatStatus) + "\"";
  j += ",\"codes\":[";
  for (int i = 0; i < heatCount; i++) {
    if (i) j += ",";
    j += "{\"code\":\"" + jsonEscape(heatCode[i]) + "\",";
    j += "\"value\":\"" + jsonEscape(heatVal[i]) + "\",";
    j += "\"unit\":\"" + jsonEscape(heatUnit[i]) + "\",";
    j += "\"raw\":\"" + jsonEscape(heatRaw[i]) + "\"}";
  }
  j += "]}}";
  req->send(200, "application/json", j);
}

// Erlaubte GPIOs — deckungsgleich mit INPINS/OUTPINS der Weboberfläche (web_pages.h).
// Nicht enthalten: GPIO6–11 (interner SPI-Flash), GPIO20/24/28–31 (auf dem Board nicht
// herausgeführt), GPIO1/3 (UART0 = serielle Konsole) und die Strapping-Pins GPIO0/2/12/15.
// Ohne diese Prüfung könnte ein direkter API-Aufruf (curl) das Gerät unbrauchbar machen.
bool validInPin(int g) {
  switch (g) {
    case 16: case 17: case 18: case 19:
    case 21: case 22: case 23: case 25:
    case 26: case 27: case 32: case 33:
    case 34: case 35: case 36: case 39:
      return true;
    default:
      return false;
  }
}

// Ausgänge: wie validInPin(), aber ohne die reinen Eingangs-Pins GPIO34–39.
bool validOutPin(int g) { return validInPin(g) && g < 34; }

void handleSetHeat(AsyncWebServerRequest* req) {
  if (reqHas(req, "en")) {
    heatEnabled = reqArg(req, "en").toInt() != 0;
    prefs.putUChar("heat_en", heatEnabled ? 1 : 0);
  }
  if (reqHas(req, "h")) {                        // Intervall -> nächster Teiler von 24 h
    heatIntervalH = snapHeatInterval(reqArg(req, "h").toInt());
    prefs.putUChar("heat_h", heatIntervalH);
    pubHeatCfg = true;                          // MQTT-Publish in loop() (thread-safe)
  }
  if (reqHas(req, "start")) {                    // Startuhrzeit "HH:MM" -> Minuten seit 0 Uhr
    String s = reqArg(req, "start");
    int c = s.indexOf(':');
    int hh = (c >= 0 ? s.substring(0, c) : s).toInt();
    int mm = c >= 0 ? s.substring(c + 1).toInt() : 0;
    int m  = hh * 60 + mm;
    if (m < 0) m = 0; if (m > 1439) m = 1439;
    heatStartMin = (uint16_t)m;
    prefs.putUShort("heat_start", heatStartMin);
    lastHeatSlot = -1;                           // neuen Fahrplan sofort greifen lassen
    pubHeatCfg = true;                           // interval_h + next_read in loop() neu publizieren
  }
  if (reqHas(req, "tx")) { int g = reqArg(req, "tx").toInt(); if (validOutPin(g)) { heatTxPin = g; prefs.putUChar("heat_tx", g); } }
  if (reqHas(req, "rx")) { int g = reqArg(req, "rx").toInt(); if (validInPin(g))  { heatRxPin = g; prefs.putUChar("heat_rx", g); } }
  Serial.printf("[CFG] Wärme: %s, ab %02u:%02u alle %u h, TX=GPIO%u RX=GPIO%u\n",
                heatEnabled ? "AN" : "AUS", heatStartMin / 60, heatStartMin % 60,
                heatIntervalH, heatTxPin, heatRxPin);
  req->send(200, "text/plain", "ok");
}

void handleSetStrom(AsyncWebServerRequest* req) {
  bool changed = false;
  if (reqHas(req, "en")) {
    stromEnabled = reqArg(req, "en").toInt() != 0;
    prefs.putUChar("strom_en", stromEnabled ? 1 : 0);
    changed = true;
  }
  if (reqHas(req, "rx")) {
    int g = reqArg(req, "rx").toInt();
    if (validInPin(g)) { stromRxPin = g; prefs.putUChar("strom_rx", g); changed = true; }
  }
  if (reqHas(req, "s")) {                       // MQTT-Sendeintervall (s)
    int s = reqArg(req, "s").toInt();
    if (s < STROM_MQTT_MIN_S) s = STROM_MQTT_MIN_S;
    if (s > STROM_MQTT_MAX_S) s = STROM_MQTT_MAX_S;
    stromMqttS = (uint16_t)s;
    prefs.putUShort("strom_s", stromMqttS);
    pubStromCfg = true;                          // MQTT-Publish in loop() (thread-safe)
  }
  if (reqHas(req, "maxw")) {                      // Plausi-Grenze Leistung (W); 0 = aus
    long m = reqArg(req, "maxw").toInt();
    if (m < 0) m = 0;
    if (m > STROM_MAXW_MAX) m = STROM_MAXW_MAX;
    stromMaxW = (uint32_t)m;
    prefs.putUInt("strom_maxw", stromMaxW);
  }
  if (changed) applyStromPending = true;         // UART-Re-Init in loop() (thread-safe)
  req->send(200, "text/plain", "ok");
}

// Sende-Diode des SML-Kopfes konfigurieren: en (an/aus), gpio, lvl (1=HIGH / 0=LOW).
void handleSetSendLed(AsyncWebServerRequest* req) {
  bool changed = false;
  if (reqHas(req, "en"))   { sendledEnabled = reqArg(req, "en").toInt() != 0; prefs.putUChar("sled_en", sendledEnabled ? 1 : 0); changed = true; }
  if (reqHas(req, "gpio")) { int g = reqArg(req, "gpio").toInt(); if (validOutPin(g)) { sendledPin = g; prefs.putUChar("sled_pin", g); changed = true; } }
  if (reqHas(req, "lvl"))  { sendledLevel = reqArg(req, "lvl").toInt() != 0; prefs.putUChar("sled_lvl", sendledLevel ? 1 : 0); changed = true; }
  if (changed) applySendLedPending = true;       // GPIO in loop() setzen (thread-safe)
  req->send(200, "text/plain", "ok");
}

// MQTT-Broker konfigurieren: host, port, user, pw (alle optional). Leeres pw-Feld
// lässt das Passwort UNVERÄNDERT (sonst würde jedes Speichern es löschen).
void handleSetMqtt(AsyncWebServerRequest* req) {
  if (reqHas(req, "en")) {                          // MQTT global an/aus
    mqttEnabled = reqArg(req, "en").toInt() != 0;
    prefs.putUChar("mqtt_en", mqttEnabled ? 1 : 0);
  }
  if (reqHas(req, "root")) {                       // Haupttopic; '/'-Enden abschneiden
    mqttRoot = reqArg(req, "root");
    mqttRoot.trim();
    while (mqttRoot.endsWith("/")) mqttRoot.remove(mqttRoot.length() - 1);
    if (mqttRoot.length() == 0) mqttRoot = MQTT_ROOT_DEF;
    prefs.putString("mqtt_root", mqttRoot);
  }
  if (reqHas(req, "host")) { mqttServer = reqArg(req, "host"); prefs.putString("mqtt_host", mqttServer); }
  if (reqHas(req, "port")) {
    int p = reqArg(req, "port").toInt();
    if (p > 0 && p <= 65535) { mqttPort = (uint16_t)p; prefs.putUShort("mqtt_port", mqttPort); }
  }
  if (reqHas(req, "user")) { mqttUser = reqArg(req, "user"); prefs.putString("mqtt_user", mqttUser); }
  if (reqHas(req, "pw")) {
    String pw = reqArg(req, "pw");
    if (pw.length()) { mqttPass = pw; prefs.putString("mqtt_pass", mqttPass); }
  }
  Serial.printf("[CFG] MQTT %s:%u user=%s\n",
                mqttServer.c_str(), mqttPort, mqttUser.length() ? mqttUser.c_str() : "(anonym)");
  applyMqttPending = true;                        // Reconnect in loop() (thread-safe)
  req->send(200, "text/plain", "ok");
}

void setupWebOta() {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", UPDATE_PAGE);
  });
  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* req) {                    // läuft nach Upload-Ende
      bool err = Update.hasError();
      AsyncWebServerResponse* resp = req->beginResponse(200, "text/plain",
                                       err ? "FEHLER beim Flashen" : "OK - Neustart...");
      resp->addHeader("Connection", "close");
      req->send(resp);
      if (!err) restartAt = millis() + 800;            // Neustart aus loop() heraus
    },
    [](AsyncWebServerRequest* req, String filename, size_t index,
       uint8_t* data, size_t len, bool final) {        // Upload-Chunks
      if (index == 0) {
        otaActive = true;
        Serial.printf("[WebOTA] Start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      }
      if (len && Update.write(data, len) != len) Update.printError(Serial);
      if (final) {
        if (Update.end(true)) Serial.printf("[WebOTA] OK: %u Bytes\n", index + len);
        else Update.printError(Serial);
      }
    }
  );
}

// ─── WLAN-Provisioning: Scan, Speichern, Reset, Captive-Portal ────────────────
// Scan wird über ein Flag in loop() gestartet (WiFi-Zugriff gehört nicht in den
// Web-Task); /scan.json pollt nur das Ergebnis und gibt den Scan-Puffer frei.
void handleScanJson(AsyncWebServerRequest* req) {
  int16_t n = WiFi.scanComplete();                 // >=0 fertig, -1 läuft, -2 idle
  if (n < 0) { req->send(200, "application/json", "{\"scanning\":true,\"nets\":[]}"); return; }
  String out = "{\"scanning\":false,\"nets\":[";
  for (int i = 0; i < n; i++) {
    if (i) out += ',';
    out += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i))
         + ",\"enc\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1") + "}";
  }
  out += "]}";
  WiFi.scanDelete();                               // Scan-Puffer freigeben (RAM)
  req->send(200, "application/json", out);
}

// Im apMode jede unbekannte / OS-Detection-URL auf die Portalseite lenken.
static void captiveTo(AsyncWebServerRequest* r) {
  if (apMode) r->redirect("http://192.168.4.1/");
  else        r->send(404, "text/plain", "404");
}

void setupProvisioning() {
  // Neue WLAN-Daten: nur Werte + Flag setzen, speichern/rebooten macht loop().
  server.on("/wifisave", HTTP_GET, [](AsyncWebServerRequest* r){
    pendingSsid = reqArg(r, "ssid");
    pendingPass = reqArg(r, "pass");
    credSaveReq = true;
    r->send(200, "text/plain", "ok");
  });
  // "WLAN vergessen": Flag setzen -> loop() löscht Creds + rebootet -> Setup-Portal.
  server.on("/wifireset", HTTP_GET, [](AsyncWebServerRequest* r){
    wifiResetReq = true;
    r->send(200, "text/plain", "ok");
  });
  // Scan anstoßen (loop() ruft WiFi.scanNetworks auf) + Ergebnis pollen.
  server.on("/scan",      HTTP_GET, [](AsyncWebServerRequest* r){ scanReq = true; r->send(200, "application/json", "{\"scanning\":true}"); });
  server.on("/scan.json", HTTP_GET, handleScanJson);

  // Captive-Portal-Detection-URLs (Android/iOS/Windows/Firefox) -> Portal.
  const char* det[] = { "/generate_204", "/gen_204", "/hotspot-detect.html",
                        "/canonical.html", "/ncsi.txt", "/connecttest.txt", "/redirect" };
  for (auto u : det) server.on(u, HTTP_ANY, captiveTo);
  server.onNotFound(captiveTo);                    // alles Übrige im apMode -> Portal
}

// Alle Routen registrieren und den Async-Webserver starten.
void setupWeb() {
  server.on("/",          HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", apMode ? PORTAL_PAGE : MAIN_PAGE); });
  server.on("/strom",     HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", STROM_PAGE); });
  server.on("/waerme",    HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", WAERME_PAGE); });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* resp = r->beginResponse_P(200, "text/css", CSS);
    resp->addHeader("Cache-Control", "max-age=86400");
    r->send(resp);
  });
  server.on("/api",       HTTP_GET, handleApi);
  server.on("/setheat",   HTTP_GET, handleSetHeat);
  server.on("/setstrom",  HTTP_GET, handleSetStrom);
  server.on("/setsendled",HTTP_GET, handleSetSendLed);
  server.on("/setmqtt",   HTTP_GET, handleSetMqtt);
  server.on("/read",      HTTP_GET, [](AsyncWebServerRequest* r){ reqRead = true; r->send(200, "text/plain", "ok"); });
  server.on("/clearcrash",HTTP_GET, [](AsyncWebServerRequest* r){ clearCrashReq = true; r->send(200, "text/plain", "ok"); });
  server.on("/toggle",    HTTP_GET, [](AsyncWebServerRequest* r){ reqIdx = 1 - reqIdx; r->send(200, "text/plain", HEAT_REQ_NAMES[reqIdx]); });
  setupWebOta();
  setupProvisioning();                             // WLAN-Setup-Portal + Captive-Routen
  server.begin();
}
