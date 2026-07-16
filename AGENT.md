# Arbeitsregeln fuer AI-Agenten

## Projektstruktur

- Produktionscode bleibt in `src/` und `include/`.
- Experimenteller Code, zugehoerige Tools und Dokumentation liegen unter `experiments/<name>/`.
- Neue Experimente werden in `experiments/README.md` eingetragen.
- Experiment-Environments liegen ausschliesslich in `experiments/platformio.ini` und werden mit `platformio run -d experiments -e <env>` aufgerufen.
- Testergebnisse werden nur gespeichert, wenn sie fuer die Nachvollziehbarkeit eines Experiments relevant sind.

## Sicherheit und Konfiguration

- Zugangsdaten und lokale Konfigurationen werden nicht committed.
- Im Repository werden nur Beispielkonfigurationen wie `*.example` abgelegt.
- Bestehende, nicht zum Auftrag gehoerende Aenderungen im Worktree bleiben unangetastet.

## Dokumentation

- `README.md` und betroffene Dokumentation werden aktualisiert, wenn sich Bedienung, Projektstruktur oder externe Schnittstellen aendern.
- Historische Implementierungsplaene und abgeschlossene Checklisten gehoeren nicht in diese Datei.

## Verifikation

- Aenderungen werden mindestens mit der direkt betroffenen PlatformIO-Umgebung geprueft.
- Produktionsrelevante Aenderungen werden nach Moeglichkeit in Debug und Release kompiliert.
- Ein Experiment darf den normalen Produktions-Build nicht beeinflussen.
