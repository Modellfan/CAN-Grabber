# Implementierungs-Reihenfolge für AI-Agenten (Arduino + PlatformIO, ESP32-S3)

- Update immer AGENT.md und README.md, wenn der Nutzer definitionen macht, die eine Änderung bedeuten. Frage vorher um Erlaubnis.

## Phase 0 – Projektgerüst & Build-Sicherheit (Foundation)
- [x] PlatformIO Projekt aufsetzen
- [x] Arduino Framework, ESP32-S3 Board
- [x] Library-Management (SD, WiFi, Webserver, mDNS, OTA, JSON)
- [x] Build-Konfigurationen: debug + release
- [x] Modul-/Ordnerstruktur anlegen
- [x] `/hardware`, `/can`, `/logging`, `/storage`, `/web`, `/rest`, `/gvret`, `/net`, `/ota`, `/upload`, `/influx`, `/dbc`, `/monitoring`
- [x] Schnittstellen (Header) definieren, aber noch ohne Logik
- [x] Zentrale Hardware-Definitionsdatei
- [x] `hardware_config.h` (oder `.hpp`)
- [x] GPIOs, SPI/I2C, CAN-CS/INT, Termination Pins, SD, RTC
- [x] Keine Logik, nur Definitionen
- [x] Done wenn: Firmware kompiliert, startet, Version/Build-Info über Serial ausgibt.

## Phase 1 – Kern-Datenpfad: CAN RX → Puffer → SD (kritischster Teil)
- [ ] Konfigurationsmodell (in RAM) + Persistenz (NVS)
- [ ] Struktur: pro Bus: enabled, bitrate, read-only, termination, logging, name (user-defined override)
- [ ] global: max file size, wifi list, upload url, influx config, dbc selection
- [ ] Save/Load + Defaultwerte
- [ ] SD-Karte: Mount, Info, Verzeichnisse
- [ ] Mount + freie Kapazität lesen
- [ ] Basis-Ordner anlegen (`/can0` → `/can5`, `/meta`)
- [ ] Meta-Datei-Konzept vorbereiten (File-Status)
- [ ] CAN Treiber (MCP2515) für 1 Bus
- [ ] SPI init
- [ ] Bitrate setzen
- [x] RX lesen (Polling oder INT)
- [x] Frame struct definieren: timestamp, bus_id, id, ext, dlc, data[8], direction(RX/TX)
- [x] Ringpuffer/Queue pro Bus
- [ ] Thread-safe (FreeRTOS)
- [ ] Overflow-Zähler + High-water
- [ ] Unit-Test-artige Checks: push/pop, overflow
- [ ] Log Writer Task (1 Bus)
- [ ] Active-file Markierung (z. B. `.active`)
- [ ] Batch-Schreiben
- [ ] Max-File-Size Rotation
- [ ] Manuelles close (API)
- [ ] Parallelisierung auf 6 Busse
- [ ] Multi-bus RX (pro Bus Task oder zentral)
- [ ] pro Bus eigenes Logging-File + state machine
- [ ] Log-Dateinamen enthalten immer den Bus; Busname ist in der Konfiguration ueberschreibbar
- [ ] Done wenn: Bei hoher Frame-Rate werden Dateien pro Bus geschrieben, Rotationen funktionieren, keine Blocking-Probleme.

## Phase 2 – Datei-Management & Status (Download/Upload-Markierung, Überschreiben)
- [ ] Datei-Index & Metadaten
- [ ] `file_status.json` (oder binär) in `/meta`
- [ ] pro Datei: bus, name, size, start/end, downloaded, uploaded, checksum(optional)
- [ ] Download-Markierung
- [ ] API: mark_downloaded(file)
- [ ] Web/REST Download setzt Flag
- [ ] Speicherüberwachungs- und Überschreibstrategie
- [ ] Low-space threshold
- [ ] Löschen nach Priorität: uploaded/downloaded zuerst, dann älteste
- [ ] Schutz: keine active-files löschen
- [ ] Done wenn: SD wird automatisch "aufgeräumt", ohne aktive Dateien zu zerstören.

## Phase 3 – Watchdog für Ringpuffer-Überlauf (Stabilität)
- [ ] Buffer Watchdog
- [ ] Periodisch: fill level + overflow count check
- [ ] Warn-/Fehlerstatus setzen
- [ ] Optional: adaptive Maßnahmen (Upload/Web drosseln)
- [ ] Optional: Restart-Policy (konfigurierbar)
- [ ] Monitoring-API intern
- [ ] zentraler Statusblock: uptime, mode, wifi rssi, sd free, frames rx/tx, buffer stats, errors
- [ ] Done wenn: Überläufe werden sichtbar + zählen + optional reagieren.

## Phase 4 – Netzwerk-Basis: WLAN (3 SSIDs) + mDNS
- [ ] WiFi Manager
- [ ] 1–3 SSIDs mit Priorität
- [ ] Reconnect
- [ ] RSSI Prozent ableiten
- [ ] mDNS/Bonjour
- [ ] Hostname `canlogger.local`
- [ ] Service für HTTP
- [ ] Done wenn: Gerät verbindet sich robust und ist per `.local` erreichbar.

## Phase 5 – Web-Interface (UI) zuerst read-only, dann write
- [ ] Webserver Grundgerüst
- [ ] Statische Assets (OpenInverter-like) aus Flash
- [ ] Statusseite (read-only): SD, RSSI, Uhrzeit, counts, buffer stats
- [ ] Konfigurationsseiten
- [ ] CAN pro Bus: enable, bitrate, read-only, termination
- [ ] Logging: max file size, close file
- [ ] Zeit: set manual, can-time sync toggle
- [ ] WLAN: SSIDs
- [ ] Upload URL
- [ ] Dateiübersicht + Download
- [ ] Liste + Filter pro Bus
- [ ] Download einzelner Dateien
- [ ] Mark downloaded
- [ ] Done wenn: Alles konfigurierbar + Status vollständig.

## Phase 6 – REST API (Maschinenzugriff)
- [ ] REST Grundgerüst + Auth
- [ ] Basic oder Token
- [ ] Endpoints: Status & Config
- [ ] `/api/status`, `/api/config`, `/api/can/*`, `/api/storage/*`, `/api/buffers`
- [ ] Endpoints: Files
- [ ] `/api/files`, `/api/files/<id>/download`, `/api/files/<id>/mark_downloaded`
- [ ] Control Endpoints
- [ ] start/stop logging
- [ ] close active file
- [ ] Done wenn: Alles per REST steuerbar, Web nutzt intern gleiche APIs.

## Phase 7 – USB: GVRET (SavvyCAN)
- [ ] USB Serial Device stabil
- [ ] Device enumeration
- [ ] GVRET Parser/Encoder
- [ ] RX commands, TX frames
- [ ] Mapping auf Multi-CAN
- [ ] bus selection / channel mapping
- [ ] TX Logging
- [ ] Alle via GVRET gesendeten Frames werden geloggt
- [ ] Done wenn: SavvyCAN sieht das Device, RX/TX auf CAN funktioniert, TX erscheint im Log.

## Phase 8 – Zeit (RTC + CAN-Zeitsync)
- [ ] RTC Treiber
- [ ] read/set
- [ ] CAN-Zeitmessage
- [ ] Parser + Update RTC
- [ ] Zeitquelle abstrahieren
- [ ] RTC primär, fallback uptime
- [ ] UI/REST Integration
- [ ] set time, status time source
- [ ] Done wenn: Zeitstempel stabil und reproduzierbar.

## Phase 9 – Auto Upload per HTTP POST
- [ ] Upload Task
- [ ] Trigger: file closed, manual UI/REST
- [ ] HTTP POST Upload
- [ ] Datei + Metadaten
- [ ] Retry/Backoff
- [ ] Mark uploaded
- [ ] uploaded → bevorzugt löschbar
- [ ] Done wenn: Automatischer Upload robust funktioniert ohne Logging zu stören.

## Phase 10 – DBC JSON + InfluxDB Dump (Interpretation Pipeline)
- [ ] DBC-JSON Speicherung & Auswahl
- [ ] Upload per REST/Web
- [ ] Schema-Version prüfen
- [ ] DBC Decoder
- [ ] id/ext match, signals decode, byte order, signed, scaling
- [ ] Dump-Engine
- [ ] Logfile lesen → dekodieren → points erzeugen
- [ ] InfluxDB Writer
- [ ] Line protocol
- [ ] batching + retry
- [ ] Dump-Status + UI/REST
- [ ] progress, counters, errors
- [ ] Done wenn: Ein Logfile wird dekodiert und als Messwerte in InfluxDB geschrieben.

## Phase 11 – OTA
- [ ] OTA Framework integrieren
- [ ] Exklusivmodus
- [ ] stop logging, close files
- [ ] Update + Verify
- [ ] Status UI/REST
- [ ] Done wenn: OTA sicher läuft, ohne Konfig-/Log-Verlust.

## Debugging notes
- Use the full PlatformIO path with quotes in PowerShell: `& "C:\Users\Win11 Pro\.platformio\penv\Scripts\platformio.exe" ...`
- COM9 was missing; serial monitor should use COM10 at 115200.

## Querschnitt: Definition of Done (für jedes Issue)
- [ ] Kompiliert in PlatformIO (debug+release)
- [ ] Keine Blockierung des CAN-Loggings
- [ ] Web/REST zeigt Status korrekt
- [ ] Fehlerzustände werden sichtbar gemacht
- [ ] Minimaler Test: simulierte Last / künstliche Frames / SD voll / WLAN weg
