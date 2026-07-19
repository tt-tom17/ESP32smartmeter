# Schnittstellen: Weboberfläche, API & MQTT

## Weboberfläche (handytauglich, mehrseitig)
Die Oberfläche läuft auf einem **asynchronen Webserver** — sie bleibt auch während
eines Lesezyklus reaktionsfähig. Navigationsleiste oben:
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
  - **🔥 Wärme:** Auslesen AN/AUS, **Startuhrzeit** + **Intervall (nur Teiler von 24 h:
    1/2/3/4/6/8/12/24)**, TX-/RX-GPIO. Abfragen laufen zu festen Wanduhrzeiten (NTP,
    sommer-/winterzeitfest); die Startseite zeigt die nächste Uhrzeit + Countdown.
  - **MQTT:** **An/Aus-Schalter (Default aus)**, Verbindungsstatus, **Haupttopic**,
    **Host/IP, Port, User, Passwort** (speichern → verbindet neu; leeres
    Passwortfeld lässt das gespeicherte PW unverändert).
  - **Firmware-Update** (Web-OTA).

Alle Einstellungen (an/aus, Intervalle, GPIOs, MQTT-Broker, Haupttopic) liegen im
**NVS (`Preferences`, Namespace `zaehler`)** und überstehen einen Reboot.

## Steuer-Endpunkte (auch per `curl`)
Dieselbe Konfiguration, die die Weboberfläche unter **Einstellungen** setzt, geht auch
direkt per HTTP — praktisch für Skripte, Automatisierung oder schnelles Testen.

**Wie es funktioniert:**
- **Alles sind `GET`-Anfragen** — auch die Setter. Parameter hängen als Query-String
  (`?key=wert&key2=wert2`) an der URL, es gibt keinen Request-Body.
- **Alle Parameter sind optional** — mitgegebene Werte werden übernommen, weggelassene
  bleiben unverändert (partielles Update). `curl "http://<IP>/setheat?en=0"` schaltet nur
  das Auslesen aus und lässt Intervall/GPIOs in Ruhe.
- **Antwort der Setter** ist schlicht `text/plain` mit `ok`. Nur `/api` liefert JSON.
- **`<IP>`** ist die Adresse des Geräts im WLAN (im Footer/Router ablesbar, z. B.
  `192.168.178.217`). Werte werden im NVS gespeichert und überstehen einen Reboot.

**Endpunkte:**
- `GET /api` — kompletter Zustand als JSON (Strom, Wärme, WLAN, MQTT-Status, Zeitplan);
  enthält u. a. `uptime_s`, `fw_ver`/`fw_build` und `reset_reason` (Grund des letzten
  Neustarts — zur Diagnose von Selbst-Reboots, siehe [troubleshooting.md](troubleshooting.md))
- `GET /setheat?en=0|1&start=HH:MM&h=N&tx=G&rx=G` — Wärme: an/aus, Startuhrzeit,
  Intervall (h; wird auf den nächsten Teiler von 24 eingerastet), TX-/RX-GPIO
- `GET /setstrom?en=0|1&rx=G&s=Sek` — Strom: an/aus, RX-GPIO, Sendeintervall
- `GET /setsendled?en=0|1&gpio=G&lvl=0|1` — Sende-Diode des SML-Lesekopfs „parken":
  an/aus, GPIO, Pegel (`1`=HIGH, `0`=LOW; welcher Pegel die Diode dunkel hält, hängt vom
  Kopf ab). Verhindert, dass die Sende-IR-Diode in den eigenen Empfänger streut; Status
  im `/api`-JSON unter `sendled.enabled/gpio/level` (siehe [troubleshooting.md](troubleshooting.md))
- `GET /setmqtt?en=0|1&root=...&host=...&port=1883&user=...&pw=...` — MQTT konfigurieren
  (leeres/weggelassenes `pw` lässt das gespeicherte Passwort unverändert)
- `GET /read` — Wärme **jetzt** einmalig lesen (verschiebt den geplanten Zeitplan nicht)
- `GET /toggle` — Sign-on-Sequenz des Wärmezählers umschalten (`/?!` ↔ `/#!`), falls ein
  Zähler auf die Standard-Sequenz nicht antwortet; Antwort = aktive Sequenz

**Beispiele:**
```bash
IP=192.168.178.217

curl "http://$IP/api"                                   # Zustand als JSON abrufen
curl "http://$IP/api" | jq .heat.next_at                # nächste geplante Wärme-Abfrage

curl "http://$IP/read"                                  # Wärme sofort einmal lesen

curl "http://$IP/setheat?start=05:55&h=6"               # Raster 05:55, alle 6 h
curl "http://$IP/setheat?en=0"                          # Wärme-Auslesen aus (Rest bleibt)
curl "http://$IP/setheat?h=5"                            # 5 -> rastet auf 4 h ein

curl "http://$IP/setstrom?en=1&s=10"                    # Strom an, MQTT-Sendeintervall 10 s

curl "http://$IP/setmqtt?en=1&host=192.168.178.55&port=1883&root=ESP32smartmeter"
curl "http://$IP/setmqtt?user=zaehler&pw=geheim"        # nur Zugangsdaten setzen
```

> `/api` liefert `mqtt_en` + `mqtt_root/host/port/user` + `mqtt_haspw`, das
> **Passwort selbst wird nie ausgeliefert**.

## MQTT-Topics
Unter einem editierbaren **Haupttopic `<root>`** (Default `ESP32smartmeter`) mit den
zwei Zweigen **`Heat`** und **`Power`**:

**Gerät**
- `<root>/online` — Verbindungsstatus (LWT)

**Wärme (`<root>/Heat/…`)** — benannte Hauptwerte (je mit `/unit`):
- `zaehlerstand_mwh` (6.8), `volumen_m3` (6.26), `leistung_kw` (6.6),
  `durchfluss_m3h` (6.33), `betriebsstunden_h` (6.31), `batterie_monate` (6.35),
  `vorlauf_c` / `ruecklauf_c` (aus 9.4)
- `data/<L+G-Code>` — **alle** Codes generisch + `/unit`, `/raw`
- `status`, `interval_h` (Leseintervall in h, retained)
- `next_read` — **nächste geplante Abfrage** als lokale Zeit `YYYY-MM-DD HH:MM`
  (retained; ohne NTP-Sync `unknown`). Aktualisiert nach jeder *geplanten* Abfrage
  und bei Änderung von Intervall/Startuhrzeit.

**Strom (`<root>/Power/…`)**:
- `bezug_kwh`, `einspeisung_kwh`, `leistung_w` — benannte Hauptwerte
- `data/<OBIS>` — alle generisch geparsten Werte + `/unit`
- `status`, `send_s` (MQTT-Sendeintervall)
