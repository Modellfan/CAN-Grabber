# CAN-Grabber

Funktionale Beschreibung und Anforderungen.

## Inhaltsverzeichnis
- [1. Ziel des Systems](#1-ziel-des-systems)
- [2. Begriffe und Abkürzungen](#2-begriffe-und-abkurzungen)
- [3. Systemübersicht](#3-systemubersicht)
  - [3.1 Gesamtarchitektur](#31-gesamtarchitektur)
  - [3.2 Hauptfunktionen](#32-hauptfunktionen)
  - [3.3 Betriebsmodi](#33-betriebsmodi)
- [4. Hardware](#4-hardware)
  - [4.1 Zentrale Recheneinheit](#41-zentrale-recheneinheit)
  - [4.2 CAN-Bus-Schnittstellen](#42-can-bus-schnittstellen)
  - [4.3 CAN-Transceiver und Isolation](#43-can-transceiver-und-isolation)
  - [4.4 Abschlusswiderstände (CAN-Termination)](#44-abschlusswiderstande-can-termination)
  - [4.5 Echtzeituhr (RTC)](#45-echtzeituhr-rtc)
  - [4.6 Massenspeicher (MicroSD)](#46-massenspeicher-microsd)
  - [4.7 USB-Schnittstelle](#47-usb-schnittstelle)
- [5. Hardware-Abstraktion](#5-hardware-abstraktion)
  - [5.1 Zentrale Hardware-Definitionsdatei](#51-zentrale-hardware-definitionsdatei)
  - [5.2 Inhalt der Hardware-Datei](#52-inhalt-der-hardware-datei)
  - [5.3 Trennung von Logik und Hardware](#53-trennung-von-logik-und-hardware)
  - [5.4 Erweiterbarkeit](#54-erweiterbarkeit)
- [6. Firmware-Grundlagen](#6-firmware-grundlagen)
  - [6.1 Plattform und Framework](#61-plattform-und-framework)
  - [6.2 Task- und Prozessmodell](#62-task--und-prozessmodell)
  - [6.2.1 Haupttasks](#621-haupttasks)
  - [6.3 Task-Priorisierung](#63-task-priorisierung)
  - [6.4 Interne Kommunikation](#64-interne-kommunikation)
  - [6.5 Zeitstempelung](#65-zeitstempelung)
  - [6.6 Persistente Konfiguration](#66-persistente-konfiguration)
- [7. CAN-Bus-Funktionalität](#7-can-bus-funktionalitat)
  - [7.1 Allgemeines](#71-allgemeines)
  - [7.2 CAN-Bus-Konfiguration](#72-can-bus-konfiguration)
  - [7.3 Bitraten](#73-bitraten)
  - [7.4 Empfang von CAN-Nachrichten](#74-empfang-von-can-nachrichten)
  - [7.5 Senden von CAN-Nachrichten](#75-senden-von-can-nachrichten)
  - [7.6 Zeit-Synchronisation über CAN](#76-zeit-synchronisation-uber-can)
- [8. Datenlogging](#8-datenlogging)
  - [8.1 Logging-Grundprinzip](#81-logging-grundprinzip)
  - [8.2 Log-Dateistruktur](#82-log-dateistruktur)
  - [8.3 Aktive Log-Dateien](#83-aktive-log-dateien)
  - [8.4 Dateirotation](#84-dateirotation)
  - [8.5 Download- und Upload-Status](#85-download--und-upload-status)
  - [8.6 Logging gesendeter Nachrichten](#86-logging-gesendeter-nachrichten)
  - [8.7 Fehlerbehandlung beim Logging](#87-fehlerbehandlung-beim-logging)
- [9. Speicherverwaltung](#9-speicherverwaltung)
  - [9.1 SD-Karten-Unterstützung](#91-sd-karten-unterstutzung)
  - [9.2 Initialisierung und Verfügbarkeit](#92-initialisierung-und-verfugbarkeit)
  - [9.3 Speicherüberwachung](#93-speicheruberwachung)
  - [9.4 Überschreibstrategie](#94-uberschreibstrategie)
  - [9.5 Konsistenz und Datensicherheit](#95-konsistenz-und-datensicherheit)
  - [9.6 Fehlerbehandlung](#96-fehlerbehandlung)
- [10. USB-CAN-Interface](#10-usb-can-interface)
  - [10.1 Allgemeines](#101-allgemeines)
  - [10.2 Protokoll: GVRET](#102-protokoll-gvret)
  - [10.3 Mehrkanal-Unterstützung](#103-mehrkanal-unterstutzung)
  - [10.4 Empfang über USB](#104-empfang-uber-usb)
  - [10.5 Logging über USB gesendeter Frames](#105-logging-uber-usb-gesendeter-frames)
  - [10.6 Fehlerbehandlung USB](#106-fehlerbehandlung-usb)
- [11. Netzwerk & Konnektivität](#11-netzwerk--konnektivitat)
  - [11.1 WLAN-Grundlagen](#111-wlan-grundlagen)
  - [11.2 Mehrere WLAN-Netzwerke](#112-mehrere-wlan-netzwerke)
  - [11.3 Verbindungslogik](#113-verbindungslogik)
  - [11.4 mDNS / Bonjour](#114-mdns--bonjour)
  - [11.5 Netzwerkstatus](#115-netzwerkstatus)
  - [11.6 Robustheit](#116-robustheit)
- [12. Web-Interface](#12-web-interface)
  - [12.1 Allgemeine Anforderungen](#121-allgemeine-anforderungen)
  - [12.2 Design und Struktur](#122-design-und-struktur)
  - [12.3 Konfigurationsfunktionen](#123-konfigurationsfunktionen)
  - [12.4 Status- und Übersichtsseiten](#124-status--und-ubersichtsseiten)
  - [12.5 Dateiverwaltung](#125-dateiverwaltung)
- [13. REST-Schnittstelle](#13-rest-schnittstelle)
  - [13.1 Allgemeines](#131-allgemeines)
  - [13.2 Authentifizierung](#132-authentifizierung)
  - [13.3 REST-Funktionsumfang](#133-rest-funktionsumfang)
  - [13.4 Dateiverwaltung über REST](#134-dateiverwaltung-uber-rest)
  - [13.5 Fehlerbehandlung](#135-fehlerbehandlung)
  - [13.6 DBC-JSON (Signalbeschreibung)](#136-dbc-json-signalbeschreibung)
    - [13.6.1 Ziel](#1361-ziel)
    - [13.6.2 Quelle und Speicherung](#1362-quelle-und-speicherung)
    - [13.6.3 Mindest-Schema (normativ)](#1363-mindest-schema-normativ)
    - [13.6.4 Interpretation](#1364-interpretation)
    - [13.6.5 Versionierung](#1365-versionierung)
  - [13.7 REST/Web Ergänzungen (kurz)](#137-restweb-erganzungen-kurz)
- [14. Automatischer Daten-Upload](#14-automatischer-daten-upload)
  - [14.1 Allgemeines](#141-allgemeines)
  - [14.2 Upload-Trigger](#142-upload-trigger)
  - [14.3 Upload-Inhalt](#143-upload-inhalt)
  - [14.4 Upload-Status](#144-upload-status)
  - [14.5 Fehlerbehandlung und Wiederholung](#145-fehlerbehandlung-und-wiederholung)
  - [14.6 InfluxDB Dump (Interpretation + Upload)](#146-influxdb-dump-interpretation--upload)
    - [14.6.1 Ziel](#1461-ziel)
    - [14.6.2 Konfiguration](#1462-konfiguration)
    - [14.6.3 Datenquelle](#1463-datenquelle)
    - [14.6.4 Interpretationspipeline](#1464-interpretationspipeline)
    - [14.6.5 Upload-Format (InfluxDB)](#1465-upload-format-influxdb)
    - [14.6.6 Status und Rückmeldung](#1466-status-und-ruckmeldung)
    - [14.6.7 Fehlerbehandlung](#1467-fehlerbehandlung)
- [15. OTA-Firmware-Update](#15-ota-firmware-update)
  - [15.1 Allgemeines](#151-allgemeines)
  - [15.2 OTA-Aktivierung](#152-ota-aktivierung)
  - [15.3 Verhalten während OTA](#153-verhalten-wahrend-ota)
  - [15.4 Sicherheit und Integrität](#154-sicherheit-und-integritat)
  - [15.5 Statusanzeige](#155-statusanzeige)
- [16. Zeitmanagement](#16-zeitmanagement)
  - [16.1 Zeitquellen](#161-zeitquellen)
  - [16.2 Manuelle Zeitsetzung](#162-manuelle-zeitsetzung)
  - [16.3 Zeit-Synchronisation über CAN](#163-zeit-synchronisation-uber-can)
  - [16.4 Zeitstempel-Qualität](#164-zeitstempel-qualitat)
- [17. Systemstatus & Monitoring](#17-systemstatus--monitoring)
  - [17.1 Laufzeitstatus](#171-laufzeitstatus)
  - [17.2 CAN-Statistiken](#172-can-statistiken)
  - [17.3 Netzwerk- und Systemparameter](#173-netzwerk--und-systemparameter)
  - [17.4 Systemgesundheit](#174-systemgesundheit)
- [18. Fehler- und Ausnahmebehandlung](#18-fehler--und-ausnahmebehandlung)
  - [18.1 CAN-Fehler](#181-can-fehler)
  - [18.2 Speicherfehler](#182-speicherfehler)
  - [18.3 Netzwerkfehler](#183-netzwerkfehler)
  - [18.4 Wiederanlaufstrategien](#184-wiederanlaufstrategien)
  - [18.5 Watchdog für Ringpuffer-Überlauf](#185-watchdog-fur-ringpuffer-uberlauf)
    - [18.5.1 Ziel](#1851-ziel)
    - [18.5.2 Überwachte Komponenten](#1852-uberwachte-komponenten)
    - [18.5.3 Metriken](#1853-metriken)
    - [18.5.4 Verhalten bei drohendem Überlauf](#1854-verhalten-bei-drohendem-uberlauf)
    - [18.5.5 Verhalten bei tatsächlichem Overflow](#1855-verhalten-bei-tatsachlichem-overflow)
    - [18.5.6 Testkriterien](#1856-testkriterien)
    - [18.5.7 SD Speed Test (Development)](#1857-sd-speed-test-development)
    - [18.5.8 RX Load Test (Development)](#1858-rx-load-test-development)
- [19. Sicherheit](#19-sicherheit)
  - [19.1 Zugriffsschutz](#191-zugriffsschutz)
  - [19.2 Netzwerk-Sicherheit](#192-netzwerk-sicherheit)
  - [19.3 Firmware-Integrität](#193-firmware-integritat)
- [20. Erweiterbarkeit](#20-erweiterbarkeit)
  - [20.1 Hardware-Erweiterungen](#201-hardware-erweiterungen)
  - [20.2 Software-Erweiterungen](#202-software-erweiterungen)
  - [20.3 Log-Formate](#203-log-formate)
- [21. Nicht-Ziele / Abgrenzungen](#21-nicht-ziele--abgrenzungen)
- [22. Anhang](#22-anhang)
  - [22.1 Konfigurationsparameter (Beispiele)](#221-konfigurationsparameter-beispiele)
  - [22.2 Beispiel-Dateistruktur](#222-beispiel-dateistruktur)
  - [22.3 Referenzen](#223-referenzen)

## 1. Ziel des Systems

Ziel dieses Projekts ist die Entwicklung eines modularen, mehrkanaligen CAN-Bus-Datenloggers für den Einsatz im Fahrzeugumfeld.

Das System soll:
- bis zu sechs unabhängige CAN-Busse parallel erfassen,
- CAN-Daten zuverlässig und zeitgestempelt auf einer SD-Karte speichern,
- eine Konfiguration und Überwachung über ein Web-Interface ermöglichen,
- CAN-Daten über USB (GVRET-kompatibel) an einen PC weiterleiten,
- sowie eine automatisierte Weiterverarbeitung der Daten über Netzwerkfunktionen (REST, HTTP Upload) erlauben.

Die Funktionsbeschreibung ist so formuliert, dass sie:
- eindeutig,
- testbar,
- und automatisiert implementierbar (z. B. durch AI-Agenten)
ist.

## 2. Begriffe und Abkürzungen

| Begriff | Beschreibung |
| --- | --- |
| CAN | Controller Area Network |
| ESP32-S3 | Mikrocontroller von Espressif mit WLAN, USB und FreeRTOS |
| MCP2515 | Externer CAN-Controller (SPI) |
| CAN-Transceiver | Physikalische CAN-Schnittstelle |
| RTC | Real-Time Clock |
| SD | Secure Digital Speicherkarte |
| GVRET | CAN-over-Serial-Protokoll (SavvyCAN kompatibel) |
| OTA | Over-the-Air Firmware Update |
| REST | HTTP-basierte Programmierschnittstelle |
| mDNS / Bonjour | Netzwerkdienst zur Namensauflösung |
| Bitrate | CAN-Bus-Geschwindigkeit (z. B. 500 kbit/s) |

## 3. Systemübersicht

### 3.1 Gesamtarchitektur

Das System besteht aus folgenden Hauptkomponenten:
- ESP32-S3 als zentrale Recheneinheit
- Bis zu 6 CAN-Bus-Module, bestehend aus:
  - MCP2515 (CAN-Controller)
  - Externem CAN-Transceiver (optional galvanisch isoliert)
- RTC- und MicroSD-Erweiterung zur Zeitstempelung und Datenspeicherung
- USB-Schnittstelle für GVRET / SavvyCAN
- WLAN-Schnittstelle für Web-Interface, REST, Upload und OTA

Alle Komponenten werden durch eine einheitliche Firmware gesteuert, die auf der Arduino-Plattform unter PlatformIO basiert und FreeRTOS nutzt.

### 3.2 Hauptfunktionen

Das System stellt folgende Hauptfunktionen bereit:
- Gleichzeitiges Erfassen mehrerer CAN-Busse
- Konfigurierbare CAN-Parameter pro Bus
- Zeitgestempeltes Datenlogging auf SD-Karte
- Dateirotation und Speicherverwaltung
- USB-CAN-Interface (GVRET)
- Web-Interface für Konfiguration und Status
- REST-Schnittstelle für Automatisierung
- OTA-Firmware-Updates
- Automatischer Upload von Log-Dateien

### 3.3 Betriebsmodi

Das System kennt folgende logische Betriebsmodi:
- Idle
  - CAN-Busse konfiguriert, aber kein aktives Logging
- Logging aktiv
  - CAN-Daten werden empfangen und gespeichert
- Logging gestoppt
  - Dateien geschlossen, System weiterhin erreichbar
- Upload aktiv
  - Log-Dateien werden über HTTP übertragen
- OTA-Modus
  - Logging deaktiviert, Firmware-Update aktiv
- Fehlerzustand
  - Kritische Fehler (z. B. SD-Karte nicht verfügbar)

## 4. Hardware

### 4.1 Zentrale Recheneinheit

Mikrocontroller: ESP32-S3

Anforderungen:
- WLAN-Funktionalität
- USB Device Unterstützung
- FreeRTOS-Unterstützung
- Ausreichender RAM für Pufferung mehrerer CAN-Busse

### 4.2 CAN-Bus-Schnittstellen

Anzahl: 1 bis 6 CAN-Busse

Pro CAN-Bus:
- Ein MCP2515 CAN-Controller
- Anbindung über SPI

Anforderungen:
- Unabhängige Konfiguration pro Bus
- Gleichzeitiger Betrieb aller Busse
- Unterstützung hoher Buslasten

### 4.3 CAN-Transceiver und Isolation

Externe CAN-Transceiver-Module:
- Austauschbar
- Bevorzugt als fertige Module (z. B. von AliExpress)

Optionale galvanische Trennung:
- Unterstützung isolierter CAN-Module (z. B. WE-ACT)

Die Firmware darf:
- Keine Annahmen über konkrete Transceiver-Typen treffen
- Nur logische CAN-Schnittstellen abstrahieren

### 4.4 Abschlusswiderstände (CAN-Termination)

Pro CAN-Bus muss ein 120-Ohm-Abschlusswiderstand vorhanden sein.

Anforderungen:
- Softwareseitig schaltbar (Ein / Aus)
- Steuerung über GPIO
- Konfiguration über Web-Interface und REST
- Zustand persistent speicherbar

### 4.5 Echtzeituhr (RTC)

Externe RTC über Erweiterungsmodul.

Anforderungen:
- Zeitstempel für alle Log-Daten
- Batteriegepuffert
- Manuell setzbar
- Synchronisierbar über CAN-Nachrichten

### 4.6 Massenspeicher (MicroSD)

Speichermedium:
- MicroSD-Karte

Anforderungen:
- Unterstützung von Karten ? 64 GB
- FAT-kompatibles Dateisystem
- Dauerbetrieb bei hoher Schreiblast

Die SD-Karte dient ausschließlich:
- Dem Speichern von Log-Dateien
- Temporären Metadaten

### 4.7 USB-Schnittstelle

USB Device Mode des ESP32-S3.

Verwendung als:
- Virtueller COM-Port

Protokoll:
- GVRET (SavvyCAN kompatibel)

## 5. Hardware-Abstraktion

### 5.1 Zentrale Hardware-Definitionsdatei

Alle hardwareabhängigen Definitionen müssen:
- In einer zentralen Datei zusammengefasst sein

Diese Datei enthält ausschließlich:
- GPIO-Zuordnungen
- Bus-Zuordnungen
- Hardware-Konstanten

### 5.2 Inhalt der Hardware-Datei

Beispiele für zu definierende Parameter:
- SPI-Bus-Konfiguration
- Chip-Select-Pins der MCP2515
- Interrupt-Pins der CAN-Controller
- GPIOs für Terminierungswiderstände
- SD-Karten-Pins
- RTC-Pins
- USB-Konfiguration (falls notwendig)

### 5.3 Trennung von Logik und Hardware

Die Hardware-Definitionsdatei darf:
- Keine Logik enthalten
- Keine Zustände verwalten

Ziel:
- Austauschbarkeit der Hardware
- Einfache Anpassung an neue Board-Revisionen
- Klare Trennung zwischen Firmware-Logik und physischer Plattform

### 5.4 Erweiterbarkeit

Neue CAN-Busse oder Hardware-Revisionen sollen:
- Durch Anpassung der Hardware-Definitionsdatei integrierbar sein
- Keine Änderungen an der Applikationslogik erfordern

## 6. Firmware-Grundlagen

### 6.1 Plattform und Framework

Zielplattform:
- ESP32-S3

Entwicklungsumgebung:
- Arduino Framework
- PlatformIO

Betriebssystem:
- FreeRTOS (integriert im Arduino-ESP32-Core)

### 6.2 Task- und Prozessmodell

Die Firmware verwendet ein taskbasiertes Modell auf Basis von FreeRTOS, um zeitkritische Funktionen (CAN-Empfang, Logging) von weniger kritischen Funktionen (Web, Upload, OTA) zu trennen.

### 6.2.1 Haupttasks

| Task | Aufgabe |
| --- | --- |
| CAN RX Task | Empfang von CAN-Frames von MCP2515 |
| CAN TX Task | Senden von CAN-Frames |
| Log Writer Task | Schreiben der Log-Daten auf SD |
| GVRET Task | USB-CAN (GVRET) Protokoll |
| Network Manager Task | WLAN, mDNS, Reconnect |
| Web / REST Task | Web-Interface & REST |
| Upload Task | HTTP POST Upload |
| OTA Task | Firmware-Update |

### 6.3 Task-Priorisierung

- CAN RX Tasks: höchste Priorität
- CAN TX Tasks: hohe Priorität
- Log Writer Task: mittelhohe Priorität
- GVRET Task: mittlere Priorität
- Netzwerk, Web, Upload, OTA: niedrigere Priorität

Zeitkritische CAN-Funktionen dürfen nicht durch Netzwerk- oder Dateisystemzugriffe blockiert werden.

- Core assignment (current firmware): CAN RX tasks pinned to core 0, Log Writer task pinned to core 1.

### 6.4 Interne Kommunikation

Tasks kommunizieren ausschließlich über:
- FreeRTOS Queues
- Ringbuffer
- Event-Gruppen

Direkte Funktionsaufrufe zwischen Tasks sind zu vermeiden.

CAN RX Tasks schreiben empfangene Frames in:
- Pro-Bus-Ringbuffer oder Queue

Der Log Writer Task liest aus diesen Puffern.

### 6.5 Zeitstempelung

Jeder CAN-Frame erhält unmittelbar beim Empfang:
- Einen Zeitstempel basierend auf RTC
- Bei nicht verfügbarer RTC: Fallback auf Systemzeit (Millis seit Start)

Zeitquelle ist zentral abstrahiert.

### 6.6 Persistente Konfiguration

Konfigurationsdaten werden:
- Persistent im internen Flash (z. B. NVS) gespeichert

Persistente Daten umfassen:
- CAN-Konfiguration pro Bus
- WLAN-Zugangsdaten
- Logging-Parameter
- Upload- und REST-Konfiguration

## 7. CAN-Bus-Funktionalität

### 7.1 Allgemeines

Das System unterstützt:
- Bis zu 6 unabhängige CAN-Busse

Jeder CAN-Bus ist:
- Logisch vollständig getrennt
- Unabhängig konfigurierbar

### 7.2 CAN-Bus-Konfiguration

Für jeden CAN-Bus sind folgende Parameter konfigurierbar:
- Aktiviert / Deaktiviert
- CAN-Bitrate
- Abschlusswiderstand: Ein / Aus
- Betriebsmodus: Read-Only, Senden erlaubt
- Busname (user-defined, ueberschreibt Standardnamen wie "can0")

Alle Parameter müssen:
- Über Web-Interface und REST setzbar sein
- Persistent gespeichert werden

### 7.3 Bitraten

Unterstützte CAN-Bitraten (mindestens):
- 125 kbit/s
- 250 kbit/s
- 500 kbit/s
- 1 Mbit/s

Erweiterbarkeit für weitere Bitraten muss vorgesehen sein.

### 7.4 Empfang von CAN-Nachrichten

CAN-Nachrichten werden:
- Über MCP2515 empfangen
- Per SPI ausgelesen

Empfang erfolgt:
- Interrupt-basiert oder Polling

Empfangene Frames müssen:
- Zeitgestempelt
- Unverändert weiterverarbeitet werden

### 7.5 Senden von CAN-Nachrichten

CAN-Nachrichten können gesendet werden durch:
- GVRET (USB)
- Web / REST

Senden ist nur erlaubt, wenn:
- Der jeweilige CAN-Bus nicht als Read-Only konfiguriert ist

Gesendete Frames müssen:
- Zeitgestempelt
- Geloggt werden

### 7.6 Zeit-Synchronisation über CAN

Das System unterstützt:
- Eine vordefinierte CAN-Nachricht zur Zeitübertragung

Anforderungen:
- Empfang auf allen CAN-Bussen
- Aktualisierung der RTC
- Aktivierbar / deaktivierbar

## 8. Datenlogging

### 8.1 Logging-Grundprinzip

CAN-Daten werden:
- Pro CAN-Bus getrennt geloggt

Logging erfolgt:
- Parallel für alle aktivierten CAN-Busse

Logging darf:
- Keine CAN-Frames verlieren
- Nicht durch SD-Zugriffe blockieren

### 8.2 Log-Dateistruktur

Pro CAN-Bus existiert:
- Eine eigene Log-Datei pro aktivem Bus (SavvyCAN ASCII)

Dateinamen folgen dem Muster:
- `/log_<millis>_bus<N>.sav`
- `<millis>` ist die Startzeit in Millisekunden seit Boot
- `<N>` ist der 1-basierte Bus-Index (bus 1, bus 2, ...)

Log-Inhalt (eine Zeile pro Frame):
- `<sec>.<usec6> <bus>R11|R29 <8hexID> <b0> ... <b7>`
- `bus` ist 1-basiert
- ID ist null-auf-8-hex gepaddet
- Datenbytes sind immer 8 Felder (fehlende Bytes werden als `00` gepaddet)

Optional:
- Header-Zeilen beginnen mit `#` und werden von SavvyCAN ignoriert
### 8.3 Aktive Log-Dateien

Pro CAN-Bus darf:
- Maximal eine aktive Log-Datei existieren

Aktive Dateien:
- Werden fortlaufend beschrieben
- Sind eindeutig als "nicht abgeschlossen" gekennzeichnet

### 8.4 Dateirotation

Maximale Dateigröße ist:
- Konfigurierbar

Bei Erreichen der Größe:
- Aktive Datei wird geschlossen
- Neue Datei wird erzeugt

Zusätzlich:
- Manuelles Schließen über Web / REST

### 8.5 Download- und Upload-Status

Jede Log-Datei besitzt Statusinformationen:
- Nicht heruntergeladen
- Heruntergeladen
- Hochgeladen

Statusinformationen müssen:
- Persistent gespeichert werden

Beim Überschreiben von Dateien:
- Bereits heruntergeladene / hochgeladene Dateien haben Priorität

### 8.6 Logging gesendeter Nachrichten

CAN-Nachrichten, die:
- Über USB (GVRET)
- Über Web / REST
- Vom System selbst

gesendet werden, müssen:
- Wie empfangene Nachrichten geloggt werden
- Mit korrektem Zeitstempel versehen sein

### 8.7 Fehlerbehandlung beim Logging

Fehlerfälle:
- SD-Karte entfernt
- SD-Karte voll
- Schreibfehler

Verhalten:
- Logging stoppen oder pausieren
- Fehlerstatus im Web / REST anzeigen
- System weiterhin erreichbar halten

## 9. Speicherverwaltung

### 9.1 SD-Karten-Unterstützung

Das System verwendet eine MicroSD-Karte als primären Massenspeicher.

Unterstützte Eigenschaften:
- Kapazität: mindestens 64 GB
- Dateisystem: FAT / FAT32 / exFAT (abhängig von Bibliotheksunterstützung)

Die SD-Karte wird ausschließlich verwendet für:
- CAN-Log-Dateien
- Log-Metadaten (Statusinformationen)

### 9.2 Initialisierung und Verfügbarkeit

Beim Systemstart muss:
- Die SD-Karte initialisiert werden
- Die Verfügbarkeit geprüft werden

Falls keine SD-Karte verfügbar ist:
- Logging darf nicht starten
- System bleibt über Web / REST / USB erreichbar
- Fehlerstatus wird angezeigt

### 9.3 Speicherüberwachung

Das System muss kontinuierlich überwachen:
- Gesamtkapazität der SD-Karte
- Freien Speicherplatz

Diese Informationen müssen:
- Im Web-Interface angezeigt werden
- Über REST abrufbar sein

### 9.4 Überschreibstrategie

Wenn der verfügbare Speicher unter einen definierten Schwellwert fällt:
- Alte Log-Dateien dürfen überschrieben werden

Reihenfolge beim Überschreiben:
- Dateien, die als heruntergeladen oder hochgeladen markiert sind
- Älteste Dateien

Dateien ohne Download-/Upload-Markierung sollen:
- Möglichst nicht überschrieben werden

### 9.5 Konsistenz und Datensicherheit

Schreibzugriffe auf die SD-Karte müssen:
- Sequenziell erfolgen
- Über den Log Writer Task zentralisiert sein

Bei Stromverlust:
- Dateisystemkorruption ist möglichst zu vermeiden
- Aktive Dateien dürfen unvollständig sein, müssen aber erkennbar bleiben

### 9.6 Fehlerbehandlung

Fehlerfälle:
- SD-Karte entfernt
- Schreibfehler
- Dateisystemfehler

Verhalten:
- Logging stoppen oder pausieren
- Fehlerstatus setzen
- System funktionsfähig halten (Web / REST / USB)

## 10. USB-CAN-Interface

### 10.1 Allgemeines

Das System stellt über USB ein virtuelles serielles Interface bereit.

Zweck:
- CAN-Kommunikation mit einem PC
- Integration mit externer Software

### 10.2 Protokoll: GVRET

Verwendetes Protokoll:
- GVRET (General Vehicle Reverse Engineering Tool)

Kompatibilität:
- Voll kompatibel mit SavvyCAN

Das System muss:
- GVRET-Kommandos empfangen und interpretieren
- CAN-Frames über GVRET senden und empfangen

### 10.3 Mehrkanal-Unterstützung

CAN-Frames müssen:
- Dem jeweiligen CAN-Bus eindeutig zugeordnet sein

GVRET-Frames müssen:
- Informationen enthalten, welcher CAN-Bus betroffen ist

Falls das Protokoll keine native Mehrkanal-Unterstützung bietet:
- Eine interne Zuordnung muss erfolgen

### 10.4 Empfang über USB

Vom PC empfangene CAN-Nachrichten:
- Werden validiert
- In eine CAN-TX-Queue eingestellt

Voraussetzung für das Senden:
- Der betroffene CAN-Bus ist nicht als Read-Only konfiguriert

### 10.5 Logging über USB gesendeter Frames

Alle über GVRET gesendeten CAN-Nachrichten müssen:
- Zeitgestempelt werden
- Wie empfangene Frames geloggt werden

Es darf kein Unterschied im Log:
- Zwischen empfangenen und gesendeten Frames bestehen

### 10.6 Fehlerbehandlung USB

Fehlerfälle:
- USB-Verbindung getrennt
- Ungültige GVRET-Kommandos

Verhalten:
- Keine Beeinträchtigung des CAN-Loggings
- USB-Verbindung kann jederzeit neu aufgebaut werden

## 11. Netzwerk & Konnektivität

### 11.1 WLAN-Grundlagen

Das System nutzt WLAN für:
- Web-Interface
- REST-Schnittstelle
- OTA-Updates
- Automatischen Datei-Upload

Betriebsmodus:
- Station Mode
- Optional Access Point (falls vorgesehen)

### 11.2 Mehrere WLAN-Netzwerke

Es können bis zu drei WLAN-Netzwerke konfiguriert werden:
- SSID
- Passwort

Netzwerke sind priorisiert:
- Verbindung wird in der festgelegten Reihenfolge versucht

### 11.3 Verbindungslogik

Beim Systemstart:
- Versuch der Verbindung mit Netzwerk 1
- Bei Fehlschlag ? Netzwerk 2 ? Netzwerk 3

Bei Verbindungsverlust:
- Automatischer Reconnect-Versuch

Erfolgreiche Verbindung bleibt aktiv, bis sie verloren geht.

### 11.4 mDNS / Bonjour

Das System muss:
- Über mDNS / Bonjour im Netzwerk auffindbar sein

Anforderungen:
- Fester Hostname (z. B. canlogger.local)
- Zugriff auf Web-Interface ohne IP-Kenntnis

### 11.5 Netzwerkstatus

Folgende Informationen müssen verfügbar sein:
- Verbindungsstatus
- Aktive SSID
- IP-Adresse
- WLAN-Signalstärke in Prozent

Anzeige:
- Web-Interface
- REST-Schnittstelle

### 11.6 Robustheit

Netzwerkprobleme dürfen:
- Das CAN-Logging nicht beeinflussen

Alle Netzwerkfunktionen laufen:
- In separaten Tasks
- Mit niedrigerer Priorität als CAN und Logging

## 12. Web-Interface

### 12.1 Allgemeine Anforderungen

Das System stellt ein browserbasiertes Web-Interface bereit.

Zugriff erfolgt über:
- WLAN
- HTTP

Das Web-Interface dient ausschließlich der menschlichen Bedienung.

Alle Funktionen des Web-Interfaces müssen:
- Nicht-blockierend implementiert sein
- Das CAN-Logging nicht beeinträchtigen

### 12.2 Design und Struktur

Das Web-Interface orientiert sich:
- Optisch und strukturell am OpenInverter-Web-Interface

Anforderungen:
- Schlankes HTML/CSS/JavaScript
- Keine externen Online-Abhängigkeiten
- Alle Assets lokal im Flash gespeichert

### 12.3 Konfigurationsfunktionen

Über das Web-Interface müssen folgende Einstellungen möglich sein:

CAN-Bus (pro Bus):
- Aktiviert / Deaktiviert
- Bitrate
- Read-Only / Senden erlaubt
- Abschlusswiderstand (Ein / Aus)

Logging:
- Maximale Log-Dateigröße
- Manuelles Schließen aktiver Log-Dateien

Zeit:
- Manuelle Zeit- und Datumseinstellung
- Aktivierung / Deaktivierung der CAN-Zeitsynchronisation

Netzwerk:
- WLAN-Zugangsdaten (bis zu 3 Netzwerke)

Upload-Zieladresse (HTTP POST).

### 12.4 Status- und Übersichtsseiten

Das Web-Interface muss folgende Informationen anzeigen:
- Aktuelle Uhrzeit
- Systemzustand
- WLAN-Verbindungsstatus
- WLAN-Signalstärke in Prozent
- SD-Karten-Gesamtkapazität
- Freier Speicherplatz
- Anzahl geloggter CAN-Nachrichten seit Start (pro CAN-Bus)
- Liste aller Log-Dateien:
  - CAN-Bus-Zuordnung
  - Dateigröße
  - Status (aktiv / abgeschlossen / heruntergeladen / hochgeladen)

### 12.5 Dateiverwaltung

Log-Dateien müssen:
- Über das Web-Interface herunterladbar sein

Download einer Datei:
- Markiert die Datei als "heruntergeladen"

Aktive Log-Dateien:
- Dürfen nicht gelöscht werden

## 13. REST-Schnittstelle

### 13.1 Allgemeines

Das System stellt eine REST-Schnittstelle über HTTP bereit.

Zweck:
- Automatisierung
- Integration in externe Systeme

REST und Web-Interface greifen:
- Auf dieselbe interne Konfigurations- und Statuslogik zu

### 13.2 Authentifizierung

REST-Zugriffe müssen:
- Authentifiziert sein

Unterstützte Verfahren (konfigurierbar):
- HTTP Basic Authentication
- Token-basierte Authentifizierung

### 13.3 REST-Funktionsumfang

Die REST-Schnittstelle muss mindestens folgende Funktionen bereitstellen:

Status:
- Systemstatus
- Aktuelle Uhrzeit
- WLAN-Status
- SD-Karten-Status
- CAN-Statistiken pro Bus

Konfiguration (Lesen und Schreiben):
- CAN-Bus-Konfiguration
- Logging-Parameter
- WLAN-Konfiguration
- Upload-Zieladresse

Steuerung:
- Start / Stop Logging
- Schließen aktiver Log-Dateien

### 13.4 Dateiverwaltung über REST

REST-Endpunkte müssen:
- Log-Dateien auflisten
- Metadaten bereitstellen (Größe, Zeitbereich, Status)

Optional:
- Download einzelner Dateien

### 13.5 Fehlerbehandlung

REST-Antworten müssen:
- Sinnvolle HTTP-Statuscodes verwenden
- Fehler eindeutig beschreiben


### 13.6 DBC-JSON (Signalbeschreibung)

#### 13.6.1 Ziel

Das System muss eine DBC-Datei in einer JSON-Repräsentation abbilden, um Dekodierung auf dem Gerät zu ermöglichen.

#### 13.6.2 Quelle und Speicherung

DBC-JSON wird:
- Über Web/REST hochgeladen oder auf SD abgelegt

Die aktive DBC-JSON Konfiguration ist:
- Persistente Einstellung (welche Datei aktiv ist)

#### 13.6.3 Mindest-Schema (normativ)

Die JSON-Repräsentation muss mindestens enthalten:
- messages[]: id (CAN Identifier, int), is_extended (bool), name (string), dlc (int), signals[] (list)
- signals[]: name (string), start_bit (int), length (int), byte_order (intel/motorola), is_signed (bool), factor (number), offset (number), min (number, optional), max (number, optional), unit (string, optional), enum (object optional: value->label)

#### 13.6.4 Interpretation

Für jedes geloggte Frame:
- Passende Message per id + is_extended suchen
- Signale dekodieren
- Skalierung anwenden: value = raw * factor + offset
- Signedness und Byteorder korrekt berücksichtigen

#### 13.6.5 Versionierung

DBC-JSON muss ein Feld enthalten:
- schema_version

Firmware muss:
- Unbekannte schema_version ablehnen (mit Fehlermeldung)

### 13.7 REST/Web Ergänzungen (kurz)

REST-Endpunkte (Ergänzungsvorschlag, normativ):
- GET `/api/buffers` ? Ringpufferstatus pro Bus (fill, high-water, overflows)
- POST `/api/dump/influx` ? Start Dump (Datei/Bus/Zeitraum Parameter)
- GET `/api/dump/status` ? Dump-Status
- POST `/api/dbc` ? Upload/Set active DBC-JSON
- GET `/api/dbc` ? Info über aktive DBC-JSON
## 14. Automatischer Daten-Upload

### 14.1 Allgemeines

Das System unterstützt einen automatischen Upload von Log-Dateien.

Upload erfolgt über:
- HTTP POST

Zieladresse:
- Frei konfigurierbar
- Persistent gespeichert

### 14.2 Upload-Trigger

Der Upload kann ausgelöst werden durch:
- Abschluss einer Log-Datei
- Manuelle Auslösung über Web-Interface
- Manuelle Auslösung über REST

### 14.3 Upload-Inhalt

Beim Upload müssen übertragen werden:
- Eine oder mehrere Log-Dateien
- Metadaten:
  - CAN-Bus
  - Start- und Endzeit
  - Dateigröße
  - Dateiname

### 14.4 Upload-Status

Für jede Log-Datei wird gespeichert:
- Upload erfolgreich / fehlgeschlagen

Erfolgreich hochgeladene Dateien:
- Werden als "hochgeladen" markiert
- Gelten als bevorzugt überschreibbar

### 14.5 Fehlerbehandlung und Wiederholung

Bei Upload-Fehlern:
- Muss ein Wiederholungsmechanismus existieren
- Wiederholungen mit zeitlichem Abstand (Backoff)

Upload-Fehler dürfen:
- Das CAN-Logging nicht beeinflussen


### 14.6 InfluxDB Dump (Interpretation + Upload)

#### 14.6.1 Ziel

Das System muss eine Funktion bereitstellen, um geloggte rohe CAN-Daten (RAW-Frames) nachträglich oder unmittelbar zu interpretieren und als Messwerte in eine InfluxDB hochzuladen.

#### 14.6.2 Konfiguration

Folgende Parameter müssen konfigurierbar sein (Web/REST):
- InfluxDB Base URL (z. B. http(s)://.)
- Organization (InfluxDB 2.x) oder User/DB (1.x)
- Bucket (2.x) oder Database (1.x)
- Authentifizierung: Token (2.x) / User+Pass (1.x) / API Key (wenn genutzt)
- Measurement Name (Default)
- Default Tags (z. B. Fahrzeug, Gerät, CAN-Bus)
- Upload-Modus: Manuell (per UI/REST), automatisch nach File-Close (optional)
- Retry/Backoff Parameter

#### 14.6.3 Datenquelle

Dump-Quelle ist:
- Eine abgeschlossene Log-Datei (pro Bus)
- Optional: ein Zeitbereich oder Dateiliste

Aktive Log-Dateien:
- Dürfen nicht in den Dump einbezogen werden (oder nur snapshot-basiert, falls implementiert)

#### 14.6.4 Interpretationspipeline

Beim Dump müssen folgende Schritte erfolgen:
- Log-Datei lesen (RAW CAN Frames)
- Zeitstempel extrahieren
- Frame anhand DBC-Signalen dekodieren
- Messpunkte generieren
- InfluxDB Line Protocol erzeugen
- Upload per HTTP (Write API)

#### 14.6.5 Upload-Format (InfluxDB)

Upload erfolgt über InfluxDB Write API.

Zu erzeugende Struktur:
- measurement: konfigurierbar (Default)
- tags: can_bus (0..5), can_id (hex oder dec), signal (Signalname), optional weitere (device_id, vehicle, etc.)
- fields: value (float/int/bool), optional raw (hex) oder status (enum)
- timestamp: aus RTC-Zeitstempel der Frames, definierte Präzision (z. B. ns/us/ms) konfigurierbar

#### 14.6.6 Status und Rückmeldung

Für jeden Dump-Lauf müssen Statusinfos vorhanden sein:
- gestartet/aktiv/abgeschlossen/fehlgeschlagen
- Anzahl gelesener Frames
- Anzahl dekodierter Signale
- Anzahl gesendeter Punkte
- Fehlerliste (z. B. Auth failed, timeouts)

Anzeige:
- Web-Interface
- REST Endpunkt

#### 14.6.7 Fehlerbehandlung

Bei Netzwerkfehlern:
- Retry mit Backoff

Bei DBC/Decode-Fehlern:
- Frame wird übersprungen
- Fehlerzähler erhöht
- Dump läuft weiter
## 15. OTA-Firmware-Update

### 15.1 Allgemeines

Das System unterstützt Firmware-Updates over-the-Air (OTA).

OTA erfolgt:
- Über WLAN
- Über HTTP oder HTTPS

### 15.2 OTA-Aktivierung

OTA kann ausgelöst werden durch:
- Web-Interface
- REST-Aufruf

OTA darf nur gestartet werden:
- Wenn ausreichend Speicher verfügbar ist
- Wenn kein kritischer Fehlerzustand vorliegt

### 15.3 Verhalten während OTA

Vor Beginn eines OTA-Updates:
- Aktives Logging wird gestoppt
- Aktive Log-Dateien werden sauber geschlossen

Während OTA:
- Keine CAN-Kommunikation
- Keine SD-Schreibzugriffe

### 15.4 Sicherheit und Integrität

OTA-Updates müssen:
- Verifiziert werden (z. B. Prüfsumme)

Bei fehlgeschlagenem Update:
- System muss auf vorherige Firmware zurückfallen
- Keine Konfigurations- oder Log-Daten dürfen verloren gehen

### 15.5 Statusanzeige

OTA-Fortschritt und Ergebnis müssen:
- Im Web-Interface angezeigt werden
- Über REST abrufbar sein

## 16. Zeitmanagement

### 16.1 Zeitquellen

Das System verwendet folgende Zeitquellen:
- Externe RTC (primäre Zeitquelle)
- CAN-basierte Zeit-Synchronisation
- Interne Systemzeit (Fallback)

Die Priorisierung ist fest definiert und konfigurierbar.

### 16.2 Manuelle Zeitsetzung

Datum und Uhrzeit können:
- Über das Web-Interface
- Über REST

manuell gesetzt werden.

Manuelle Zeitsetzung:
- Überschreibt die aktuelle RTC-Zeit
- Wird persistent gespeichert

### 16.3 Zeit-Synchronisation über CAN

Das System unterstützt eine:
- Vordefinierte CAN-Nachricht zur Zeitübertragung

Anforderungen:
- Empfang auf allen CAN-Bussen
- Validierung der empfangenen Zeitdaten
- Aktualisierung der RTC

Die Funktion ist:
- Aktivierbar / deaktivierbar

### 16.4 Zeitstempel-Qualität

Jeder geloggte CAN-Frame muss enthalten:
- Absoluten Zeitstempel (Datum + Uhrzeit)

Optional:
- Relative Zeit seit Systemstart

## 17. Systemstatus & Monitoring

### 17.1 Laufzeitstatus

Das System muss kontinuierlich folgende Statusinformationen erfassen:
- Aktueller Betriebsmodus
- Laufzeit seit letztem Start
- Anzahl aktiver CAN-Busse
- Anzahl aktiver Log-Dateien

### 17.2 CAN-Statistiken

Pro CAN-Bus müssen erfasst werden:
- Anzahl empfangener Frames
- Anzahl gesendeter Frames
- Fehlerzähler

Statistiken werden:
- Im Web-Interface angezeigt
- Über REST bereitgestellt

### 17.3 Netzwerk- und Systemparameter

WLAN-Signalstärke (in Prozent)

Aktive SSID

IP-Adresse

CPU-Auslastung (optional)

Freier RAM (optional)

### 17.4 Systemgesundheit

Das System muss:
- Kritische Fehler erkennen
- Einen globalen Fehlerstatus führen

Fehlerzustände müssen:
- Sichtbar sein
- Eindeutig klassifiziert werden

## 18. Fehler- und Ausnahmebehandlung

### 18.1 CAN-Fehler

Erkennung von:
- Bus-Off
- Error Passive
- Error Active

Verhalten:
- Fehlerstatus setzen
- Logging fortsetzen, sofern möglich

### 18.2 Speicherfehler

Fehlerfälle:
- SD-Karte entfernt
- Schreibfehler
- Kein freier Speicher

Verhalten:
- Logging stoppen oder pausieren
- Fehler melden
- System weiter betreibbar halten

### 18.3 Netzwerkfehler

Fehlerfälle:
- WLAN-Verbindung verloren
- Upload-Server nicht erreichbar

Verhalten:
- Automatischer Reconnect
- Upload-Wiederholung
- Logging unbeeinflusst

### 18.4 Wiederanlaufstrategien

Nach einem Fehler muss:
- Ein definierter Wiederanlauf erfolgen

System darf:
- Nicht dauerhaft in einem Fehlerzustand verharren


### 18.5 Watchdog für Ringpuffer-Überlauf

#### 18.5.1 Ziel

Das System muss einen Watchdog implementieren, der Ringpuffer-/Queue-Überläufe in der CAN-Datenpipeline erkennt und behandelt, um stille Datenverluste zu vermeiden.

#### 18.5.2 Überwachte Komponenten

Pro CAN-Bus: RX-Ringpuffer / RX-Queue (Frames vom MCP2515)

Optional:
- TX-Queue (Frames zum Senden)
- Log-Writer Input-Queue (Frames zum SD-Schreiben)

#### 18.5.3 Metriken

Pro überwachten Puffer müssen mindestens erfasst werden:
- Aktuelle Belegung (Bytes/Frames)
- High-Water-Mark (Maximum seit Boot)
- Anzahl Overflows (Drop-Ereignisse)
- Letzter Overflow-Zeitstempel

#### 18.5.4 Verhalten bei drohendem Überlauf

Wenn Belegung > konfigurierbarer Schwellenwert (z. B. 80%):
- System setzt einen Warnstatus (Web/REST)
- System kann optional:
- Logging-Flush aggressiver betreiben (größere Schreibblöcke)
- Nicht-kritische Tasks drosseln (Upload/Web), ohne CAN-Logging zu blockieren

#### 18.5.5 Verhalten bei tatsächlichem Overflow

Bei Overflow muss das System:
- Einen Fehlerzähler erhöhen (per Bus)
- Das Ereignis persistent protokollieren (z. B. in Meta-Datei / NVS)
- Den Fehlerzustand über Web/REST sichtbar machen

Optional (konfigurierbar):
- Automatischen Neustart auslösen, wenn Overflows pro Zeitfenster > Grenzwert

#### 18.5.6 Testkriterien

Bei künstlich erzeugter Last muss:
- Overflow erkannt werden
- Zähler und Zeitstempel korrekt sein
- Web/REST Anzeige aktualisiert werden
#### 18.5.7 SD Speed Test (Development)

Setup:
- Test file: 16 MB, SPI mode, HSPI

Results:
- Baseline (SD.h, 4 KB buffer, default SPI): 0.35 MB/s
- Step 1 (SD.h, 20 MHz SPI): 0.90 MB/s
- Step 2 (SD.h, 32 KB buffer): 1.53 MB/s
- Step 3 (SD.h, 32 KB buffer + preallocate): 1.55 MB/s
- Step 4 (SdFat, 20 MHz SPI, preallocate): 0.54 MB/s
- Step 5 (SD.h, 40 MHz SPI, 32 KB buffer): 1.41 MB/s

Takeaways:
- Biggest gains come from SPI clock and larger write buffers.
- Preallocation helped only slightly on this card.
- SdFat was slower in this setup; keep SD.h for now unless further tuning proves faster.
- 40 MHz SPI was slightly slower than 20 MHz on this setup.
#### 18.5.8 RX Load Test (Development)

Purpose:
- Simulate the CAN RX ring buffer load in isolation to find the max sustainable frame rate before drops.

Setup:
- PlatformIO env: `rx_load_test`
- Source: `dev/rx_load_test.cpp`
- Serial (115200): `f <fps>` sets target rate, `r` resets counters

Metrics (serial output):
- Produced/s, Consumed/s
- Drops (ring overflow count)
- High-water (max ring fill level)
## 19. Sicherheit

### 19.1 Zugriffsschutz

Web-Interface und REST müssen:
- Geschützt sein

Unterstützte Mechanismen:
- Passwort
- Token-basierter Zugriff

### 19.2 Netzwerk-Sicherheit

OTA- und Upload-Verbindungen sollen:
- HTTPS unterstützen

Zugangsdaten müssen:
- Verschlüsselt gespeichert werden

### 19.3 Firmware-Integrität

Firmware-Images müssen:
- Verifiziert werden

Manipulierte Firmware darf:
- Nicht ausgeführt werden

## 20. Erweiterbarkeit

### 20.1 Hardware-Erweiterungen

Das System muss:
- Erweiterbar auf zusätzliche CAN-Busse sein

Hardware-Anpassungen sollen:
- Über die Hardware-Definitionsdatei erfolgen

### 20.2 Software-Erweiterungen

Neue Funktionen sollen:
- Als eigenständige Module integrierbar sein

Bestehende Funktionen dürfen:
- Nicht beeinträchtigt werden

### 20.3 Log-Formate

Unterstützung zusätzlicher Log-Formate:
- Muss möglich sein

Das Log-System soll:
- Formatagnostisch aufgebaut sein

## 21. Nicht-Ziele / Abgrenzungen

Das System ist kein:
- Echtzeit-Steuergerät
- Sicherheitskritisches Fahrzeugsteuergerät

Es ersetzt keine:
- OEM-Diagnosegeräte

Der Fokus liegt auf:
- Datenerfassung
- Analyse
- Weiterverarbeitung

## 22. Anhang

### 22.1 Konfigurationsparameter (Beispiele)

- CAN_BAUDRATE_BUS_X
- CAN_TERMINATION_BUS_X
- CAN_BUS_NAME_X
- LOG_FILE_SIZE_MAX
- WIFI_SSID_X
- WIFI_PASSWORD_X
- UPLOAD_URL

### 22.2 Beispiel-Dateistruktur

```
/sdcard
ÃÄÄ log_1234567_bus1.sav
ÃÄÄ log_1234567_bus2.sav
ÀÄÄ meta/
    ÀÄÄ file_status.json
```
### 22.3 Referenzen

- SavvyCAN / GVRET
- OpenInverter Web-Interface
- ESP32 Arduino Framework
- PlatformIO
