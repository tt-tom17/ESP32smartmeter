// ─────────────────────────────────────────────────────────────────────────────
//  heat.h — Wärmezähler: D0 / IEC 62056-21 (Landis+Gyr UH50 / T550)
//
//  Wird aus zaehler-esp32.ino NACH net_mqtt.h inkludiert (nutzt topicify()).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

int parseHeat(const char* buf) {
  heatCount = 0;
  const char* p = buf;
  while (*p && heatCount < HEAT_MAX) {
    const char* open  = strchr(p, '(');
    if (!open) break;
    const char* close = strchr(open, ')');
    if (!close) break;

    String code, inner;
    for (const char* q = p;        q < open;  q++) code  += *q;
    for (const char* q = open + 1; q < close; q++) inner += *q;
    code.trim();
    if (code.length() == 0) { p = close + 1; continue; }

    String value = inner, unit = "";
    int star = inner.indexOf('*');
    if (star >= 0 && inner.indexOf('&') < 0) {     // nur splitten, wenn eindeutig
      value = inner.substring(0, star);
      unit  = inner.substring(star + 1);
    }

    heatCode[heatCount] = code;
    heatVal[heatCount]  = value;
    heatUnit[heatCount] = unit;
    heatRaw[heatCount]  = (value != inner) ? inner : "";
    heatCount++;
    p = close + 1;
  }
  return heatCount;
}

// Sprechende Namen für die wichtigsten UH50-OBIS-Codes. Diese werden zusätzlich
// zu data/<code> unter heatPrefix()+<name> publiziert (stabile, lesbare Topics).
// 9.4 (Vor-/Rücklauftemp, Format "vor&rueck") wird gesondert gesplittet.
struct HeatNamed { const char* code; const char* topic; };
static const HeatNamed HEAT_NAMED[] = {
  { "6.8",  "zaehlerstand_mwh"  },   // Wärme-Zählerstand (MWh)
  { "6.26", "volumen_m3"        },   // Volumen gesamt (m³)
  { "6.6",  "leistung_kw"       },   // aktuelle Leistung (kW)
  { "6.33", "durchfluss_m3h"    },   // Durchfluss (m³/h)
  { "6.31", "betriebsstunden_h" },   // Betriebsstunden (h)
  { "6.35", "batterie_monate"   },   // Batterie-Restmonate
};
static const int HEAT_NAMED_N = sizeof(HEAT_NAMED) / sizeof(HEAT_NAMED[0]);

// Liefert für einen OBIS-Code den sprechenden Topic-Namen oder nullptr.
const char* heatNamedTopic(const String& code) {
  for (int k = 0; k < HEAT_NAMED_N; k++)
    if (code == HEAT_NAMED[k].code) return HEAT_NAMED[k].topic;
  return nullptr;
}

void publishHeat() {
  for (int i = 0; i < heatCount; i++) {
    String base = heatPrefix() + "data/" + topicify(heatCode[i]);
    mqtt.publish(base.c_str(), heatVal[i].c_str(), true);
    if (heatUnit[i].length()) mqtt.publish((base + "/unit").c_str(), heatUnit[i].c_str(), true);
    if (heatRaw[i].length())  mqtt.publish((base + "/raw").c_str(),  heatRaw[i].c_str(),  true);

    // Zusätzlich unter sprechendem Namen publizieren (falls bekannt).
    const char* named = heatNamedTopic(heatCode[i]);
    if (named) {
      String nt = heatPrefix() + named;
      mqtt.publish(nt.c_str(), heatVal[i].c_str(), true);
      if (heatUnit[i].length()) mqtt.publish((nt + "/unit").c_str(), heatUnit[i].c_str(), true);
    }

    // Vor-/Rücklauftemperatur (9.4 = "074.4*C&059.7*C") in zwei numerische DPs
    // aufteilen: am '&' trennen, dann je die Einheit ab '*' abschneiden.
    if (heatCode[i] == "9.4") {
      int amp = heatVal[i].indexOf('&');
      if (amp >= 0) {
        String vl = heatVal[i].substring(0, amp);
        String rl = heatVal[i].substring(amp + 1);
        int s;
        if ((s = vl.indexOf('*')) >= 0) vl = vl.substring(0, s);
        if ((s = rl.indexOf('*')) >= 0) rl = rl.substring(0, s);
        mqtt.publish((heatPrefix() + "vorlauf_c").c_str(),          vl.c_str(), true);
        mqtt.publish((heatPrefix() + "vorlauf_c/unit").c_str(),     "°C",       true);
        mqtt.publish((heatPrefix() + "ruecklauf_c").c_str(),        rl.c_str(), true);
        mqtt.publish((heatPrefix() + "ruecklauf_c/unit").c_str(),   "°C",       true);
      }
    }
  }
}

// Baudrate-Kennbuchstabe (IEC 62056-21, 5. Zeichen der Ident).
long baudFromChar(char z, bool* modeC) {
  switch (z) {
    case '0': *modeC = true; return 300;
    case '1': *modeC = true; return 600;
    case '2': *modeC = true; return 1200;
    case '3': *modeC = true; return 2400;
    case '4': *modeC = true; return 4800;
    case '5': *modeC = true; return 9600;
    case '6': *modeC = true; return 19200;
    case 'A': *modeC = false; return 600;
    case 'B': *modeC = false; return 1200;
    case 'C': *modeC = false; return 2400;
    case 'D': *modeC = false; return 4800;
    case 'E': *modeC = false; return 9600;
    case 'F': *modeC = false; return 19200;
    default:  *modeC = false; return 300;     // Mode A: bei 300 bleiben
  }
}

// Nächsten fälligen Abfrage-Slot als retained Topic <root>/Heat/next_read
// veröffentlichen (lokale Zeit "YYYY-MM-DD HH:MM"); ohne NTP-Sync "unknown".
// Nur aus loop() aufrufen (thread-safe); publish() ist bei getrenntem MQTT ein No-Op.
void publishHeatNext() {
  char buf[20] = "unknown";
  if (timeValid()) {
    time_t nxt = heatNextSlot();
    if (nxt) { struct tm nt; localtime_r(&nxt, &nt); strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &nt); }
  }
  mqtt.publish((heatPrefix() + "next_read").c_str(), buf, true);
}

void readHeat() {
  heatReads++;
  heatLastAt = millis();
  String stTopic = heatPrefix() + "status";

  // Phase 1: @300 Baud 7E1 — Wake-up (40x 0x00) + Anfrage senden
  Heat.begin(300, SERIAL_7E1, heatRxPin, heatTxPin);
  delay(20);
  while (Heat.available()) Heat.read();
  for (int i = 0; i < 40; i++) Heat.write((uint8_t)0x00);
  Heat.print(HEAT_REQUESTS[reqIdx]);
  Heat.flush();

  // Phase 2: Identifikation @300 lesen ( /MMMZident\r\n )
  char ident[80]; int idn = 0;
  unsigned long dl = millis() + 2000;
  while (millis() < dl && idn < 79) {
    if (Heat.available()) {
      char c = (char)Heat.read();
      dl = millis() + 400;
      if (c == '\r') continue;
      if (c == '\n') break;
      ident[idn++] = c;
    }
  }
  ident[idn] = '\0';
  heatIdent = String(ident);

  if (idn < 5 || ident[0] != '/') {
    heatStatus = "keine/ungueltige Identifikation \"" + heatIdent + "\" (Anfrage " + HEAT_REQ_NAMES[reqIdx] + ")";
    heatCount = 0; heatLastLen = 0;
    mqtt.publish(stTopic.c_str(), "no_response", true);
    Serial.printf("[D0] Ident ungueltig/leer: \"%s\"\n", ident);
    return;
  }

  bool modeC = false;
  long dataBaud = baudFromChar(ident[4], &modeC);
  Serial.printf("[D0] Ident=\"%s\"  '%c' -> %ld Baud, Mode %s\n",
                ident, ident[4], dataBaud, modeC ? "C" : "B/A");

  // Phase 3: Mode C -> ACK ( 0x06 '0' Z '0' \r\n )
  if (modeC) {
    Heat.write((uint8_t)0x06); Heat.write('0');
    Heat.write((uint8_t)ident[4]); Heat.write('0');
    Heat.write('\r'); Heat.write('\n');
    Heat.flush();
  }

  // Phase 4: Daten-Baudrate 7E1, Datenblock bis '!' lesen
  Heat.begin(dataBaud, SERIAL_7E1, heatRxPin, heatTxPin);
  int len = 0;
  unsigned long deadline = millis() + HEAT_FIRST_BYTE_MS;
  while (millis() < deadline && len < TELEGRAM_BUF - 1) {
    if (Heat.available()) {
      char c = (char)Heat.read();
      deadline = millis() + HEAT_IDLE_MS;
      if (c == 0x02 || c == 0x03 || c == '\r' || c == '\n') continue;
      if (c == '!') break;
      telegram[len++] = c;
    }
  }
  telegram[len] = '\0';
  heatLastLen = len;

  if (len == 0) {
    heatStatus = "Ident ok (" + heatIdent + "), aber kein Datenblock @" + String(dataBaud);
    heatCount = 0;
    mqtt.publish(stTopic.c_str(), "ident_only", true);
    return;
  }

  int n = parseHeat(telegram);
  heatOk++;
  heatStatus = "ok / " + String(n) + " Codes / " + String(len) + " B @" + String(dataBaud);
  mqtt.publish(stTopic.c_str(), heatStatus.c_str(), true);
  publishHeat();
  Serial.printf("[D0] OK: %d Codes geparst, publiziert.\n", n);
}
