# ESP32 Wärme- + Stromzähler-Reader

Der ESP32-WROOM-32 liest **Wärmezähler** (Landis+Gyr UH50/T550, D0/IEC 62056-21)
und **Stromzähler** (SML) über ein TTL-IR-Lesekopf aus, zeigt alles auf einer eigenen,
handytauglichen Weboberfläche an und schickt es optional per **MQTT** an einen Broker
(z. B. Mosquitto / ioBroker).

- **Zwei Zähler, ein Gerät** — Wärme und Strom gleichzeitig.
- **Eigene Weboberfläche** (Start · Strom · Wärme · Einstellungen), reaktionsfähig
  dank asynchronem Webserver.
- **MQTT optional** — standardmäßig **aus**; ohne Broker läuft das Gerät als reines
  Web-Display. Aktivierung, Broker und Haupttopic zur Laufzeit im Web einstellbar.
- **LAN oder WLAN** (ab FW 1.6.0) — Ethernet über ein **W5500**-Modul am SPI-Bus oder
  klassisch WLAN. Die Betriebsart ist im Web umschaltbar; *Auto* bevorzugt LAN und
  nimmt das WLAN als Rückfall.
- **Web-OTA** — Updates später ohne USB, über LAN genauso wie über WLAN.
- **WLAN-Setup-Portal** — Zugangsdaten übers Handy eintragen, ohne neu zu flashen.

## Schnellstart (fertig flashen)

1. Aktuelle **`ESP32smartmeter-<version>-factory.bin`** von den
   [**Releases**](https://github.com/tt-tom17/ESP32smartmeter/releases) laden.
2. Per Browser-Flasher ([esptool-js](https://espressif.github.io/esptool-js/)) oder
   `esptool` an Offset **`0x0`** auf den ESP32 schreiben.
3. Mit dem offenen WLAN **`Zaehler-Setup`** verbinden → Heim-WLAN eintragen.
   *(Mit LAN-Kabel entfällt das — der ESP holt sich direkt eine IP.)*
4. `http://<IP>/` im Browser öffnen — fertig.

Ausführlich (inkl. Web-OTA & WLAN-Portal): **[docs/flashen.md](docs/flashen.md)**.

## Verdrahtung

### Wärmezähler-Lesekopf (D0, optisch) — UART1
| Lesekopf      | ESP32        |
|---------------|--------------|
| Rx (Eingang)  | GPIO17 (TX)  |
| Tx (Ausgang)  | GPIO16 (RX)  |
| VCC           | 3V3          |
| GND           | GND          |

### Strom-Lesekopf (Strom, SML) — UART2
| Lesekopf      | ESP32        |
|---------------|--------------|
| TX (Daten)    | GPIO27 (RX)  |
| VCC           | 3V3          |
| GND           | GND          |

> Beim Strom-Lesekopf reicht **eine Datenleitung** (TX → ESP32 RX), da der SML-Zähler von
> selbst sendet. (RX frei) Die GPIOs sind zur Laufzeit über die
> Weboberfläche umstellbar.

### Ethernet-Modul W5500 (optional, ab FW 1.6.0) — SPI

Nur nötig, wenn das Gerät per Kabel ans Netz soll. Ohne Modul läuft alles unverändert
über WLAN weiter.

| W5500-Modul | ESP32   | `config.h`        |
|-------------|---------|-------------------|
| `SCLK`      | GPIO18  | `W5500_SCK_PIN`   |
| `MISO`      | GPIO19  | `W5500_MISO_PIN`  |
| `MOSI`      | GPIO23  | `W5500_MOSI_PIN`  |
| `SCS`       | GPIO21  | `W5500_CS_PIN`    |
| `RST`       | GPIO26  | `W5500_RST_PIN`   |
| `INT`       | —       | `W5500_IRQ_PIN` = `-1` (siehe unten) |
| `5V`        | 5V      | — |
| `GND`       | GND     | — |

> **Versorgung an `5V`.** Das Modul hat einen eigenen AMS1117-3.3 an Bord und ist
> 5-V-signaltolerant; über `5V` entlastet es den kleinen Regler des ESP-Boards.
> Alternativ geht `3.3V` → 3V3 — aber **immer nur einen der beiden Pins belegen**.

> **`INT` bleibt unbeschaltet.** Der Ethernet-Treiber des Arduino-Core 3.x läuft ohne
> Interrupt (`ETH_SPI_SUPPORTS_NO_IRQ`, pollt alle 10 ms — bei dieser Datenmenge
> unkritisch). Ein bereits gelöteter INT-Draht darf am Modul bleiben; GPIO34 ist
> deshalb aus der GPIO-Auswahl gesperrt.

Der SPI-Takt steht bewusst auf **8 MHz** (`W5500_SPI_MHZ`) statt der zunächst
verwendeten 20 MHz — siehe [Troubleshooting](docs/troubleshooting.md#lan-w5500).

### Pin-Referenz — beide Boards

Die Firmware ist board-unabhängig (`board = esp32dev`); es unterscheidet sich nur die
physische Lage der Pins. Alle benötigten GPIOs (16, 17, 18, 19, 21, 23, 25, 26, 27) sowie
`+5V`, `+3.3V` und `GND` sind auf **beiden** Boards herausgeführt:

| Board | Status |
|---|---|
| **ESP32 Dev Kit C V4** (38 Pins) | aktuell verbaut, ab FW 1.6.0 mit LAN |
| **MH-ET LIVE D1 mini ESP32** | Bestandsgerät, weiterhin unterstützt |


Die Default-Pins **GPIO16/17/27** sind auf beiden Boards „grün = immer nutzbar“. Wer die
GPIOs über die Weboberfläche umstellt, sollte beachten:

- **RX / Dateneingänge** (Wärme-RX, Strom-RX): grüne Pins **oder** die reinen Eingangs-Pins
  **GPIO35/36/39** (nur Eingang → ideal als Lesekopf-RX).
- **TX / Ausgang** (Wärme-TX): nur output-fähige Pins — **nicht** GPIO34–39.
- **Tabu:** GPIO6–11 (interner SPI-Flash) sowie die für den W5500 reservierten
  **18/19/21/23/26** und **34**. Die Pin-Auswahl der Weboberfläche (INPINS/OUTPINS)
  bietet ohnehin nur die zulässigen Pins an — die vollständige Liste steht unter
  [Troubleshooting → GPIO-Auswahl](docs/troubleshooting.md#bekannte-grenzen).

## Dokumentation

- **[Flashen & Einrichten](docs/flashen.md)** — `.bin`-Flash, Web-OTA, WLAN-Setup-Portal, Inbetriebnahme
- **[Selber bauen](docs/bauen.md)** — PlatformIO & Arduino IDE, Projektstruktur, Versionierung
- **[Wärme-Auslesung](docs/waermezaehler.md)** — D0 / IEC-62056-21-Ableseablauf
- **[Weboberfläche](docs/weboberflaeche.md)** — Seiten-Rundgang (Start/Strom/Wärme/Einstellungen) & Setup-Portal
- **[Schnittstellen](docs/schnittstellen.md)** — JSON-API, `curl`-Endpunkte, MQTT-Topics
- **[Troubleshooting & Grenzen](docs/troubleshooting.md)**
