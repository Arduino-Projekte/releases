# Word-Clock IV (ESP32-S3 / XIAO ESP32S3 Plus)

Arduino-/ESP32-Projekt mit Web-UI, API-Endpunkten, Timern und (optional) Audio/SD-Management.

> Einstiegspunkt ist **`main/main.ino`**.

## Quick Start (Arduino IDE)

1. Arduino IDE öffnen → **Sketch öffnen:** `main/main.ino`
2. ESP32-Boardpaket installieren/aktivieren ("ESP32 by Espressif Systems").
3. Board/Settings passend zu deinem Setup auswählen (typisch **XIAO ESP32S3 Plus** oder **ESP32S3 Dev Module**).
4. **PSRAM: an** (wird vom Projekt erwartet).
5. Partition Scheme: **eins wählen, das LittleFS unterstützt** (sonst kann LittleFS nicht mounten).
6. Flashen.
7. Serial Monitor öffnen → IP-Adresse aus dem Log ablesen.
8. Web-UI öffnen:
   - `http://<ip>/`
   - (wenn mDNS aktiv) `http://word-clock-iv.local/`  *(Default-Hostname kann in den Settings geändert werden)*

## Projektstruktur

- **/main/** – Arduino-Sketch + gesamte Firmware (Entry: `main/main.ino`)
- **/docs/** – Dokumentation (API, Smoke-Test, Patch-Notizen)
- **/word-clock/** – Platzhalter/Altstruktur (siehe Hinweis unten)

## Doku

- **API:** `docs/api.md`
- **Smoke-Test:** `docs/smoketest.md`
- **Patch-Notizen:** `docs/patches.md`

## Hinweis zu /word-clock

Der Ordner `/word-clock` ist aktuell nur ein historischer Platzhalter.
Der relevante Code liegt in `/main/`.
