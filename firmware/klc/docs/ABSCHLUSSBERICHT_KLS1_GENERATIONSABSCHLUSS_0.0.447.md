# KLC 0.0.447 – KLS1-Generationsabschluss

## Kurzfazit

Hauptkonfiguration und KLS-Szenen bilden nun eine gemeinsame, persistente
Generation. Eine vorbereitete Migration bleibt bis zur erfolgreichen
Pool-, PIO-/DMA- und LED-Backend-Aktivierung in `await_activation`; erst danach
werden KLS-Autorität und Replacement gemeinsam als `COMPLETE` geschrieben.

## Boot- und Fallbackmodell

- Normal/Pending: Ziel-KLS wird verifiziert vorbereitet, aber nicht vorzeitig
  autoritativ. Nach erfolgreicher Hardwareaktivierung folgt der Abschluss.
- LKG/Previous/Recovery: Die tatsächlich aktivierte Fallback-Konfiguration
  erhält eine eigene passende KLS-Zielgeneration. Sämtliche Revisions-, CRC-,
  Längen-, Slot- und Dirty-Metadaten werden dabei neu initialisiert.
- Ein Stromausfall vor dem Abschluss hinterlässt einen wiederaufnehmbaren
  Journalzustand. Ein Generationenunterschied wird beim nächsten Boot erkannt
  und gegen die ausgewählte Hauptkonfiguration aufgelöst.
- Netzwerkreset und Zugangsdaten ändern die KLS-Generation bewusst nicht.

## Revisionen und Vorgangs-IDs

- `accepted_revision`: angenommener Kandidat.
- `applied_revision`: aktuell sichtbarer Payload in `g_config` und Runtime.
- `persisted_revision`: verifizierter KLS-Flashpayload.
- `storage_sequence`: unabhängige A/B-Flashgeneration.
- `operation_id`: 64-Bit-Wert aus persistenter Bootgeneration und lokalem
  Zähler; JSON überträgt ihn verlustfrei als String.

Die Browser-Wiederaufnahme prüft Vorgangstyp, ID, Szene, Auftragsrevision und
Bootgeneration. Fremde oder unbekannte Zustände löschen den sessionStorage-
Eintrag und stellen die servergerenderte `applied_revision` wieder her.

## Fehler- und Storageverhalten

Runtime-Apply, Runtime-Rollback, Storage-Open, Write, Verify und Commit sind als
getrennte Fehlerphasen sichtbar. Nach einem Flashfehler bleibt die bereits
aktive Szene dirty/RAM-only und direkt erneut speicherbar. Nach einem
Schreib-/Verifyfehler werden beide Slots sofort erneut gelesen. Die
LKG-Promotion besitzt nun ebenfalls einen zentralen CONFIG-Writer-Lease.

Alle Replacement-Auslöser stellen die persistente Replacement-ID über JSON,
Redirectparameter oder Antworttext bereit; der Statusendpunkt meldet Fortschritt,
Fehlerszene, Fehlercode, Configgeneration, Aktivierungsflags und Endzustand.

## Prüfung

- 28 gezielte KLS-Tests: bestanden.
- Eingebetteter Flow-JavaScriptblock mit Node.js geparst: bestanden.
- `git diff --check`: bestanden.
- Gesamte historische Testsammlung: 130 bestanden, 58 fehlgeschlagen. Die
  Fehlschläge betreffen überwiegend veraltete Wizard-, Release-, Sprachpaket-
  und Modulstruktur-Verträge außerhalb dieser Korrektur.
- Firmwareprofile wurden auf ausdrücklichen Wunsch nicht kompiliert.

Normale Einzel-Szenenspeicherung löst weiterhin keinen Voll-JSON-Commit aus.
Szene 0 bleibt vom KLS-Speichern und von der Migration ausgeschlossen.
