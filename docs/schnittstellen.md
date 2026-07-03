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
  - **🔥 Wärme:** Auslesen AN/AUS, **Leseintervall 1–24 h**, TX-/RX-GPIO.
  - **MQTT:** **An/Aus-Schalter (Default aus)**, Verbindungsstatus, **Haupttopic**,
    **Host/IP, Port, User, Passwort** (speichern → verbindet neu; leeres
    Passwortfeld lässt das gespeicherte PW unverändert).
  - **Firmware-Update** (Web-OTA).

Alle Einstellungen (an/aus, Intervalle, GPIOs, MQTT-Broker, Haupttopic) liegen im
**NVS (`Preferences`, Namespace `zaehler`)** und überstehen einen Reboot.

## Steuer-Endpunkte (auch per `curl`)
- `GET /api` — kompletter Zustand als JSON
- `GET /setheat?en=0|1&h=N&tx=G&rx=G` — Wärme: an/aus, Intervall (h), TX-/RX-GPIO
- `GET /setstrom?en=0|1&rx=G&s=Sek` — Strom: an/aus, RX-GPIO, Sendeintervall
- `GET /setmqtt?en=0|1&root=...&host=...&port=1883&user=...&pw=...` — MQTT konfigurieren
- `GET /read` — Wärme sofort lesen

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
- `status`, `interval_h`

**Strom (`<root>/Power/…`)**:
- `bezug_kwh`, `einspeisung_kwh`, `leistung_w` — benannte Hauptwerte
- `data/<OBIS>` — alle generisch geparsten Werte + `/unit`
- `status`, `send_s` (MQTT-Sendeintervall)
