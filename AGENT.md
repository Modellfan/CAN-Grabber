# Implementierungs-Reihenfolge fÃ¼r AI-Agenten (Arduino + PlatformIO, ESP32-S3)

- Update immer AGENT.md und README.md, wenn der Nutzer definitionen macht, die eine Ã„nderung bedeuten. Frage vorher um Erlaubnis.

## Phase 0 â€“ ProjektgerÃ¼st & Build-Sicherheit (Foundation)
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
- [x] Done wenn: Firmware kompiliert, startet, Version/Build-Info Ã¼ber Serial ausgibt.

## Phase 1 â€“ Kern-Datenpfad: CAN RX â†’ Puffer â†’ SD (kritischster Teil)
- [x] Konfigurationsmodell (in RAM) + Persistenz (NVS)
- [x] Struktur: pro Bus: enabled, bitrate, read-only, termination, logging, name (user-defined override)
- [x] global: max file size, wifi list, upload url, influx config, dbc selection
- [x] Save/Load + Defaultwerte
- [x] SD-Karte: Mount, Info, Verzeichnisse
- [x] Mount + freie KapazitÃ¤t lesen
- [x] Basis-Ordner anlegen (`/can0` â†’ `/can5`, `/meta`)
- [x] Meta-Datei-Konzept vorbereiten (File-Status)
- [x] CAN Treiber (MCP2515) fÃ¼r 1 Bus
- [x] SPI init
- [x] Bitrate setzen
- [x] RX lesen (Polling oder INT)
- [x] Frame struct definieren: timestamp, bus_id, id, ext, dlc, data[8], direction(RX/TX)
- [x] Blockpuffer (8192 B x 2 Bloecke) pro Bus
- [x] Thread-safe (FreeRTOS)
- [x] Overflow-ZÃ¤hler + High-water
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

## Phase 2 â€“ Datei-Management & Status (Download/Upload-Markierung, Ãœberschreiben)
- [x] Datei-Index & Metadaten
- [x] `file_status.json` (oder binÃ¤r) in `/meta`
  - [x] pro Datei: bus, name, size, start/end, downloaded, uploaded, checksum(optional)
- [x] Download-Markierung
- [x] API: mark_downloaded(file)
  - [x] Web/REST Download setzt Flag
- [x] SpeicherÃ¼berwachungs- und Ãœberschreibstrategie
  - [x] Low-space threshold
- [x] LÃ¶schen nach PrioritÃ¤t: uploaded/downloaded zuerst, dann Ã¤lteste
- [x] Schutz: keine active-files lÃ¶schen
- [ ] Done wenn: SD wird automatisch "aufgerÃ¤umt", ohne aktive Dateien zu zerstÃ¶ren.

## Phase 3 â€“ Watchdog fÃ¼r Blockpuffer-Ãœberlauf (StabilitÃ¤t)
- [ ] Buffer Watchdog
- [ ] Periodisch: fill level + overflow count check
- [ ] Warn-/Fehlerstatus setzen
- [ ] Optional: adaptive MaÃŸnahmen (Upload/Web drosseln)
- [ ] Optional: Restart-Policy (konfigurierbar)
- [ ] Monitoring-API intern
- [ ] zentraler Statusblock: uptime, mode, wifi rssi, sd free, frames rx/tx, buffer stats, errors
- [ ] Done wenn: ÃœberlÃ¤ufe werden sichtbar + zÃ¤hlen + optional reagieren.

## Phase 4 â€“ Netzwerk-Basis: WLAN (3 SSIDs) + mDNS
- [x] WiFi Manager
- [x] 1â€“3 SSIDs mit PrioritÃ¤t
- [x] Reconnect
- [x] RSSI Prozent ableiten
- [x] mDNS/Bonjour
- [x] Hostname `canlogger.local`
- [x] Service fÃ¼r HTTP
- [ ] Done wenn: GerÃ¤t verbindet sich robust und ist per `.local` erreichbar.

## Phase 5 â€“ Web-Interface (UI) zuerst read-only, dann write
- [ ] Webserver GrundgerÃ¼st
- [ ] Statische Assets (OpenInverter-like) aus Flash
- [ ] Statusseite (read-only): SD, RSSI, Uhrzeit, counts, buffer stats
- [ ] Konfigurationsseiten
- [ ] CAN pro Bus: enable, bitrate, read-only, termination
- [ ] Logging: max file size, close file
- [ ] Zeit: set manual, can-time sync toggle
- [ ] WLAN: SSIDs
- [ ] Upload URL
- [ ] DateiÃ¼bersicht + Download
- [ ] Liste + Filter pro Bus
- [ ] Download einzelner Dateien
- [ ] Mark downloaded
- [ ] Done wenn: Alles konfigurierbar + Status vollstÃ¤ndig.

## Phase 6 â€“ REST API (Maschinenzugriff)
- [x] REST GrundgerÃ¼st + Auth
- [x] Basic oder Token
- [x] Endpoints: Status & Config
- [x] `/api/status`, `/api/config`, `/api/can/*`, `/api/storage/*`, `/api/buffers`
- [x] Endpoints: Files
- [x] `/api/files`, `/api/files/<id>/download`, `/api/files/<id>/mark_downloaded`
- [x] Control Endpoints
- [x] start/stop logging
- [x] close active file
- [ ] Done wenn: Alles per REST steuerbar, Web nutzt intern gleiche APIs.

## Phase 7 â€“ USB: GVRET (SavvyCAN)
- [ ] USB Serial Device stabil
- [ ] Device enumeration
- [ ] GVRET Parser/Encoder
- [ ] RX commands, TX frames
- [ ] Mapping auf Multi-CAN
- [ ] bus selection / channel mapping
- [ ] TX Logging
- [ ] Alle via GVRET gesendeten Frames werden geloggt
- [ ] Done wenn: SavvyCAN sieht das Device, RX/TX auf CAN funktioniert, TX erscheint im Log.

## Phase 8 â€“ Zeit (RTC + CAN-Zeitsync)
- [ ] RTC Treiber
- [ ] read/set
- [ ] CAN-Zeitmessage
- [ ] Parser + Update RTC
- [ ] Zeitquelle abstrahieren
- [ ] RTC primÃ¤r, fallback uptime
- [ ] UI/REST Integration
- [ ] set time, status time source
- [ ] Done wenn: Zeitstempel stabil und reproduzierbar.

## Phase 9 â€“ Auto Upload per HTTP POST
- [ ] Upload Task
- [ ] Trigger: file closed, manual UI/REST
- [ ] HTTP POST Upload
- [ ] Datei + Metadaten
- [ ] Retry/Backoff
- [ ] Mark uploaded
- [ ] uploaded â†’ bevorzugt lÃ¶schbar
- [ ] Done wenn: Automatischer Upload robust funktioniert ohne Logging zu stÃ¶ren.

## Phase 10 â€“ DBC JSON + InfluxDB Dump (Interpretation Pipeline)
- [x] DBC-JSON Speicherung & Auswahl
- [ ] Upload per REST/Web
- [ ] Schema-Version prÃ¼fen
- [ ] DBC Decoder
- [ ] id/ext match, signals decode, byte order, signed, scaling
- [ ] Dump-Engine
- [ ] Logfile lesen â†’ dekodieren â†’ points erzeugen
- [ ] InfluxDB Writer
- [ ] Line protocol
- [ ] batching + retry
- [ ] Dump-Status + UI/REST
- [ ] progress, counters, errors
- [ ] Done wenn: Ein Logfile wird dekodiert und als Messwerte in InfluxDB geschrieben.

## Phase 11 â€“ OTA
- [ ] OTA Framework integrieren
- [ ] Exklusivmodus
- [ ] stop logging, close files
- [ ] Update + Verify
- [ ] Status UI/REST
- [ ] Done wenn: OTA sicher lÃ¤uft, ohne Konfig-/Log-Verlust.

## Debugging notes
- Use the full PlatformIO path with quotes in PowerShell: `& "C:\Users\Win11 Pro\.platformio\penv\Scripts\platformio.exe" ...`
- COM9 was missing; serial monitor should use COM10 at 115200.

## Querschnitt: Definition of Done (fÃ¼r jedes Issue)
- [ ] Kompiliert in PlatformIO (debug+release)
- [ ] Keine Blockierung des CAN-Loggings
- [ ] Web/REST zeigt Status korrekt
- [ ] FehlerzustÃ¤nde werden sichtbar gemacht
- [ ] Minimaler Test: simulierte Last / kÃ¼nstliche Frames / SD voll / WLAN weg



