// ─────────────────────────────────────────────────────────────────────────────
//  strom.h — Stromzähler: SML-Parser (eHZ via Hichi TTL-IR-Lesekopf)
//
//  Wird aus zaehler-esp32.ino NACH net_mqtt.h inkludiert (nutzt topicify()).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// SML-Transport-CRC16 (CRC-16/X-25, wie libSML): poly 0x8408 (reflektiert), init
// 0xFFFF, xorout 0xFFFF, danach Byte-Swap in die On-Wire-Reihenfolge. Bitweise
// gerechnet (kein 512-B-Tabellen-Footprint; bei ~1 kB alle 10 s vernachlässigbar).
uint16_t smlCrc16(const uint8_t* d, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
  }
  crc ^= 0xFFFF;
  return (uint16_t)(((crc & 0xFF) << 8) | ((crc >> 8) & 0xFF));   // Byte-Swap
}

// Ein TLV-Element überspringen (rekursiv für Listen). Liefert Zeiger danach.
const uint8_t* smlSkip(const uint8_t* p, const uint8_t* end) {
  if (p >= end) return end;
  uint8_t tl = *p;
  if (tl & 0x80) return end;
  uint8_t type = tl & 0x70;
  uint8_t len  = tl & 0x0F;
  if (type == 0x70) {                   // Liste mit 'len' Elementen
    const uint8_t* q = p + 1;
    for (int i = 0; i < len && q < end; i++) q = smlSkip(q, end);
    return q;
  }
  if (len == 0) return p + 1;           // 0x00 (Füll-/Endbyte)
  return p + len;
}

// Ein TLV-Element als (vorzeichenbehaftete) Ganzzahl lesen.
long smlInt(const uint8_t* p, const uint8_t* end, bool* ok) {
  *ok = false;
  if (p >= end) return 0;
  uint8_t tl = *p;
  if (tl & 0x80) return 0;
  uint8_t type = tl & 0x70;
  int n = (tl & 0x0F) - 1;
  if (n <= 0 || p + 1 + n > end) return 0;
  bool neg = (type == 0x50) && (p[1] & 0x80);
  long v = neg ? -1L : 0L;
  for (int i = 0; i < n; i++) v = (v << 8) | p[1 + i];
  *ok = true;
  return v;
}

// DLMS-Einheitencode -> Klartext (häufige Stromzähler-Einheiten)
const char* dlmsUnit(uint8_t u) {
  switch (u) {
    case 27: return "W";   case 28: return "VA";  case 29: return "var";
    case 30: return "Wh";  case 31: return "VAh"; case 32: return "varh";
    case 33: return "A";   case 35: return "V";   case 44: return "Hz";
    case 4:  return "°";   case 8:  return "°";   case 9:  return "°C";
    default: return "";
  }
}

int smlFindObis(const uint8_t* buf, int len, const uint8_t* obis) {
  for (int i = 0; i + 7 <= len; i++)
    if (buf[i] == 0x07 && memcmp(buf + i + 1, obis, 6) == 0) return i;
  return -1;
}

double smlValue(const uint8_t* buf, int len, const uint8_t* obis) {
  int idx = smlFindObis(buf, len, obis);
  if (idx < 0) return NAN;
  const uint8_t* end = buf + len;
  const uint8_t* p   = buf + idx + 7;
  p = smlSkip(p, end);                 // status
  p = smlSkip(p, end);                 // valTime
  p = smlSkip(p, end);                 // unit
  bool ok;
  long scaler = smlInt(p, end, &ok);
  if (!ok) scaler = 0;
  p = smlSkip(p, end);                 // -> value
  long value = smlInt(p, end, &ok);
  if (!ok) return NAN;
  return (double)value * pow(10.0, (double)scaler);
}

// Generisch ALLE OBIS-Werte (Medium Strom, obis[0]==1) aus dem Telegramm holen.
void smlScanAll(const uint8_t* buf, int len) {
  stromCount = 0;
  const uint8_t* end = buf + len;
  for (int i = 0; i + 7 <= len && stromCount < STROM_MAX; i++) {
    if (buf[i] != 0x07) continue;
    const uint8_t* obis = buf + i + 1;
    if (obis[0] != 0x01) continue;            // nur Strom-Register

    const uint8_t* p = buf + i + 7;
    p = smlSkip(p, end);                       // status
    p = smlSkip(p, end);                       // valTime
    uint8_t unitCode = 0;                      // unit (0x62 = unsigned 1 Byte)
    if (p < end) { uint8_t tl = *p; if ((tl & 0x70) == 0x60 && (tl & 0x0F) >= 2) unitCode = p[1]; }
    p = smlSkip(p, end);                       // unit
    bool ok;
    long scaler = smlInt(p, end, &ok);
    if (!ok) scaler = 0;
    p = smlSkip(p, end);                       // -> value
    uint8_t vt = (p < end) ? (*p & 0x70) : 0;  // nur Integer-Werte (kein octet-string)
    if (vt != 0x50 && vt != 0x60) continue;
    long value = smlInt(p, end, &ok);
    if (!ok) continue;

    String code = String(obis[2]) + "." + String(obis[3]) + "." + String(obis[4]);
    bool dup = false;
    for (int k = 0; k < stromCount; k++) if (stromCode[k] == code) { dup = true; break; }
    if (dup) continue;

    double v = (double)value * pow(10.0, (double)scaler);
    // scaler kommt ungeprüft aus dem Telegramm. Ein extremer scaler ergibt eine
    // riesige Zahl (positiv) oder Präzision (negativ); das frühere dtostrf() ist
    // NICHT längenbegrenzt und lief dann über vb[] hinaus -> Stack-Overflow /
    // __stack_chk_fail-Panic (loopTask). snprintf() ist hart längenbegrenzt und
    // kann per Definition nicht überlaufen; Präzision zusätzlich gekappt (Zähler
    // nutzen ≤3–4 Nachkommastellen).
    int prec = (scaler < 0) ? (int)-scaler : 0;
    if (prec > 6) prec = 6;
    char vb[40]; snprintf(vb, sizeof vb, "%.*f", prec, v);
    stromCode[stromCount]    = code;
    stromValStr[stromCount]  = String(vb);
    stromUnitStr[stromCount] = dlmsUnit(unitCode);
    stromCount++;
  }
}

void smlProcess(const uint8_t* buf, int len) {
  static const uint8_t OBIS_BEZUG[6] = {0x01,0x00,0x01,0x08,0x00,0xFF}; // 1.8.0
  static const uint8_t OBIS_EINSP[6] = {0x01,0x00,0x02,0x08,0x00,0xFF}; // 2.8.0
  static const uint8_t OBIS_LEIST[6] = {0x01,0x00,0x10,0x07,0x00,0xFF}; // 16.7.0

  double b = smlValue(buf, len, OBIS_BEZUG);
  double e = smlValue(buf, len, OBIS_EINSP);
  double w = smlValue(buf, len, OBIS_LEIST);

  bool any = false;
  if (!isnan(b)) { stromBezugWh   = b; any = true; }
  if (!isnan(e)) { stromEinspWh   = e; any = true; }
  // Momentan-Leistung gegen die (Web-einstellbare) Plausi-Grenze prüfen: absurde
  // Ausreißer (z.B. der 1-MW-Peak aus einem Glitch-Telegramm) NICHT übernehmen,
  // sondern den letzten guten Wert behalten. stromMaxW == 0 -> Prüfung aus.
  if (!isnan(w)) {
    if (stromMaxW == 0 || fabs(w) <= (double)stromMaxW) { stromLeistungW = w; any = true; }
    else stromImplaus++;
  }

  smlScanAll(buf, len);                       // komplette Werteliste

  if (any || stromCount > 0) { stromValid = true; stromLastOk = millis(); }
}

void smlPoll() {
  while (Sml.available()) {
    uint8_t b = (uint8_t)Sml.read();
    if (smlLen < SML_BUF) smlBuf[smlLen++] = b; else { smlLen = 0; smlEndPos = 0; continue; }

    // Start-Escape 1B1B1B1B 01010101 -> Puffer auf diese 8 Startbytes zurücksetzen.
    if (smlLen >= 8 &&
        memcmp(&smlBuf[smlLen - 8], "\x1B\x1B\x1B\x1B\x01\x01\x01\x01", 8) == 0) {
      memmove(smlBuf, &smlBuf[smlLen - 8], 8);
      smlLen = 8; smlEndPos = 0;
      continue;
    }
    // End-Escape 1B1B1B1B 1A gesehen -> danach folgen noch NN (Füllbyte-Anzahl) + CRC16
    // (2 Bytes). Position hinter dem '1A' merken und auf diese 3 Bytes warten.
    if (smlEndPos == 0 && smlLen >= 5 &&
        memcmp(&smlBuf[smlLen - 5], "\x1B\x1B\x1B\x1B\x1A", 5) == 0) {
      smlEndPos = smlLen;
    }
    // Rahmen vollständig (End-Escape + NN + 2 CRC-Bytes) -> CRC über [Start .. NN] prüfen.
    if (smlEndPos > 0 && smlLen >= smlEndPos + 3) {
      uint16_t crcCalc = smlCrc16(smlBuf, smlEndPos + 1);   // inkl. Füllbyte-Anzahl NN
      uint16_t crcRecv = ((uint16_t)smlBuf[smlEndPos + 1] << 8) | smlBuf[smlEndPos + 2];
      if (crcCalc == crcRecv) { stromCrcOk++;  smlProcess(smlBuf, smlEndPos - 5); }
      else                    { stromCrcErr++; }
      smlLen = 0; smlEndPos = 0;
    }
  }
  if (stromValid && millis() - stromLastOk > STROM_STALE_MS) stromStatus = "stale";
  else if (stromValid)                                       stromStatus = "ok";
}

void publishStrom() {
  if (!stromEnabled || !stromValid) return;
  char v[24];
  if (!isnan(stromBezugWh)) {
    dtostrf(stromBezugWh / 1000.0, 0, 3, v);
    mqtt.publish((stromPrefix() + "bezug_kwh").c_str(), v, true);
  }
  if (!isnan(stromEinspWh)) {
    dtostrf(stromEinspWh / 1000.0, 0, 3, v);
    mqtt.publish((stromPrefix() + "einspeisung_kwh").c_str(), v, true);
  }
  if (!isnan(stromLeistungW)) {
    dtostrf(stromLeistungW, 0, 1, v);
    mqtt.publish((stromPrefix() + "leistung_w").c_str(), v, true);
  }
  for (int i = 0; i < stromCount; i++) {       // komplette Liste generisch
    String t = stromPrefix() + "data/" + topicify(stromCode[i]);
    mqtt.publish(t.c_str(), stromValStr[i].c_str(), true);
    if (stromUnitStr[i].length()) mqtt.publish((t + "/unit").c_str(), stromUnitStr[i].c_str(), true);
  }
  mqtt.publish((stromPrefix() + "status").c_str(), stromStatus.c_str(), true);
}

// Stromzähler-UART nach Konfig (de)aktivieren / Pin wechseln
void applyStrom() {
  Sml.end();
  smlLen = 0;
  if (stromEnabled) {
    Sml.setRxBufferSize(4096);
    Sml.begin(9600, SERIAL_8N1, stromRxPin, -1, SML_INVERT);
    pinMode(stromRxPin, INPUT_PULLUP);  // Hichi-Open-Collector: RX-Leitung nicht floaten lassen
    stromStatus = "init";
    Serial.printf("[CFG] Strom: AN, GPIO%u\n", stromRxPin);
  } else {
    stromValid = false;
    stromStatus = "aus";
    Serial.println("[CFG] Strom: AUS");
  }
}

// Sende-Diode des SML-Kopfes auf definierten Pegel parken (verhindert Einstreuung in den
// Lesesensor). Web-Setter setzt nur Werte + applySendLedPending; die GPIO-Aktion macht loop().
void applySendLed() {
  static uint8_t activePin = 255;              // zuletzt belegter Pin (255 = keiner)
  if (activePin != 255 && (!sendledEnabled || activePin != sendledPin)) {
    pinMode(activePin, INPUT);                 // alten Pin freigeben (aus oder gewechselt)
    activePin = 255;
  }
  if (sendledEnabled) {
    pinMode(sendledPin, OUTPUT);
    digitalWrite(sendledPin, sendledLevel ? HIGH : LOW);
    activePin = sendledPin;
    Serial.printf("[CFG] Sende-Diode: AN, GPIO%u=%s\n", sendledPin, sendledLevel ? "HIGH" : "LOW");
  } else {
    Serial.println("[CFG] Sende-Diode: AUS");
  }
}
