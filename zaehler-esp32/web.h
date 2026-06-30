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

void handleApi(AsyncWebServerRequest* req) {
  unsigned long nextS = 0;
  if (heatEnabled && lastHeat != 0) {
    unsigned long el = millis() - lastHeat;
    nextS = (el >= heatIntervalMs()) ? 0 : (heatIntervalMs() - el) / 1000;
  }

  String j = "{";
  j += "\"uptime_s\":" + String(millis() / 1000);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"mqtt\":" + String(mqtt.connected() ? "true" : "false");
  j += ",\"mqtt_root\":\"" + jsonEscape(mqttRoot) + "\"";
  j += ",\"mqtt_host\":\"" + jsonEscape(mqttServer) + "\"";
  j += ",\"mqtt_port\":" + String(mqttPort);
  j += ",\"mqtt_user\":\"" + jsonEscape(mqttUser) + "\"";
  j += ",\"mqtt_haspw\":" + String(mqttPass.length() ? "true" : "false");
  j += ",\"fw_ver\":" + String(FW_VERSION);
  j += ",\"fw_build\":\"" + jsonEscape(FW_BUILD) + "\"";

  // Strom
  j += ",\"strom\":{";
  j += "\"enabled\":" + String(stromEnabled ? "true" : "false");
  j += ",\"gpio\":" + String(stromRxPin);
  j += ",\"send_s\":" + String(stromMqttS);
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
  j += ",\"heat\":{";
  j += "\"enabled\":" + String(heatEnabled ? "true" : "false");
  j += ",\"interval_h\":" + String(heatIntervalH);
  j += ",\"next_read_s\":" + String(nextS);
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

bool validGpio(int g) { return g >= 0 && g <= 39; }

void handleSetHeat(AsyncWebServerRequest* req) {
  if (reqHas(req, "en")) {
    heatEnabled = reqArg(req, "en").toInt() != 0;
    prefs.putUChar("heat_en", heatEnabled ? 1 : 0);
  }
  if (reqHas(req, "h")) {
    int h = reqArg(req, "h").toInt();
    if (h < HEAT_INTERVAL_MIN_H) h = HEAT_INTERVAL_MIN_H;
    if (h > HEAT_INTERVAL_MAX_H) h = HEAT_INTERVAL_MAX_H;
    heatIntervalH = (uint8_t)h;
    prefs.putUChar("heat_h", heatIntervalH);
    pubHeatCfg = true;                          // MQTT-Publish in loop() (thread-safe)
  }
  if (reqHas(req, "tx")) { int g = reqArg(req, "tx").toInt(); if (validGpio(g)) { heatTxPin = g; prefs.putUChar("heat_tx", g); } }
  if (reqHas(req, "rx")) { int g = reqArg(req, "rx").toInt(); if (validGpio(g)) { heatRxPin = g; prefs.putUChar("heat_rx", g); } }
  Serial.printf("[CFG] Wärme: %s, %u h, TX=GPIO%u RX=GPIO%u\n",
                heatEnabled ? "AN" : "AUS", heatIntervalH, heatTxPin, heatRxPin);
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
    if (validGpio(g)) { stromRxPin = g; prefs.putUChar("strom_rx", g); changed = true; }
  }
  if (reqHas(req, "s")) {                       // MQTT-Sendeintervall (s)
    int s = reqArg(req, "s").toInt();
    if (s < STROM_MQTT_MIN_S) s = STROM_MQTT_MIN_S;
    if (s > STROM_MQTT_MAX_S) s = STROM_MQTT_MAX_S;
    stromMqttS = (uint16_t)s;
    prefs.putUShort("strom_s", stromMqttS);
    pubStromCfg = true;                          // MQTT-Publish in loop() (thread-safe)
  }
  if (changed) applyStromPending = true;         // UART-Re-Init in loop() (thread-safe)
  req->send(200, "text/plain", "ok");
}

// MQTT-Broker konfigurieren: host, port, user, pw (alle optional). Leeres pw-Feld
// lässt das Passwort UNVERÄNDERT (sonst würde jedes Speichern es löschen).
void handleSetMqtt(AsyncWebServerRequest* req) {
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

// Alle Routen registrieren und den Async-Webserver starten.
void setupWeb() {
  server.on("/",          HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", MAIN_PAGE); });
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
  server.on("/setmqtt",   HTTP_GET, handleSetMqtt);
  server.on("/read",      HTTP_GET, [](AsyncWebServerRequest* r){ reqRead = true; r->send(200, "text/plain", "ok"); });
  server.on("/toggle",    HTTP_GET, [](AsyncWebServerRequest* r){ reqIdx = 1 - reqIdx; r->send(200, "text/plain", HEAT_REQ_NAMES[reqIdx]); });
  setupWebOta();
  server.begin();
}
