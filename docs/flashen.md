# Flashen & Einrichten

Zwei Wege, die Firmware aufs Gerät zu bekommen:

1. **Fertig-Image flashen** (`.bin`) — kein Compiler nötig, ideal für den Erststart.
2. **Web-OTA** — Update über den Browser, wenn die Firmware schon läuft.

Danach das **WLAN über das Setup-Portal** einrichten (siehe unten).

---

## 1. Fertig-Image per USB flashen (Erststart)

Fertige Binärdateien liegen unter
[**Releases**](https://github.com/tt-tom17/ESP32smartmeter/releases). Es gibt zwei:

| Datei                                | Wofür                                   | Flash-Offset |
|--------------------------------------|-----------------------------------------|--------------|
| `ESP32smartmeter-<version>-factory.bin` | **USB-Erstflash** (Komplett-Image)      | `0x0`        |
| `ESP32smartmeter-<version>-ota.bin`     | **Web-OTA** (siehe Abschnitt 2)         | –            |

Für einen jungfräulichen ESP32 immer die **`factory.bin`** nehmen — sie enthält
Bootloader, Partitionstabelle und App in einem und wird an Offset `0x0` geschrieben.

### Variante A — im Browser (am einfachsten)
1. [esptool-js Web-Flasher](https://espressif.github.io/esptool-js/) in Chrome/Edge öffnen.
2. **Connect** → seriellen Port des ESP32 wählen.
3. Bei *Flash Address* **`0x0`** eintragen, die `factory.bin` auswählen.
4. **Program** — fertig.

> Alternativ das grafische **Espressif Flash Download Tool** (Windows) oder
> jedes ESP-Web-Tools-basierte Flasher-Frontend; überall die `factory.bin` an `0x0`.

### Variante B — Kommandozeile (esptool)
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  write_flash 0x0 ESP32smartmeter-<version>-factory.bin
```
(Port ggf. anpassen, z. B. `COM5` unter Windows.)

---

## 2. Firmware-Update per Web-OTA

Sobald die Firmware läuft und im WLAN ist, gehen weitere Updates ohne USB:

`http://<IP>/update` (Seite **Einstellungen**) → Firmware hochladen. Datei ist die
**`ota.bin`** aus den Releases (bzw. die selbst gebaute `firmware.bin`).

Per `curl`:
```bash
curl -F "f=@ESP32smartmeter-<version>-ota.bin" http://<IP>/update
```

> Ein OTA-Update ersetzt **nur die Firmware**, nicht den NVS — WLAN-Zugangsdaten
> und Einstellungen bleiben erhalten.

---

## 3. WLAN einrichten (Setup-Portal)

Die WLAN-Zugangsdaten werden **nicht** einkompiliert, sondern beim Erststart über
ein eigenes Setup-WLAN eingerichtet und im NVS gespeichert:

1. Nach dem ersten Flashen (oder wenn keine WLAN-Daten hinterlegt sind) öffnet der
   ESP ein **offenes WLAN `Zaehler-Setup`**.
2. Mit Handy/Laptop verbinden — die Setup-Seite erscheint automatisch
   (Captive Portal), sonst im Browser **`http://192.168.4.1`** öffnen.
3. Über **Suchen** das eigene WLAN wählen, Passwort eingeben, **Speichern & Neustart**.
4. Der ESP startet neu und verbindet sich als Station mit dem Heim-WLAN.

Später lässt sich das WLAN unter **Einstellungen → WLAN → „WLAN vergessen"**
zurücksetzen (öffnet nach dem Neustart wieder das Setup-Portal).

---

## 4. Inbetriebnahme

1. **Erster Flash per USB** (danach reicht das Web-OTA unter `/update`).
2. WLAN über das **Setup-Portal** einrichten (Abschnitt 3).
3. IP-Adresse herausfinden (Router-Liste oder serieller monitor, wenn der ESP noch am Rechner angeschlossen ist).
4. Im Browser `http://<IP>/` öffnen → Live-Anzeige; `http://<IP>/api` liefert das
   rohe JSON (gut zum Debuggen).
5. Optional: unter **Einstellungen** MQTT **aktivieren**, den Broker (Host/IP, Port,
   ggf. User/PW) und bei Bedarf das **Haupttopic** eintragen → speichern, der ESP
   verbindet sich neu. (MQTT ist im Auslieferungszustand **aus**.)
