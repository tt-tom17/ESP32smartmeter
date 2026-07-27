# Troubleshooting & Grenzen

## Die zwei wahrscheinlichen Stolpersteine
- **Wärmezähler antwortet nicht** (`no_response`): Sign-on per `/toggle`-Endpunkt
  auf `/#!` umschalten (normal `/?!`). TX/RX am Lesekopf vertauscht? GPIO16/17
  prüfen (Lesekopf Tx → GPIO16, Rx → GPIO17).
- **Strom: keine/falsche/lückenhafte Werte** — meist die **Sende-IR-Diode des
  Lesekopfs**, die den eigenen Empfänger blendet. Viele IR-Leseköpfe haben neben dem
  Empfänger eine Sende-Diode (Rückrichtung zum Zähler); leuchtet sie, kommt das
  SML-Telegramm zerhackt an und Register fallen weg — sieht aus wie ein PIN-gesperrter
  Zähler, ist aber nur Einstreuung. **Abhilfe:** Sende-Diode dunkel halten. Die Firmware
  legt dafür einen GPIO fest auf einen Pegel (Verdrahtung: Sende-Diode → GPIO,
  Default 25). Web → Einstellungen → ⚡ Strom „Sende-Diode" (AN, GPIO, Pegel) oder
  `curl "http://<IP>/setsendled?en=1&gpio=25&lvl=1"` (`lvl` 1=HIGH / 0=LOW — welcher
  Pegel dunkel hält, hängt vom Kopf ab, notfalls beide testen). Hilft das nicht:
  `SML_INVERT` in `config.h` testen. Prüfen, ob `strom.status` im `/api`-JSON auf `ok`
  springt.

  **CRC-Zähler zur Eingrenzung nutzen.** Die Firmware prüft seit Commit `87e159a` (19.07.2026)
  die SML-Transport-CRC16 und verwirft fehlerhafte Telegramme; im `/api`-JSON stehen dazu
  `strom.crc_ok` und `strom.crc_err`:

  ```bash
  curl -s "http://<IP>/api" | jq '{ok: .strom.crc_ok, err: .strom.crc_err, up: .uptime_s}'
  ```

  Steigt `crc_err`, lohnt ein Blick auf Sitz und Verkabelung des Lesekopfs; die Ursache ist
  bislang nicht abschließend geklärt. **Zum Vergleichen immer `uptime_s` mitschreiben** — die
  Zähler laufen seit dem Boot, und die Fehler treten in Klumpen auf: eine einzelne
  Kurzmessung von ein bis zwei Minuten kann zufällig in eine Lücke fallen und Fehlerfreiheit
  vortäuschen. Aussagekräftig ist nur die Rate über mehrere Ablesungen hinweg.

## Unerwartete Neustarts einordnen (`reset_reason`)
Bootet der ESP von selbst neu (Uptime springt zurück, MQTT-Lücke), sagt das Feld
`reset_reason` im `/api`-JSON, **woran** es lag — ohne Serial- oder Log-Zugang.
Der Wert stammt aus `esp_reset_reason()` und bleibt über die ganze Laufzeit stabil,
beschreibt also immer den **letzten** Reset.

```bash
curl "http://<IP>/api" | jq .reset_reason
```

| Wert | Bedeutung | Richtung |
| --- | --- | --- |
| `brownout` | Versorgungsspannung eingebrochen | **Stromversorgung** — stabiles 5-V-Netzteil |
| `poweron` | echtes Ein-/Ausschalten (Stromausfall) | Steckkontakt / Netzteil prüfen |
| `panic` | Firmware-Absturz (Exception) | **Firmware-Bug** |
| `int_wdt` / `task_wdt` / `wdt` | Watchdog: `loop()`/Task hing zu lange | Firmware-Bug / Blockade |
| `sw` | gewollter Software-Reboot (u. a. nach OTA-Flash) | kein Problem |
| `ext` | Reset-Pin / externer Reset | Verdrahtung / EN-Pin |
| `deepsleep` | Aufwachen aus Deep-Sleep | (hier nicht genutzt) |
| `unknown` | Grund nicht ermittelbar | — |

Kurz: `brownout`/`poweron` deuten auf die **Stromseite**, `panic`/`*_wdt` auf die
**Firmware**. Direkt nach einem OTA-Flash steht hier korrekt `sw`.

## LAN (W5500)

Ab FW 1.6.0 kann der ESP statt oder neben dem WLAN über ein **W5500**-Modul am SPI-Bus
ans Netz. Erste Anlaufstelle ist immer das `/api`-JSON:

```bash
curl -s "http://<IP>/api" | jq '{mode: .net_mode, iface: .net_if, ip: .ip, eth: .eth}'
```

`net_if` sagt, worüber das Gerät **gerade** erreichbar ist (`eth` / `wifi`), das Objekt
`eth` liefert `link`, `speed` (10/100), `duplex` und die MAC. Auf der Startseite steht
dasselbe unter **Verbindung → Netz**.

| Symptom | Ursache / Abhilfe |
| --- | --- |
| `eth.link` bleibt `false` | Kabel/Switch prüfen; Versorgung des Moduls (`5V`, `GND`) und die fünf SPI-Drähte SCLK18 · MISO19 · MOSI23 · SCS21 · RST26 nachmessen. |
| Link da, aber keine IP | DHCP im Netz? In der Betriebsart *Auto* wartet der ESP 8 s auf eine LAN-IP und zieht danach zusätzlich das WLAN hoch — `net_if` zeigt dann `wifi`. |
| Log: `emac_w5500_receive: write RX RD failed` bzw. `read PHY register failed`, LAN bricht unter Last weg | **SPI-Takt zu hoch.** Bei 20 MHz brach die Strecke unter Volllast (Web-OTA, 1,3 MB) reproduzierbar zusammen — typisch für fliegende Verdrahtung ohne kurzen GND-Rückweg. `W5500_SPI_MHZ` steht deshalb auf **8**; das reicht für 100 Mbit/s bei dieser Datenmenge um Größenordnungen. Erst bei sauberer Platine wieder erhöhen. |
| Boot-Fehler `gpio_pullup_en(85): GPIO number error` | Der als `INT` verdrahtete **GPIO34 ist input-only und hat keinen internen Pull-up**. Der Treiber läuft ohne Interrupt (`W5500_IRQ_PIN` = `-1`, pollt alle 10 ms) — im Feld verifiziert. Der Draht darf am Modul bleiben. |
| Gerät nach Umschalten auf *nur LAN* nicht mehr erreichbar | Kein Grund zur Panik: Bleibt in dieser Betriebsart 2 min lang eine LAN-IP aus, öffnet der ESP von selbst das Setup-Portal (`Zaehler-Setup`). Ein USB-Reflash ist dafür nicht nötig. |

> Jede Umschaltung der Betriebsart (*Auto* / *nur LAN* / *nur WLAN*) **startet das Gerät
> absichtlich neu** — Interfaces zur Laufzeit umzuhängen ist beim ESP32 fragil.

> Der Verbindungs-Watchdog prüft seit 1.6.0 „**irgendein** Interface oben" statt nur das
> WLAN: Mit gestecktem Kabel führt ein WLAN-Ausfall also nicht mehr zum Reboot.

## Bekannte Grenzen
- Generischer SML-Scan: nur Strom-Register (OBIS-Medium 1) mit Integer-Wert;
  Mehrbyte-Zählerstände werden als 32-Bit gelesen (für Haushaltswerte ausreichend).
- Der Wärme-Lesezyklus (~5–6 s) blockiert die `loop()`; dank Async-Webserver bleibt
  die Oberfläche aber bedienbar. MQTT-Publishes pausieren in dieser Zeit kurz; bei
  1–24 h Intervall fällt das nicht ins Gewicht.
- **Thread-Safety:** Web-Handler laufen in einem eigenen Task; MQTT (PubSubClient)
  und die UARTs sind nicht thread-safe. Setter setzen daher nur Werte/Flags, die
  Seiteneffekte führt `loop()` aus.
- **GPIO-Auswahl:** Die Firmware lässt nur eine feste Positivliste zu
  (`validInPin()`/`validOutPin()` in `web.h`) — sie ist deutlich enger als „alles außer den
  Flash-Pins":

  | Verwendung | erlaubte GPIOs |
  | --- | --- |
  | RX **und** TX (Ein-/Ausgang) | 16, 17, 22, 25, 27, 32, 33 |
  | nur RX (reine Eingänge) | 35, 36, 39 |

  Gesperrt sind damit außer den Flash-Pins 6–11 auch **37/38**, die UART0-Pins **1/3**, die
  Strapping-Pins **0/2/12/15** sowie die am ESP32 nicht herausgeführten **20/24/28–31**.
  **Seit FW 1.6.0 zusätzlich gesperrt: 18/19/21/23/26 und 34** — sie gehören dem
  W5500 (SPI, Reset, INT). Ein Lesekopf auf dem SPI-Bus würde das LAN mit lahmlegen.
  Die Defaults 17/16/27 sind davon nicht betroffen, bestehende Geräte müssen also
  nichts umstellen.
  ⚠️ **Ungültige Werte werden still verworfen** — der Setter antwortet trotzdem mit `ok`,
  der alte Pin bleibt stehen. Nach dem Setzen im `/api`-JSON gegenprüfen.
