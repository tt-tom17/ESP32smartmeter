# ESP32 Wärme- + Stromzähler-Reader

Ein ESP32-WROOM-32 liest **Wärmezähler** (Landis+Gyr UH50/T550, D0/IEC 62056-21)
und **Stromzähler** (SML über Hichi TTL-IR-Lesekopf), zeigt alles auf einer
eigenen, handytauglichen Weboberfläche und schickt es optional per **MQTT** an
einen Broker (z. B. Mosquitto / ioBroker). MQTT ist **standardmäßig deaktiviert** —
ohne Broker läuft das Gerät als reines Web-Display. MQTT-Schalter, Broker und
Haupttopic sind zur Laufzeit über die Weboberfläche einstellbar.

## Projektstruktur
```
esp32-zaehler-reader/
├─ zaehler-esp32/
│  ├─ zaehler-esp32.ino     # Einstieg: Includes + setup() + loop()
│  ├─ config.h              # Konstanten & Defaults (Pins, Intervalle, FW-Version)
│  ├─ globals.h             # globale Objekte + Laufzeit-Zustand
│  ├─ net_mqtt.h            # WLAN- + MQTT-Verbindung
│  ├─ heat.h                # Wärmezähler (D0 / IEC 62056-21)
│  ├─ strom.h               # Stromzähler (SML-Parser)
│  ├─ web.h                 # JSON-API, Konfig-Handler, Routen, Web-OTA, Setup-Portal
│  └─ web_pages.h           # CSS + HTML-Seiten (PROGMEM), inkl. Setup-Portal
├─ platformio.ini
├─ README.md
└─ LICENSE
```

Der Sketch ist auf mehrere Header aufgeteilt, die alle als **eine**
Translation-Unit aus `zaehler-esp32.ino` in fester Reihenfolge inkludiert werden
(keine separate `.cpp`-Kompilierung nötig).

## Einrichtung (WLAN per Setup-Portal)
Die WLAN-Zugangsdaten werden **nicht** einkompiliert, sondern beim Erststart
komfortabel über ein eigenes Setup-WLAN eingerichtet und im NVS gespeichert:

1. Nach dem ersten Flashen (oder wenn keine WLAN-Daten hinterlegt sind) öffnet der
   ESP ein **offenes WLAN `Zaehler-Setup`**.
2. Mit Handy/Laptop verbinden — die Setup-Seite erscheint automatisch
   (Captive Portal), sonst im Browser **`http://192.168.4.1`** öffnen.
3. Über **Suchen** das eigene WLAN wählen, Passwort eingeben, **Speichern & Neustart**.
4. Der ESP startet neu und verbindet sich als Station mit dem Heim-WLAN.

Später kann das WLAN unter **Einstellungen → WLAN → „WLAN vergessen"** zurückgesetzt
werden (öffnet nach dem Neustart wieder das Setup-Portal).


## Verdrahtung

### Wärmezähler-Lesekopf (D0, optisch) — UART1
| Lesekopf      | ESP32        |
|---------------|--------------|
| Rx (Eingang)  | GPIO17 (TX)  |
| Tx (Ausgang)  | GPIO16 (RX)  |
| VCC           | 3V3          |
| GND           | GND          |

### Hichi TTL-IR-Lesekopf (Strom, SML) — UART2
| Hichi         | ESP32        |
|---------------|--------------|
| TX (Daten)    | GPIO27 (RX)  |
| VCC           | 3V3          |
| GND           | GND          |

> Beim Hichi reicht **eine Datenleitung** (TX → ESP32 RX), da der SML-Zähler von
> selbst sendet. RX des Hichi bleibt frei.

Die GPIOs sind außerdem zur Laufzeit über die Weboberfläche umstellbar.

## Wärme-Auslesung (D0 / IEC 62056-21) — funktionierender Ablauf
Der UH50 wird nach IEC-Standard ausgelesen (verifiziert am echten Zähler):
1. UART1 @ **300 Baud 7E1** öffnen, **40× `0x00`** Wake-up senden, dann Sign-on
   **`/?!\r\n`**.
2. Identifikation `/MMMZident\r\n` @300 lesen (z. B. `/LUGCUH50`). Das **5. Zeichen**
   kodiert die Datenbaudrate (Ziffer 0–6 = Mode C mit ACK, Buchstabe A–F = Mode B).
3. Bei Mode C ein **ACK** `0x06 '0' Z '0' \r\n` @300 senden.
4. Auf die Datenbaudrate (beim UH50 **2400, Mode B**) 7E1 umschalten und den
   Datenblock bis `!` lesen → ~66 OBIS-Codes.

Wichtigster Wert: **`6.8` = Wärmemenge in MWh**.

## Bauen — PlatformIO (VSCode, empfohlen)
Funktioniert aus demselben Repo — `platformio.ini` zeigt per `src_dir` auf den
Sketch-Ordner, du musst nichts nach `src/` kopieren.
1. PlatformIO-Projekt öffnen (Ordner `esp32-zaehler-reader/`).
2. Bauen/Flashen über die PlatformIO-Toolbar oder:
   ```bash
   pio run                 # kompilieren
   pio run -t upload       # per USB flashen
   pio device monitor      # serieller Monitor @115200
   ```
Die Libraries (`PubSubClient`, `ESPAsyncWebServer`, `AsyncTCP`) werden automatisch
aus `lib_deps` geholt. Für die **pioarduino**-Variante (neuere ESP32-Cores) in
`platformio.ini` die `platform`-Zeile auf die pioarduino-URL umstellen (Kommentar
dort beachten). OTA-Upload: `upload_protocol`/`upload_port` in `platformio.ini`
aktivieren — oder bequemer das Web-OTA (siehe unten).

## Bauen — Arduino-IDE
- Board: **ESP32 Dev Module**
- Libraries installieren: **PubSubClient** (Nick O'Leary), **ESPAsyncWebServer**
  und **AsyncTCP** (ESP32Async).
- Rest (WiFi, ArduinoOTA, Update, Preferences) kommt mit dem ESP32-Core.

## Inbetriebnahme
1. **Erster Flash per USB** (danach reicht das Web-OTA unter `/update`).
2. WLAN über das **Setup-Portal** einrichten (siehe „Einrichtung (WLAN per
   Setup-Portal)" oben) → der ESP verbindet sich danach mit dem Heim-WLAN.
3. Seriellen Monitor @115200 öffnen → IP-Adresse notieren.
4. Im Browser `http://<IP>/` öffnen → Live-Anzeige; `http://<IP>/api` liefert das
   rohe JSON (gut zum Debuggen).
5. Optional: Auf **Einstellungen** MQTT **aktivieren**, den Broker (Host/IP, Port,
   ggf. User/PW) und bei Bedarf das **Haupttopic** eintragen → speichern, der ESP
   verbindet sich neu. (MQTT ist im Auslieferungszustand **aus**.)

## Weboberfläche (handytauglich, mehrseitig)
Die Oberfläche läuft auf einem **asynchronen Webserver** — sie bleibt auch
während eines Wärme-Lesezyklus reaktionsfähig. Navigationsleiste oben:
**Start · Strom · Wärme · Einstellungen**. Im Footer stehen **Firmware-Version**
und Build-Zeitstempel (zeigt, ob ein OTA-Flash wirklich angekommen ist).

- **Start (`/`)** — Übersicht: aktuelle Strom- (Leistung, Bezug, Einspeisung) und
  Wärmewerte, WLAN-RSSI, Uptime sowie **MQTT-Status** (verbunden/getrennt) als Pill.
- **Strom (`/strom`)** — Status + **alle** ausgelesenen OBIS-Werte (generischer
  SML-Scan).
- **Wärme (`/waerme`)** — „Jetzt lesen", Status-/Identifikations-Details und
  **alle** UH50-Werte.
- **Einstellungen (`/update`)** — sämtliche Konfiguration an einem Ort:
  - **⚡ Strom:** Auslesen AN/AUS, Lesekopf-GPIO, **MQTT-Sendeintervall (2–300 s)**.
    Das Sendeintervall ist das Pendant zu Tasmotas `TelePeriod`; der SML-Zähler
    sendet selbst 1–2×/s, schneller als ~2 s bringt also keine neuen Werte.
  - **🔥 Wärme:** Auslesen AN/AUS, **Leseintervall 1–24 h**, TX-/RX-GPIO.
  - **MQTT:** **An/Aus-Schalter (Default aus)**, Verbindungsstatus, **Haupttopic**,
    **Host/IP, Port, User, Passwort** (speichern → verbindet neu; leeres
    Passwortfeld lässt das gespeicherte PW unverändert).
  - **Firmware-Update** (Web-OTA, siehe unten).

Alle Einstellungen (an/aus, Intervalle, GPIOs, MQTT-Broker, Haupttopic) liegen im
**NVS (`Preferences`, Namespace `zaehler`)** und überstehen einen Reboot.

Steuer-Endpunkte (auch per `curl`): `GET /api` (JSON),
`GET /setheat?en=0|1&h=N&tx=G&rx=G`, `GET /setstrom?en=0|1&rx=G&s=Sek`,
`GET /setmqtt?en=0|1&root=...&host=...&port=1883&user=...&pw=...`, `GET /read`
(Wärme sofort lesen).
Hinweis: `/api` liefert `mqtt_en` + `mqtt_root/host/port/user` + `mqtt_haspw`, das
**Passwort selbst wird nie ausgeliefert**.

## Firmware-Update (Web-OTA)
`http://<IP>/update` (Seite **Einstellungen**) → Firmware hochladen:
- **PlatformIO:** die gebaute `firmware.bin` wählen, oder per `curl`:
  ```bash
  curl -F "f=@.pio/build/esp32dev/firmware.bin" http://<IP>/update
  ```
- **Arduino-IDE:** **Sketch → Kompilierte Binärdatei exportieren**, dann die
  `.ino.bin` hochladen.

Erhöhe `FW_VERSION` in `config.h` bei jedem Build, dann lässt sich im Footer prüfen,  
ob der Flash angekommen ist.

## MQTT-Topics
Unter einem editierbaren **Haupttopic `<root>`** (Default `ESP32smartmeter`) mit
den zwei Zweigen **`Heat`** und **`Power`**:

**Gerät**
- `<root>/online` — Verbindungsstatus (LWT)

**Wärme (`<root>/Heat/…`)** — benannte Hauptwerte (je mit `/unit`):
- `zaehlerstand_mwh` (6.8), `volumen_m3` (6.26), `leistung_kw` (6.6),
  `durchfluss_m3h` (6.33), `betriebsstunden_h` (6.31), `batterie_monate` (6.35),
  `vorlauf_c` / `ruecklauf_c` (aus 9.4)
- `data/<L+G-Code>` — **alle** Codes generisch + `/unit`, `/raw`
- `status`, `interval_h`

**Strom (`<root>/Power/…`)**:
- `bezug_kwh`, `einspeisung_kwh`, `leistung_w` — benannte Hauptwerte
- `data/<OBIS>` — alle generisch geparsten Werte + `/unit`
- `status`, `send_s` (MQTT-Sendeintervall)

## Troubleshooting (die zwei wahrscheinlichen Stolpersteine)
- **Wärmezähler antwortet nicht** (`no_response`): Sign-on per `/toggle`-Endpunkt
  auf `/#!` umschalten (normal `/?!`). TX/RX am Lesekopf vertauscht? GPIO16/17
  prüfen (Lesekopf Tx → GPIO16, Rx → GPIO17).
- **Strom: keine/falsche Werte**: `SML_INVERT` in `config.h` auf `true` setzen
  (manche Hichi-Köpfe geben ein invertiertes Signal aus). Prüfen, ob im
  `/api`-JSON `strom.status` auf `ok` springt.

## Bekannte Grenzen
- SML-CRC wird nicht geprüft (Resync über Escape-Sequenzen).
- Generischer SML-Scan: nur Strom-Register (OBIS-Medium 1) mit Integer-Wert;
  Mehrbyte-Zählerstände werden als 32-Bit gelesen (für Haushaltswerte ausreichend).
- Der Wärme-Lesezyklus (~5–6 s) blockiert die `loop()`; dank Async-Webserver
  bleibt die Oberfläche aber bedienbar. MQTT-Publishes pausieren in dieser Zeit
  kurz; bei 1–24 h Intervall fällt das nicht ins Gewicht.
- **Thread-Safety:** Web-Handler laufen in einem eigenen Task; MQTT (PubSubClient)
  und die UARTs sind nicht thread-safe. Setter setzen daher nur Werte/Flags, die
  Seiteneffekte führt `loop()` aus.
- GPIO-Auswahl: Eingangs-Pins inkl. 34–39 (nur Eingang!) für RX; TX nur
  ausgangsfähige Pins. Flash-Pins 6–11 sind nicht wählbar.
