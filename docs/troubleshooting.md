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
  | RX **und** TX (Ein-/Ausgang) | 16–19, 21–23, 25–27, 32, 33 |
  | nur RX (reine Eingänge) | 34, 35, 36, 39 |

  Gesperrt sind damit außer den Flash-Pins 6–11 auch **37/38**, die UART0-Pins **1/3**, die
  Strapping-Pins **0/2/12/15** sowie die am ESP32 nicht herausgeführten **20/24/28–31**.
  ⚠️ **Ungültige Werte werden still verworfen** — der Setter antwortet trotzdem mit `ok`,
  der alte Pin bleibt stehen. Nach dem Setzen im `/api`-JSON gegenprüfen.
