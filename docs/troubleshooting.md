# Troubleshooting & Grenzen

## Die zwei wahrscheinlichen Stolpersteine
- **Wärmezähler antwortet nicht** (`no_response`): Sign-on per `/toggle`-Endpunkt
  auf `/#!` umschalten (normal `/?!`). TX/RX am Lesekopf vertauscht? GPIO16/17
  prüfen (Lesekopf Tx → GPIO16, Rx → GPIO17).
- **Strom: keine/falsche Werte**: `SML_INVERT` in `config.h` auf `true` setzen
  (manche Leseköpfe geben ein invertiertes Signal aus). Prüfen,
  ob im `/api`-JSON `strom.status` auf `ok` springt.

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
