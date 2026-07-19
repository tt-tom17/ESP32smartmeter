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
| `brownout` | Versorgungsspannung eingebrochen | **Stromversorgung** — stabiles 5-V-Netzteil / kabelgebundener Uplink (W5500) statt WLAN-Peaks |
| `poweron` | echtes Ein-/Ausschalten (Stromausfall) | Steckkontakt / Netzteil prüfen |
| `panic` | Firmware-Absturz (Exception) | **Firmware-Bug** — Ethernet o. Ä. hilft *nicht* |
| `int_wdt` / `task_wdt` / `wdt` | Watchdog: `loop()`/Task hing zu lange | Firmware-Bug / Blockade |
| `sw` | gewollter Software-Reboot (u. a. nach OTA-Flash) | kein Problem |
| `ext` | Reset-Pin / externer Reset | Verdrahtung / EN-Pin |
| `deepsleep` | Aufwachen aus Deep-Sleep | (hier nicht genutzt) |
| `unknown` | Grund nicht ermittelbar | — |

Kurz: `brownout`/`poweron` deuten auf die **Stromseite**, `panic`/`*_wdt` auf die
**Firmware**. Direkt nach einem OTA-Flash steht hier korrekt `sw`.

## Bekannte Grenzen
- SML-CRC wird nicht geprüft (Resync über Escape-Sequenzen).
- Generischer SML-Scan: nur Strom-Register (OBIS-Medium 1) mit Integer-Wert;
  Mehrbyte-Zählerstände werden als 32-Bit gelesen (für Haushaltswerte ausreichend).
- Der Wärme-Lesezyklus (~5–6 s) blockiert die `loop()`; dank Async-Webserver bleibt
  die Oberfläche aber bedienbar. MQTT-Publishes pausieren in dieser Zeit kurz; bei
  1–24 h Intervall fällt das nicht ins Gewicht.
- **Thread-Safety:** Web-Handler laufen in einem eigenen Task; MQTT (PubSubClient)
  und die UARTs sind nicht thread-safe. Setter setzen daher nur Werte/Flags, die
  Seiteneffekte führt `loop()` aus.
- **GPIO-Auswahl:** Eingangs-Pins inkl. 34–39 (nur Eingang!) für RX; TX nur
  ausgangsfähige Pins. Flash-Pins 6–11 sind nicht wählbar.
