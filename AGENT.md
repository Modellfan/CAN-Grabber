# Implementierungs-Reihenfolge f√ºr AI-Agenten (Arduino + PlatformIO, ESP32-S3)

- Update immer AGENT.md und README.md, wenn der Nutzer definitionen macht, die eine √"nderung bedeuten. Frage vorher um Erlaubnis.

## Phase 0 ‚?" Projektger√ºst & Build-Sicherheit (Foundation)
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
- [x] Done wenn: Firmware kompiliert, startet, Version/Build-Info √ºber Serial ausgibt.

## Phase 1 ‚?" Kern-Datenpfad: CAN RX ‚?' Puffer ‚?' SD (kritischster Teil)
- [x] Konfigurationsmodell (in RAM) + Persistenz (NVS)
- [x] Struktur: pro Bus: enabled, bitrate, read-only, logging, name (user-defined override)
- [x] global: max file size, wifi list, upload url, influx config, dbc selection
- [x] Save/Load + Defaultwerte
- [x] SD-Karte: Mount, Info, Verzeichnisse
- [x] Mount + freie Kapazit√§t lesen
- [x] Basis-Ordner anlegen (`/can0` ‚?' `/can5`, `/meta`)
- [x] Meta-Datei-Konzept vorbereiten (File-Status)
- [x] CAN Treiber (MCP2515) f√ºr 1 Bus
- [x] SPI init
- [x] Bitrate setzen
- [x] RX lesen (Polling oder INT)
- [x] Frame struct definieren: timestamp, bus_id, id, ext, dlc, data[8], direction(RX/TX)
- [x] Blockpuffer (8192 B x 2 Bloecke) pro Bus
- [x] Thread-safe (FreeRTOS)
- [x] Overflow-Z√§hler + High-water
- [ ] Unit-Test-artige Checks: block buffer
- [x] Log Writer Task (1 Bus)
- [x] Batch-Schreiben
- [x] Max-File-Size Rotation
- [x] Manuelles close (API)
- [ ] Parallelisierung auf 6 Busse
- [ ] Multi-bus RX (pro Bus Task oder zentral)
- [x] pro Bus eigenes Logging-File + state machine
- [x] Log-Dateinamen enthalten immer den Bus; Busname ist in der Konfiguration ueberschreibbar
- [ ] Done wenn: Bei hoher Frame-Rate werden Dateien pro Bus geschrieben, Rotationen funktionieren, keine Blocking-Probleme.

## Phase 2 ‚?" Datei-Management & Status (Download/Upload-Markierung, √oberschreiben)
- [x] Datei-Index & Metadaten
- [x] `file_status.json` (oder bin√§r) in `/meta`
  - [x] pro Datei: bus, name, size, start/end, downloaded, uploaded, checksum(optional)
- [x] Download-Markierung
- [x] API: mark_downloaded(file)
  - [x] Web/REST Download setzt Flag
- [x] Speicher√ºberwachungs- und √oberschreibstrategie
  - [x] Low-space threshold
- [x] L√∂schen nach Priorit√§t: uploaded/downloaded zuerst, dann √§lteste
- [x] Schutz: keine active-files l√∂schen
- [ ] Done wenn: SD wird automatisch "aufger√§umt", ohne aktive Dateien zu zerst√∂ren.

## Phase 3 ‚?" Watchdog f√ºr Blockpuffer-√oberlauf (Stabilit√§t)
- [ ] Buffer Watchdog
- [ ] Periodisch: fill level + overflow count check
- [ ] Warn-/Fehlerstatus setzen
- [ ] Optional: adaptive Ma√Ynahmen (Upload/Web drosseln)
- [ ] Optional: Restart-Policy (konfigurierbar)
- [ ] Monitoring-API intern
- [ ] zentraler Statusblock: uptime, mode, wifi rssi, sd free, frames rx/tx, buffer stats, errors
- [ ] Done wenn: √oberl√§ufe werden sichtbar + z√§hlen + optional reagieren.

## Phase 4 ‚?" Netzwerk-Basis: WLAN (3 SSIDs) + mDNS
- [x] WiFi Manager
- [x] 1‚?"3 SSIDs mit Priorit√§t
- [x] Reconnect
- [x] RSSI Prozent ableiten
- [x] mDNS/Bonjour
- [x] Hostname `canlogger.local`
- [x] Service f√ºr HTTP
- [ ] Done wenn: Ger√§t verbindet sich robust und ist per `.local` erreichbar.

## Phase 5 ‚?" Web-Interface (UI) zuerst read-only, dann write
- [x] Webserver Grundgeruest
- [x] Statische Assets (OpenInverter-like) aus Flash
- [x] Statusseite (read-only): SD, RSSI, Uhrzeit, counts, buffer stats
- [x] Konfigurationsseiten
- [x] CAN pro Bus: enable, bitrate, read-only
- [x] Logging: max file size, close file
- [x] Zeit: set manual, can-time sync toggle
- [x] WLAN: SSIDs
- [x] Upload URL
- [x] Dateiuebersicht + Download
- [x] Liste + Filter pro Bus
- [x] Download einzelner Dateien
- [x] Mark downloaded
- [x] Done wenn: Alles konfigurierbar + Status vollstaendig.
## Phase 6 ‚?" REST API (Maschinenzugriff)
- [x] REST Grundger√ºst + Auth
- [x] Basic oder Token
- [x] Endpoints: Status & Config
- [x] `/api/status`, `/api/config`, `/api/can/*`, `/api/storage/*`, `/api/buffers`
- [x] Endpoints: Files
- [x] `/api/files`, `/api/files/<id>/download`, `/api/files/<id>/mark_downloaded`
- [x] Control Endpoints
- [x] start/stop logging
- [x] close active file
- [ ] Done wenn: Alles per REST steuerbar, Web nutzt intern gleiche APIs.

## Phase 7 ‚?" USB: GVRET (SavvyCAN)
- [ ] USB Serial Device stabil
- [ ] Device enumeration
- [ ] GVRET Parser/Encoder
- [ ] RX commands, TX frames
- [ ] Mapping auf Multi-CAN
- [ ] bus selection / channel mapping
- [ ] TX Logging
- [ ] Alle via GVRET gesendeten Frames werden geloggt
- [ ] Done wenn: SavvyCAN sieht das Device, RX/TX auf CAN funktioniert, TX erscheint im Log.

## Phase 8 ‚?" Zeit (RTC + CAN-Zeitsync)
- [ ] RTC Treiber
- [ ] read/set
- [ ] CAN-Zeitmessage
- [ ] Parser + Update RTC
- [ ] Zeitquelle abstrahieren
- [ ] RTC prim√§r, fallback uptime
- [ ] UI/REST Integration
- [ ] set time, status time source
- [ ] Done wenn: Zeitstempel stabil und reproduzierbar.

## Phase 9 ‚?" Auto Upload per HTTP POST
- [ ] Upload Task
- [ ] Trigger: file closed, manual UI/REST
- [ ] HTTP POST Upload
- [ ] Datei + Metadaten
- [ ] Retry/Backoff
- [ ] Mark uploaded
- [ ] uploaded ‚?' bevorzugt l√∂schbar
- [ ] Done wenn: Automatischer Upload robust funktioniert ohne Logging zu st√∂ren.

## Phase 10 ‚?" DBC JSON + InfluxDB Dump (Interpretation Pipeline)
- [x] DBC-JSON Speicherung & Auswahl
- [ ] Upload per REST/Web
- [ ] Schema-Version pr√ºfen
- [ ] DBC Decoder
- [ ] id/ext match, signals decode, byte order, signed, scaling
- [ ] Dump-Engine
- [ ] Logfile lesen ‚?' dekodieren ‚?' points erzeugen
- [ ] InfluxDB Writer
- [ ] Line protocol
- [ ] batching + retry
- [ ] Dump-Status + UI/REST
- [ ] progress, counters, errors
- [ ] Done wenn: Ein Logfile wird dekodiert und als Messwerte in InfluxDB geschrieben.

## Phase 11 ‚?" OTA
- [ ] OTA Framework integrieren
- [ ] Exklusivmodus
- [ ] stop logging, close files
- [ ] Update + Verify
- [ ] Status UI/REST
- [ ] Done wenn: OTA sicher l√§uft, ohne Konfig-/Log-Verlust.

## Debugging notes
- Use the full PlatformIO path with quotes in PowerShell: `& "C:\Users\Win11 Pro\.platformio\penv\Scripts\platformio.exe" ...`
- COM9 was missing; serial monitor should use COM10 at 115200.

## Querschnitt: Definition of Done (f√ºr jedes Issue)
- [ ] Kompiliert in PlatformIO (debug+release)
- [ ] Keine Blockierung des CAN-Loggings
- [ ] Web/REST zeigt Status korrekt
- [ ] Fehlerzust√§nde werden sichtbar gemacht
- [ ] Minimaler Test: simulierte Last / k√ºnstliche Frames / SD voll / WLAN weg



