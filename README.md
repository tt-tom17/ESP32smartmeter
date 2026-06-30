# ESP32 Wärme- + Stromzähler-Reader

Ein ESP32-WROOM-32 liest **Wärmezähler** (Landis+Gyr UH50/T550, D0/IEC 62056-21)
und **Stromzähler** (SML über Hichi TTL-IR-Lesekopf), zeigt alles auf einer
eigenen, handytauglichen Weboberfläche und schickt es per MQTT an ioBroker.

## Projektstruktur
```
esp32-zaehler-reader/
├─ zaehler-esp32/
│  ├─ zaehler-esp32.ino     # der Sketch
│  ├─ secrets.example.h     # Vorlage für die WLAN-Daten (committet)
│  └─ secrets.h             # deine WLAN-Daten (lokal, .gitignore)
├─ README.md
└─ LICENSE
```

## Einrichtung (WLAN-Zugangsdaten)
Die WLAN-Zugangsdaten liegen **nicht** im Sketch, sondern in `secrets.h`
(per `.gitignore` ausgeschlossen):

```bash
cp zaehler-esp32/secrets.example.h zaehler-esp32/secrets.h
# danach WIFI_SSID und WIFI_PASS in secrets.h eintragen
```

Den MQTT-Broker (Default-IP) bei Bedarf oben in `zaehler-esp32.ino` anpassen.

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

## Wärme-Auslesung (D0 / IEC 62056-21) — funktionierender Ablauf
Der UH50 wird nach IEC-Standard ausgelesen (verifiziert am echten Zähler):
1. UART1 @ **300 Baud 7E1** öffnen, **40× `0x00`** Wake-up senden, dann Sign-on
   **`/?!\r\n`**.
2. Identifikation `/MMMZident\r\n` @300 lesen (z. B. `/LUGCUH50`). Das **5. Zeichen**
   kodiert die Datenbaudrate (Ziffer 0–6 = Mode C mit ACK, Buchstabe A–F = Mode B).
3. Bei Mode C ein **ACK** `0x06 '0' Z '0' \r\n` @300 senden.
4. Auf die Datenbaudrate (beim UH50 **2400, Mode B**) 7E1 umschalten und den
   Datenblock bis `!` lesen → ~70 OBIS-Codes.

Wichtigster Wert: **`6.8` = Wärmemenge in MWh** (→ DB `waermedaten`).

## Bauen — Arduino-IDE
- Board: **ESP32 Dev Module**
- Library: **PubSubClient** (Nick O'Leary) installieren
- Rest (WiFi, WebServer, ArduinoOTA, Update, Preferences) kommt mit dem ESP32-Core.

## Bauen — PlatformIO (VSCode)
Funktioniert aus demselben Repo — `platformio.ini` zeigt per `src_dir` auf den
Sketch-Ordner, du musst nichts nach `src/` kopieren.
1. `secrets.h` anlegen (siehe oben) — sie liegt in `zaehler-esp32/`.
2. PlatformIO-Projekt öffnen (Ordner `esp32-zaehler-reader/`).
3. Bauen/Flashen über die PlatformIO-Toolbar oder:
   ```bash
   pio run                 # kompilieren
   pio run -t upload       # per USB flashen
   pio device monitor      # serieller Monitor @115200
   ```
PubSubClient wird automatisch aus `lib_deps` geholt. Für die **pioarduino**-Variante
(neuere ESP32-Cores) in `platformio.ini` die `platform`-Zeile auf die pioarduino-URL
umstellen (Kommentar dort beachtet). OTA-Upload: `upload_protocol`/`upload_port`
in `platformio.ini` aktivieren.

## Inbetriebnahme
1. `secrets.h` anlegen (siehe oben), `zaehler-esp32/zaehler-esp32.ino` in der
   Arduino-IDE öffnen.
2. **Erster Flash per USB** (danach reicht das Web-OTA unter `/update`).
3. Seriellen Monitor @115200 öffnen → IP-Adresse notieren.
4. Im Browser `http://<IP>/` öffnen → Live-Anzeige; `http://<IP>/api` liefert das
   rohe JSON (gut zum Debuggen).

## Weboberfläche (handytauglich, mehrseitig)
Oben auf jeder Seite eine Navigationsleiste: **Start · Strom · Wärme · Update**.

- **Start (`/`)** — Übersicht: aktuelle Strom- (W, Bezug, Einspeisung) und
  Wärmewerte (6.8 MWh), WLAN-RSSI, Uptime sowie **MQTT-Karte**: Verbindungsstatus
  + Felder für **Host/IP, Port, User, Passwort** (speichern → verbindet neu;
  leeres Passwortfeld lässt das gespeicherte PW unverändert).
- **Strom (`/strom`)** — **alle** ausgelesenen OBIS-Werte (generischer SML-Scan),
  Schalter **Auslesen AN/AUS**, **Lesekopf-GPIO** und **MQTT-Sendeintervall
  (2–300 s)** auswählen + speichern. Das Sendeintervall ist das Pendant zu
  Tasmotas `TelePeriod` (dort min. 10 s); der SML-Zähler selbst sendet 1–2×/s,
  schneller als ~2 s bringt also keine neuen Werte.
- **Wärme (`/waerme`)** — alle UH50-Werte, Schalter **Auslesen AN/AUS**,
  **Leseintervall 1–24 h**, **TX-/RX-GPIO** auswählen, „Jetzt lesen", Sign-on
  `/?! ↔ /#!` umschalten.
- **Update (`/update`)** — Firmware-Upload (siehe unten).

Alle Einstellungen (an/aus, Intervall, GPIOs) liegen im **NVS (`Preferences`,
Namespace `zaehler`)** und überstehen einen Reboot.

Steuer-Endpunkte (auch per `curl`): `GET /api` (JSON),
`GET /setheat?en=0|1&h=N&tx=G&rx=G`, `GET /setstrom?en=0|1&rx=G&s=Sek`,
`GET /setmqtt?host=...&port=1883&user=...&pw=...`, `GET /read`, `GET /toggle`.
Hinweis: `/api` liefert `mqtt_host/port/user` + `mqtt_haspw`, das **Passwort
selbst wird nie ausgeliefert**.

## Firmware-Update (Web-OTA)
`http://<IP>/update` → in der Arduino-IDE **Sketch → Kompilierte Binärdatei
exportieren**, die `.ino.bin` hochladen. Zuverlässiger als ArduinoOTA/UDP.

## Troubleshooting (die zwei wahrscheinlichen Stolpersteine)
- **Wärmezähler antwortet nicht** (`no_response`): auf der Webseite „Anfrage
  /?! ↔ /#!" umschalten (normal `/?!`). TX/RX am Lesekopf vertauscht? GPIO16/17
  prüfen (Lesekopf Tx → GPIO16, Rx → GPIO17).
- **Strom: keine/falsche Werte**: `SML_INVERT` auf `true` setzen (manche
  Hichi-Köpfe geben ein invertiertes Signal aus). Prüfen, ob im `/api`-JSON
  `strom.status` auf `ok` springt.

## MQTT-Topics (ioBroker)
- `waermezaehler/data/6_8` … (reiner Wert je L+G-Code) + `.../unit`, `.../raw`
- `waermezaehler/status`, `waermezaehler/interval_h`, `waermezaehler/online` (LWT)
- `stromzaehler/bezug_kwh`, `stromzaehler/einspeisung_kwh`, `stromzaehler/leistung_w`
- `stromzaehler/data/<OBIS>` … (alle generisch geparsten Werte) + `.../unit`
- `stromzaehler/status`, `stromzaehler/send_s` (MQTT-Sendeintervall)

## Bekannte Grenzen
- SML-CRC wird nicht geprüft (Resync über Escape-Sequenzen).
- Generischer SML-Scan: nur Strom-Register (OBIS-Medium 1) mit Integer-Wert;
  Mehrbyte-Zählerstände werden als 32-Bit gelesen (für Haushaltswerte ausreichend).
- Wärme-Lesezyklus blockiert ~5–6 s pro Intervall → Webseite hakt dann kurz;
  SML-Verlust ist durch den 4-KB-RX-Puffer abgefangen. Bei 1–24 h Intervall fällt
  das praktisch nicht ins Gewicht.
- GPIO-Auswahl: Eingangs-Pins inkl. 34–39 (nur Eingang!) für RX; TX nur
  ausgangsfähige Pins. Flash-Pins 6–11 sind nicht wählbar.
