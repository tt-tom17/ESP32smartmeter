# Wärme-Auslesung (D0 / IEC 62056-21)

Der Landis+Gyr UH50/T550 wird nach IEC-Standard über die optische D0-Schnittstelle
ausgelesen. Der folgende Ablauf ist am echten UH50 verifiziert:

1. UART1 @ **300 Baud 7E1** öffnen, **40× `0x00`** Wake-up senden, dann Sign-on
   **`/?!\r\n`**.
2. Identifikation `/MMMZident\r\n` @300 lesen (z. B. `/LUGCUH50`). Das **5. Zeichen**
   kodiert die Datenbaudrate (Ziffer 0–6 = Mode C mit ACK, Buchstabe A–F = Mode B).
3. Bei Mode C ein **ACK** `0x06 '0' Z '0' \r\n` @300 senden.
4. Auf die Datenbaudrate (beim UH50 **2400, Mode B**) 7E1 umschalten und den
   Datenblock bis `!` lesen → ~66 OBIS-Codes.

Wichtigster Wert: **`6.8` = Wärmemenge in MWh**.

Falls der Zähler nicht antwortet, lässt sich der Sign-on per `/toggle`-Endpunkt auf
`/#!` umschalten (siehe [Troubleshooting](troubleshooting.md)).
