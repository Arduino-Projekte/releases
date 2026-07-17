# Changelog

## 0.0.447 – Gemeinsame Config-/KLS-Generation und rebootfeste Vorgänge

- Ein Replacement bleibt nach der verifizierten KLS-Migration im Zustand
  `await_activation`. Erst eine vollständig erfolgreiche Pool-, PIO-/DMA- und
  LED-Backend-Aktivierung schreibt die gemeinsame Autorität und `COMPLETE`.
  LKG-, Previous- und Recovery-Fallbacks binden ihre Szenen und Metadaten als
  eigene Generation; verworfene Kandidatenszenen werden nicht erneut aktiv.
- `accepted_revision`, `applied_revision`, `persisted_revision` und die
  A/B-`storage_sequence` sind getrennt. Runtime-Apply- und Rollbackfehler sowie
  Storagefehler liefern eindeutige Fehlerphasen und passende Browserrevisionen.
- Szenen- und Replacement-Vorgänge verwenden eine 64-Bit-ID aus persistenter
  Bootgeneration und lokalem Zähler. APIs übertragen sie als String; die
  Browser-Wiederaufnahme prüft Typ, Szene, Revision und Bootgeneration.
- Die LKG-Promotion verwendet den zentralen Storage-Writer-Lease. Sämtliche
  Replacement-Auslöser geben die persistente Vorgangs-ID zurück. Nach einem
  fehlerhaften Slotwrite wird der A/B-Zustand sofort vollständig neu gelesen.

## 0.0.445 – KLS1-Transaktionen, Dirty-Retry und zentrale Storage-Koordination

- Ein fehlgeschlagener Szenencommit lässt die neue Szene aktiv und markiert sie
  dauerhaft als `ram_only`/dirty. Ein erneuter Speicherversuch wird gegen den
  Fingerprint des letzten verifizierten Slots geprüft und nicht mehr irrtümlich
  als unverändert verworfen. Bearbeitungsrevision und persistente A/B-Sequenz
  sind getrennt.
- Queue-Coalescing beendet den ersetzten Vorgang als `superseded` und vergibt
  für den neuen Inhalt immer eine neue Vorgangs-ID. Das Statusfenster umfasst
  32 abgeschlossene Vorgänge; klassische Formularpfade führen auf eine
  selbstaktualisierende Statusseite.
- Die bisherige einzelne Autoritätsmarkierung wurde durch verifizierte
  A/B-Kontrollsätze ergänzt. Vollimport, Recovery und Werksreset verwenden ein
  ebenfalls doppelt abgelegtes Zieljournal. Beim Boot wird dessen Szenendigest
  mit der tatsächlich übernommenen Hauptkonfiguration verglichen; die Migration
  ist wiederaufnehmbar und löscht niemals vorab gültige Szenenslots.
- KLS-Slots werden nach CRC und Decodierung zusätzlich vollständig semantisch
  validiert. Gleiche Sequenzen mit unterschiedlichem Payload werden diagnostiziert
  und ohne stillen Zufallsentscheid behandelt. Freier LittleFS-Speicher wird vor
  einer Migration konservativ geprüft.
- Die Runtime übernimmt eine Szene vor dem Einreihen des Flash-Auftrags. Bei
  Runtime-, Konflikt- oder Queuefehler wird auf den vorherigen Stand
  zurückgerollt. Ein gemeinsamer LittleFS-Schreibkoordinator serialisiert
  Szenen-, Hauptconfig-, Import- und Sprachpaket-Schreiber.
- Backupexporte werden während laufender oder nur im RAM aktiver Szenen mit
  HTTP 409 abgelehnt. Der Rollbackdialog warnt ausdrücklich, dass Firmware vor
  KLS1 neue A/B-Szenen nicht lesen kann. Die Laufzeitmessung erfasst nun auch
  Commit- und Fehlerzustände.

## 0.0.382 – KNX-Farbstatus meldet die tatsächlich ausgegebene Farbe (Aufgabe 7)

- Die Statusobjekte Farbe R/G/B/W je Ausgang melden nicht mehr die zuletzt
  direkt gesetzte Farbe, sondern die tatsächlich ausgegebene: den
  arithmetischen Mittelwert der aktuell sichtbaren (nicht schwarzen) Pixel je
  Kanal direkt aus dem finalen Renderpuffer. Global- und Ausgangshelligkeit,
  Szenen, Übergänge, Rampen und Animationen sind darin genau so enthalten,
  wie sie physisch gesendet werden; ein ausgeschalteter Ausgang meldet
  0/0/0/0. Ein Kettenkopf mittelt über alle physischen Glieder seiner Kette.
- Kein zusätzlicher Pixel-Durchlauf pro Renderframe: Der Mittelwert wird nur
  bedarfsgesteuert beim Statusversand berechnet. Im Änderungsmodus wird der
  Laufzeitzustand höchstens einmal pro Sekunde eingesammelt; Farbänderungen
  werden erst ab einer Relevanzschwelle von 4 je Kanal gesendet
  (Ein-/Aus-Kanten immer), und nach dem Ende eines Übergangs wird der exakte
  Endwert einmal nachgesendet. Objekt-IDs, DPTs, Fehlerstatus DP 53 und der
  Vollversand nach BAOS-Reconnect bleiben unverändert.

## 0.0.381 – Diagnose für den Wizard-Speicherabbruch

- Der Bootvorgang ermittelt und protokolliert den Chip-Neustartgrund
  (Power-on, RUN-Pin, Software, Watchdog/Panic, Glitch, Brownout). Ein
  Watchdog-, Glitch- oder Brownout-Start erscheint zusätzlich als Warnung im
  Diagnoseprotokoll und ist damit auch ohne serielle Konsole sichtbar.
- Der komplette Wizard-Commit gibt auf der seriellen Konsole ein
  Stufenprotokoll aus (Formular gelesen, Kandidat validiert, Quittung
  PREPARING, Messlauf/JSON-Länge/freier Heap, Rückprüfung, Dateitransaktion,
  Journalabschluss, Quittung SUCCESS, HTTP-Bestätigung). Bricht die Ausgabe
  zwischen zwei Stufen ab, ist die abstürzende Stufe eindeutig eingegrenzt.

## 0.0.380 – Resetfeste Wizard-Bestätigung und stackfreier Config-Import

- Der gemeinsame JSON-Import-/Rücklesepfad legt die rund 13 KiB große
  `KlcDeviceConfig` nicht mehr auf dem Boot- oder HTTP-Request-Stack ab. Auch
  die zuvor zeitweise verschachtelten vollständigen Default-Konfigurationen
  und die zusätzliche Heap-Kopie der Serialisierungsprüfung entfallen. Damit
  kann der Controller nach einem gültigen Wizard- oder `/outputs`-Commit nicht
  mehr vor der HTTP-Bestätigung durch diesen Stacküberlauf neu starten.
- Wizard-API-Commits erhalten vor dem eigentlichen Speichern eine kleine,
  atomare LittleFS-Vorgangsquittung. Die Statusabfrage ist weiterhin an die
  zufällige 128-Bit-Vorgangskennung gebunden und kann einen erfolgreichen
  Commit nun auch nach einem Verbindungsabbruch oder Controller-Neustart
  eindeutig bestätigen.

## 0.0.379 – RAM-sichere Commits, funktionierende Steuerung und LED-Berechtigung

- Konfigurations-Commits allokieren nicht mehr pauschal einen 240-KiB-JSON-
  Puffer. Ein erster Serialisierungsdurchlauf bestimmt die exakte Größe; der
  zweite erzeugt und verifiziert weiterhin denselben atomaren Temp-/Backup-
  Commit. Damit funktionieren Wizard- und Ausgangs-Speichern auch neben dem
  beim Start reservierten LED-Masterpool.
- `/outputs` nutzt für den vollständigen Kandidaten einen festen, seriell
  verwendeten Arbeitspuffer statt einer großen Kopie auf dem HTTP-Stack und
  zeigt bei Speicherfehlern jetzt die konkrete Storage-Ursache an.
- Der Wizard-Abbruch ist idempotent: Nach bereits erfolgreichem Commit kann
  eine alte Browserseite den Stand nicht mehr überschreiben. Ein echter
  Abbruch speichert nur das Abschluss-Flag und erzeugt keinen falschen
  Pending-/Neustartstatus.
- `/control` lädt eine neue kompakte Laufzeitprojektion über `/api/control`
  statt für die Bedienseite die bis zu 190 KiB große Editor-/Diagnoseantwort
  `/api/scenes` aufzubauen. Ungültige Antworten werden als Ladefehler gezeigt
  und nicht mehr als „Keine Ausgänge vorhanden“ getarnt.
- Normale Benutzer erreichen `/led` in der erweiterten Ansicht. Nur die
  Kachel „RAM- / LED-Speicher“ wird serverseitig ausschließlich für
  Administratoren erzeugt.

## 0.0.377 – Persistentes Pending/LKG, transaktionale Aktivierung und Recovery

- Die atomar gespeicherte Hauptkonfiguration ist bei strukturellen
  Ausgangsänderungen jetzt ein persistenter Pending-Kandidat. Eine getrennte
  Last-Known-Good-Datei wird erst nach erfolgreicher Validierung,
  Masterpool-Reservierung/-Aufteilung, PIO-, DMA- und vollständiger
  Backendinitialisierung aktualisiert.
- Ein kompaktes Aktivierungsjournal schützt Stromausfall- und Resetfenster,
  hält Fehlerursache, Schritt, Ausgang, Versuche und Sequenz fest und
  unterdrückt Neustartschleifen mit demselben fehlgeschlagenen Kandidaten.
  Nach vollständigem Rollback wird LKG einmal versucht; andernfalls bleibt
  die LED-Ausgabe im erreichbaren sicheren Recoverymodus deaktiviert.
- Backup-Import, Wizard und Reset übernehmen strukturelle Werte nicht mehr
  teilweise live. Sie speichern als Pending; aktive Poolbereiche,
  Kettentopologie und LED-Runtime wechseln erst nach Neustart.
- Sichere 64-Bit-Prüfungen decken Multiplikationen, Pooloffsets,
  Blackout-Tails und Wire-Time ab. Ein fehlgeschlagener PIO-/DMA-Start gibt
  sämtliche bereits belegten Ressourcen vor dem Fallback frei.
- Der flüchtige Testmodus erlaubt 0 Pixel eindeutig als „keine Ausgabe“
  ohne PIO/DMA oder Kantenindex. 1/800/801/1200 behalten ihre zentrale
  Semantik; 1201 bleibt abgelehnt.
- Diagnose, globale Hinweise, LED-JSON und Support-Snapshot verwenden eine
  zentrale LED-/Aktivierungsdiagnose mit aktiven, Pending- und LKG-Plänen,
  Heapwerten vor/nach Pool/Backend, Fehlern, Zählern und Recoverystatus.
- Offline-Hilfe und sieben Sprachpakete wurden um Pending/LKG, Recovery,
  Speicherbudget/-reserve, Pixelgrenzen und Kettenverhalten ergänzt
  (Sprachpaket 2.4.1, Mindestfirmware 0.0.377).

## 0.0.376 – LED-Masterpool beim Start, flüchtiger Ausgangs-Testmodus, Neustartbanner

- LED-Speicherverwaltung umgebaut (Aufgabe 16): Alle großen LED-Puffer
  (Render- + DMA-Puffer aller aktiven Ausgänge inkl. Blackout-Tail plus ein
  fester Testpool für EINEN Ausgang bis 1200 Pixel) liegen jetzt in EINEM
  zusammenhängenden **LED-Masterpool**, der beim Hochfahren einmalig
  reserviert und im Betrieb nur noch mit berechneten Offsets aufgeteilt
  wird (`klc_led_pool`). Keine wiederholten großen malloc/free-Zyklen und
  keine Heap-Fragmentierung mehr durch /outputs-Änderungen.
- Strukturelle Ausgangswerte (Aktiv, Pixelzahl, LED-Farben, LED-Chip,
  Farbreihenfolge, Verkettung, Kettenrichtung) werden beim Speichern nur
  noch **persistent als ausstehende Konfiguration** abgelegt
  (`klc_output_pending`); die aktive Laufzeitkonfiguration bleibt bis zum
  kontrollierten Neustart vollständig unverändert (keine teilweise aktive
  Mischung). Nicht-strukturelle Werte (Name, Power, Sendeeinstellungen,
  Versatz, Segmente, Power-On-/KNX-Verhalten, Helligkeit) wirken weiterhin
  sofort.
- Der zentrale atomare Speicherpfad blendet die ausstehenden Strukturwerte
  in JEDEN regulären Konfigurations-Vollwrite ein; Szenen-/Netzwerk-/KNX-
  Speichervorgänge können eine wartende Ausgangsänderung nicht mehr
  stillschweigend zurücksetzen. Backup-Import/Resets (Recovery-Pfad) und
  der Einrichtungsassistent ersetzen den gespeicherten Stand bewusst
  vollständig.
- **Flüchtiger Testmodus unter /outputs:** LED-Typ/Chip, RGB/RGBW,
  Farbreihenfolge, Pixelanzahl sowie Anfang/Ende/Rot/Grün/Blau/Weiß
  blinken bleiben ohne Neustart und ohne Speichern sofort testbar. Die
  Testwerte wirken nur im RAM und laufen im festen Testpool; es ist immer
  nur EIN kontrollierter Ausgangstest gleichzeitig aktiv, beim Beenden
  (Test zurücksetzen, Kachel schließen, Seite verlassen, Speichern) wird
  der aktive Zustand wiederhergestellt.
- **Neustartbanner** auf allen Seiten („Gespeicherte Ausgangsänderungen
  warten auf Aktivierung. Ein Neustart ist erforderlich.") mit „Jetzt neu
  starten" (kontrollierter Neustart über /output/restart) und „Später neu
  starten". Der Bedarf entsteht immer aus dem Vergleich gespeicherter
  gegen aktiver Strukturwerte (kein Sticky-Flag): Wer alle Werte exakt
  zurückstellt und speichert, sieht den Banner automatisch verschwinden.
  /outputs zeigt je Ausgang kompakt „Aktiv/Im Test/Gespeichert".
- Schlägt die Poolreservierung oder Aktivierung beim Start fehl, bleibt
  die Firmware ohne echte LED-Ausgabe erreichbar (WebUI/Netzwerk/
  Service-AP), setzt einen konkreten Diagnosefehler (neu: 1703 = Pool zu
  klein, Neustart erforderlich) und startet KEINE Neustartschleife; die
  gespeicherte Konfiguration bleibt zur Korrektur erhalten.
- **Diagnose:** /led zeigt eine verständliche RAM-/LED-Speicherdiagnose
  mit drei Balken (Heap-Auslastung, Systemreserve 64 KiB, LED-Pufferbudget
  128 KiB), allen Pool-/Plan-/Pixelwerten (aktiv und ausstehend) und einem
  festen Heap-Verlaufs-Ringpuffer (5-s-Takt, ca. 10 Minuten, ohne
  LittleFS). Dieselben zentralen Werte stehen im Support-Snapshot
  (/support.txt) und in /api/leddiag (`memory`).
- Sprachpakete 2.4.0: 58 neue UI-Texte in allen sieben Sprachen,
  kanonische Schlüsselmenge/Hash in der Firmware nachgezogen,
  minimum_firmware_version 0.0.376.
- Wizard-Speichern RAM-schonend gehalten: Der zentrale Speicherpfad nutzt
  einen Schnellpfad ohne zusammengeführte Konfigurationskopie, wenn der
  Kandidat strukturell bereits dem gespeicherten Ausgangsstand entspricht
  (Wizard-Commit, /outputs-Speichern, alle Saves ohne ausstehende
  Änderung). Der RAM-Spitzenbedarf beim Speichern bleibt damit exakt auf
  dem bisherigen Niveau, während der große JSON-Puffer lebt.
- „Einrichtungsassistent abbrechen" beendet den Assistenten jetzt
  wirklich: Es wird nur das Abgeschlossen-Flag gespeichert (keine
  weiteren Änderungen, kein Neustart) und direkt zur Hauptseite
  weitergeleitet (/api/setup/skip mit no_reboot=1; ohne JavaScript bleibt
  der bisherige /setup/cancel-Fallback).

## 0.0.320 – OTA-Download repariert: eth0-Empfang verhungerte während des Downloads

- Befund (Serial-Log): Download startet, Content-Length kommt an, dann
  15 s keine Daten → „Online-Update Download Timeout" → Abbruch. Auf
  Firmware ohne den 0.0.318-Fix blockierte der verwaiste Updater-Rest
  anschließend alle weiteren Versuche („ERROR[0]: No Error").
- Ursache: Der BIN-Download läuft innerhalb des Webserver-Handlers, die
  Hauptschleife steht solange. Im WLAN-Profil wird der W5500 aber
  maßgeblich vom Vordergrundservice `klcEthernetServiceNow()` bedient
  (0.0.314: der geteilte CYW43-Async-Kontext verzögert den
  Hintergrund-Poll). Ohne diesen Aufruf in der Download-Schleife wurde der
  TCP-Empfang über eth0 praktisch nicht mehr bedient – die kleinen
  Manifest-Abrufe (update.json/versions.json) rutschten noch durch, der
  1,5-MB-Download verhungerte.
- Fix: `klcEthernetServiceNow()` läuft jetzt in jeder Iteration der
  Download-Schleife mit (intern auf 500 µs gedrosselt). Zusätzlich meldet
  der Download-Timeout jetzt auf Serial, wie viele Bytes bis dahin
  angekommen sind.
- Hinweis: Geräte mit 0.0.317 oder älter können dieses Update NICHT per
  Online-OTA ziehen (der Download-Bug steckt in der laufenden Firmware).
  Einmalig per USB/BOOTSEL flashen oder nach einem Neustart die BIN als
  lokale Datei über die Update-Seite hochladen – am besten über die
  WLAN-IP, dann läuft der Upload über den Funk statt über den W5500.

## 0.0.319 – Service-Taster final: 2–5 s Netzwerk-Reset, 5–10 s Werksreset

- Neue Zonen (Auswertung beim Loslassen, im laufenden Betrieb):
  - unter 2 s: keine Aktion
  - 2 bis unter 5 s: **Netzwerk-Reset** – Netzwerkeinstellungen auf
    Werksdefaults (Ethernet und WLAN wieder an, DHCP, Service-AP aktiv,
    Hostname zurück), dann Neustart. Das ist zugleich der Rückweg, wenn
    Ethernet/WLAN per KNX (DP 20/21) abgeschaltet wurden.
  - 5 bis unter 10 s: **Werksreset** komplett (einschließlich Netzwerk),
    dann Neustart.
  - 10 s und länger: keine Aktion (Schutz gegen klemmenden/verdeckten
    Taster).
- Rückmeldung über die Error-LED beim Halten: blinkt schnell in der
  Netzwerk-Reset-Zone, leuchtet dauerhaft in der Werksreset-Zone, aus nach
  10 s.
- Der Kurzdruck „Netzwerk einschalten" aus 0.0.315 entfällt zugunsten des
  Netzwerk-Resets; die zugehörige Schaltfunktion wurde entfernt.

## 0.0.318 – OTA repariert: verwaister Updater blockierte alle weiteren Versuche

- Befund: Online-Update startete den Download, dann kam nur
  „ERROR[0]: No Error" und nichts passierte – bei jedem weiteren Versuch
  wieder. „ERROR[0]" ist die Meldung des Core-Updaters für „already
  running": Update.begin() wird abgelehnt, weil noch ein alter Vorgang
  offen ist.
- Ursache: Drei Abbruchpfade (Download-Timeout/Stream-Abriss, unvollständig
  empfangene BIN, Client-Abbruch beim Browser-Upload) setzten nur die
  KLC-Flags zurück, aber nie den Core-Updater (`Update.end(false)` fehlte).
  Nach dem ersten fehlgeschlagenen Versuch war OTA damit bis zum Neustart
  blockiert.
- Fix: Alle Abbruchpfade verwerfen den Updater-Rest jetzt sauber.
  Zusätzlich Selbstheilung direkt vor Update.begin(): ein verwaister Rest
  wird erkannt, gemeldet und verworfen – ein hängender Zustand kann sich
  nicht mehr festsetzen.
- Soforthilfe für laufende Geräte mit alter Firmware: Controller einmal
  neu starten, dann „Empfohlenes Update installieren" erneut ausführen.

## 0.0.317 – WLAN-Masterschalter in der WebUI, WLAN-Änderungen wirken sofort

- Neuer Haken „WLAN verwenden (Client und AP)" oben in der WLAN-Kachel:
  das ist derselbe Masterschalter wie KNX DP 21 (Haken, KNX und
  Service-Taster schalten denselben gespeicherten Zustand). „Access Point
  verwenden (Servicezugang)" heißt jetzt eindeutig so – er schaltet nur den
  AP, nicht das ganze WLAN.
- WLAN-Änderungen (Masterschalter, Client/AP-Haken, SSID, Passwort,
  Priorität, IP-Einstellungen) wirken jetzt sofort: das Funkmodul wird nach
  dem Speichern automatisch neu gestartet, ein Controller-Neustart ist nicht
  mehr nötig. Bisher stand zwar „wirkt nach Neustart" im Hinweis, das wurde
  aber leicht übersehen – deshalb schien der AP trotz entferntem Haken
  weiterzulaufen und der Client verband sich nicht.
- Hinweis ergänzt: das Funkmodul unterstützt nur 2,4-GHz-WLANs (kein 5 GHz).

## 0.0.316 – Update-Seite: leerer Datei-Upload klar abgefangen

- Befund: „[OTA] Abbruch: Dateiname ist keine .bin/.bin.gz-Datei." trotz
  korrekt vorhandener BIN auf GitHub. Ursache war nicht das Online-Update,
  sondern der Button „Lokale Firmware-Datei hochladen": wird er ohne vorher
  gewählte Datei geklickt, schickt der Browser einen leeren Dateinamen, und
  die Firmware meldete nur die verwirrende .bin-Fehlermeldung.
- Fix WebUI: ohne gewählte Datei wird das Formular gar nicht mehr
  abgeschickt; stattdessen kommt ein Hinweis, zuerst eine BIN-Datei zu
  wählen bzw. für das GitHub-Update den Button „Empfohlenes Update
  installieren" zu verwenden.
- Fix Firmware: leerer Dateiname wird als eigener Fall gemeldet
  („keine Datei ausgewählt", Serial-Hinweis auf den Online-Update-Button).
- Zum Installieren von GitHub gilt weiterhin: „Nach Update suchen", dann
  „Empfohlenes Update installieren" – das Gerät lädt die BIN selbst herunter.

## 0.0.315 – Netzwerk per KNX schaltbar, /network neu geordnet, Service-Taster neu

- KNX-Netzwerkschalter (dauerhaft gespeichert, gilt auch nach Neustart):
  DP 20 = Ethernet Ein/Aus (GA 2/2/14), DP 21 = WLAN Ein/Aus (GA 2/2/15),
  Status DP 54/55 (GA 2/2/16/17). WLAN-Aus schaltet Client UND Access Point
  ab (Funkmodul aus). Sind beide Schnittstellen aus, stoppen auch Webserver
  und OTA-Tick; die freigewordene Loopzeit kommt den LED-Animationen zugute.
  ETS-Dateien im Ordner `KNX/` aktualisiert (Datenpunkte, GA-Liste txt/xml,
  DCA-Import-CSV, alle v0.0.315).
- `/network` umgebaut: WLAN-Kachel jetzt oben, Kacheln heißen „WLAN" und
  „Ethernet". Der Haken „Ethernet verwenden" liegt jetzt in der
  Ethernet-Kachel und wirkt sofort (ohne Neustart). Sicherheitsnetz bleibt:
  über die WebUI lässt sich nicht die letzte aktive Schnittstelle abschalten
  (KNX und Service-Taster dürfen das bewusst).
- Betrieb ohne W5500-Modul: Haken raus = kein Startversuch, kein Fehler,
  keine rote Blink-LED; ein noch aktiver Ethernet-Fehler wird beim
  Deaktivieren sofort gelöscht. Haken gesetzt + Modul fehlt = Fehler 1001
  wie bisher, das Modul deaktiviert sich selbst und kostet keine Loopzeit.
- Service-Taster komplett neu (Auswertung beim Loslassen, im laufenden
  Betrieb): kurz = Ethernet und (falls vorhanden) WLAN wieder einschalten,
  5–10 s = Werksreset + Neustart (Error-LED leuchtet in diesem Fenster
  dauerhaft), länger als 10 s = keine Aktion (Schutz gegen klemmenden oder
  verdeckten Taster; bisher löste ein klemmender Taster beim Boot einen
  Werksreset aus). Der alte Boot-Handler (3 s Netzwerk-Reset / 8 s
  Werksreset) entfällt.
- Laufzeit-Herunterfahren sauber implementiert: W5500 per `end()` +
  RST-Reset (kein SPI-/Worker-Verkehr mehr), CYW43-Funk per
  `softAPdisconnect`/`disconnect`/`WIFI_OFF`, HTTP-Listener gestoppt.
  Wiedereinschalten zur Laufzeit startet Ethernet ohne blockierendes
  DHCP-Warten (IP meldet der Tick im Hintergrund).
- Konfigschema 55: neues Feld `wifi.wlan_enabled` (Master für wlan0+ap0),
  neue KNX-Globalobjekte `global_eth_enable`/`global_wlan_enable` und
  Statusobjekte; alte Konfigurationen laden kompatibel mit „ein" als
  Default.

## 0.0.311 – W5500 zurück auf Polling (4 ms), INT-Modus verworfen

- Befund mit 0.0.310: Unter `/control`-Last erneut Komplett-Hänger, diesmal
  bestätigt mit stehender Run-LED → die Hauptschleife selbst war tot, nicht
  nur das Netzwerk. Der INT-Modus (0.0.307–0.0.310) ist damit als Ursache
  überführt: Im IRQ-Pfad des Core-Treibers konkurriert der GPIO-Interrupt
  mit der lwIP-Sperre der Hauptschleife, `sendFrame()` wartet unbegrenzt,
  und die KLC-RX-Reparatur konnte aus dem IRQ-Kontext eine laufende
  SPI-Transaktion der Hauptschleife zerschneiden.
- Beide Profile laufen wieder im bewährten Polling-Modus – aber mit 4 ms
  Poll-Intervall statt 20 ms (`lwipPollingPeriod(4)`). Damit läuft der
  16-KB-RX-Puffer auch unter Last praktisch nicht mehr über; genau dieser
  Überlauf hatte auf dem WLAN-Board (geteilter CYW43-Async-Kontext, träge
  Polls) ursprünglich das tote Ethernet verursacht.
- Die RX-Selbstheilung (Socket-Reopen bei unplausibler Frame-Länge) bleibt
  aktiv und läuft im Polling-Worker sauber serialisiert – keine
  IRQ-Kontext-Risiken mehr.

## 0.0.310 – RX-Reparatur ohne Chip-Reset, keine Komplett-Hänger mehr

- Befund: Auf `/control` mit „Live aktualisieren" konnte das Board komplett
  einfrieren (Browser meldet „signal is aborted…", danach nur noch Reset
  möglich). Zwei Mechanismen kamen zusammen:
  1. Die RX-Reparatur aus 0.0.308 machte einen Chip-Softreset – der wirft
     auch den PHY ab (Link 1–2 s weg). Der Core-Treiber wartet in
     `sendFrame()` aber unbegrenzt auf SENDOK; wird während der
     Link-Neuverhandlung gesendet, steht die komplette Firmware.
  2. Der Aufruf von `end()` enthielt eine unbegrenzte Warteschleife im
     IRQ-Kontext.
- Fix: Die Reparatur setzt jetzt nur noch Socket 0 zurück (CLOSE/OPEN per
  direktem, zeitbegrenztem Registerzugriff): RX-/TX-Ringzeiger werden frisch
  aufgesetzt, MAC/PHY/Link und lwIP-Zustand bleiben unberührt, Dauer im
  Mikrosekundenbereich. Nur wenn der Socket nicht mehr reagiert, folgt als
  letzte Rettung der Chip-Reset. Alle Wartezeiten sind hart begrenzt
  (max. ~5 ms), es gibt keine Endlosschleifen im IRQ-Kontext mehr.
- Warnzeile erweitert: `… Vorfaelle gesamt: N, davon mit Chip-Reset: M`.

## 0.0.309 – W-Profil: Online-Updatepfad korrigiert

- klc_16m_w: `KLC_RELEASE_VARIANT_PATH` zeigt jetzt auf
  `firmware/klc/klc_16m_w` – dorthin veröffentlicht build_klc.bat
  tatsächlich (Beta-Manifest von 0.0.308 liegt dort). Die Firmware fragte
  bisher `firmware/klc_w/klc_16m_w` ab; dieser Pfad existiert im
  Release-Repo nicht (HTTP 404). Damit schlugen Online-Updateprüfung und
  Online-Update auf dem W-Board immer fehl („Online-Prüfung fehlgeschlagen…",
  4–7 s = nur TLS-Handshake bis zum 404) – es war kein Netzwerkproblem.
- Schutz gegen Varianten-Verwechslung bleibt vollständig erhalten:
  getrennte Variantenordner, eindeutige Dateinamens-Prefixe
  (`…-16m` vs. `…-16m-w`) und der Manifest-Profilcheck in klc_ota.

## 0.0.308 – W5500-Empfang übersteht RX-Überlauf, Netz-Diagnose

- W5500 (beide Profile): Läuft der 16-KB-MACRAW-Empfangspuffer über, geriet
  der Lesezeiger bisher dauerhaft aus dem Tritt – Ethernet war bis zum
  Neustart tot. Typischer Auslöser: LittleFS-Schreibvorgänge (Konfiguration
  speichern) blockieren Interrupts/XIP, während Browser-Tabs weiter Traffic
  senden. Befund mit 0.0.307: WebUI über Ethernet lief, fiel aber nach
  mehreren Speichervorgängen mit parallelen Tabs aus. Die Firmware erkennt
  jetzt unplausible Frame-Längen (>1600 Bytes), setzt den MACRAW-Socket
  automatisch neu auf (IP/DHCP bleibt erhalten, TCP holt Verluste per
  Neuübertragung nach) und meldet den Vorfall:
  `[ETH] WARNUNG: RX-Puffer war uebergelaufen …` + Diagnose-Warncode 1008.
- Vor der Online-Updateprüfung wird eine Routing-/DNS-Diagnosezeile
  ausgegeben (`[NET] Diagnose: eth0 IP …, GW …, DNS1 …, Default-Netif …`),
  um die weiterhin fehlschlagende Online-Prüfung auf dem W-Board
  einzugrenzen.

## 0.0.307 – W-Profil: Ethernet-Empfang über INT-Leitung

- klc_16m_w: Der W5500-Empfang läuft jetzt IRQ-getrieben über die INT-Leitung
  (GP21) statt über das 20-ms-Polling. Hintergrund (Befund mit 0.0.306):
  Beim WLAN-Build teilt sich das W5500-Polling den Async-Kontext mit dem
  CYW43-Funktreiber. DHCP im Setup funktionierte, aber im Loop-Betrieb war
  eth0 danach in beide Richtungen tot – WebUI über Ethernet nicht erreichbar
  und `[OTA] … beendet (4052 ms, Fehler)`, während die WebUI über den
  WLAN-AP normal lief. Der IRQ-Empfang hängt nicht am Polling-Worker.
- Neue Serial-Zeile `[ETH] Empfangsmodus: Interrupt (INT auf GP21)` bzw.
  `Polling` zeigt den aktiven Modus.
- klc_16m bleibt bewusst beim bewährten Polling (läuft dort fehlerfrei);
  die INT-Leitung ist auf der Platine für beide Module verdrahtet, sodass
  das Standardprofil später nachziehen kann.

## 0.0.306 – Service-AP ab Werk beim W-Profil, Netzwerk-Diagnosezeilen

- klc_16m_w: Der WLAN-Service-Access-Point (ap0) ist ab Werk aktiv. SSID ist
  automatisch `KLC-xxxxxx` (aus der Chip-ID), IP `192.168.4.1`, offen ohne
  Passwort. Damit ist das Gerät nach dem ersten Flashen per Smartphone
  erreichbar, um WLAN einzurichten – wie von der Inbetriebnahme erwartet.
  Über die WebUI (`Netzwerk → Access Point`) kann der AP weiterhin
  deaktiviert oder mit Passwort versehen werden. klc_16m ignoriert das Feld
  unverändert.
- Konfigurationsschema 54: Bestehende Konfigurationen (Schema <54) schalten
  den Service-AP beim Laden einmalig ein, weil `ap_enabled=false` dort nur
  der alte Default war. Wer den AP danach ausschaltet, behält das dauerhaft.
- Diagnose für das Problem „B-Variante: IP vorhanden, WebUI nicht erreichbar":
  - `[OTA] Automatische Update-Prüfung beendet (… ms, OK/Fehler): …` zeigt,
    dass die Hauptschleife nach der synchronen Online-Prüfung weiterläuft.
  - `[WEB] Erste HTTP-Anfrage empfangen: /` erscheint einmalig beim ersten
    Browserzugriff. Fehlt die Zeile trotz Zugriffsversuch, erreichen die
    Anfragen den Webserver nicht (Netzwerk-/lwIP-Ebene); erscheint sie,
    liegt das Problem in der Antwort-/Render-Phase.

## 0.0.301 – W-Pinplan final und RGBW-Test ohne Neuladen

- RP2350B-Plus-W: Pinprofil nach Waveshare-Pinout bestätigt. Headerpositionen
  1–30 sind GPIO-gleich zum RP2350-Plus; nur die ADC-Eckpositionen ändern sich:
  Service-Taster GP40 (Pos. 31), Run-LED GP41 (Pos. 32), Error-LED GP42
  (Pos. 34) statt GP26/GP27/GP28.
- `/outputs`: Beim Umschalten der LED-Farben RGB↔RGBW erscheint bzw.
  verschwindet der Button `Weiß blinken` sofort, ohne Neuladen der Seite.
- Die GPIO-Warnliste der Ausgangsseite nutzt jetzt die Boardprofil-Pins statt
  fest kodierter 26/27/28.

## 0.0.300 – Uptime lesbar formatiert

- Dashboard: Uptime wird nicht mehr als rohe Sekunden angezeigt, sondern als
  `X min Y s`, ab einer Stunde als `X h Y min`, ab einem Tag als `X d Y h`.
- Die bestehende Uptime-Formatierung der System-/Diagnoseanzeige zeigt bei
  mehr als einem Tag jetzt ebenfalls kompakt `Tage + Stunden` (ohne Minuten).

## 0.0.299 – Deutsche Hilfe mit Umlauten

- Deutsche Hilfe-Texte verwenden jetzt korrekte Umlaute statt `ae/oe/ue`,
  wo es grammatikalisch richtig ist.
- Hilfe-Suche normalisiert Umlaute: Suche nach `Ausgänge` und `Ausgaenge`
  findet dieselben Treffer.
- Schnellstart korrigiert: `Ein Backup speichern` statt `Danach erneut...`;
  LED-Typ, LED-Anzahl und Farbreihenfolge werden gemeinsam geprüft.

## 0.0.298 – Hilfe-Schnellstart lesbarer

- `/help`: Hilfetexte mit Zeilenumbrüchen werden jetzt mit sichtbaren
  Zeilenumbrüchen gerendert.
- Deutscher Schnellstart neu sortiert: Updates prüfen vor Konfiguration,
  frühes Backup entfernt, Szenen/Abläufe vor ETS-/KNX-Verknüpfung.

## 0.0.297 – Buildfix Dashboard-Zähler

- Dashboard-Buildfix: `disabled_outputs` wird in `klcWebUiIndexHtml()`
  jetzt korrekt gezählt, bevor der Hinweis zu deaktivierten Ausgängen gerendert
  wird.

## 0.0.296 – Deaktivierte Ausgänge im Dashboard sichtbar

- `/outputs`: die doppelten Summary-Kacheln `Vorgesehene Ausgänge` und
  `Platinen-Ausgänge` entfernt.
- Dashboard-Tabelle `Ausgänge / Szene / geschätzte Last` zeigt jetzt Status,
  deaktivierte Ausgänge mit `0 mA` und einen Hinweistext zur Anzahl
  deaktivierter Ausgänge.

## 0.0.295 – Direkte Feldhilfe-Links

- `/help?q=...` filtert jetzt direkt nach Hilfe-IDs wie `outputs.pixels`
  oder `power.controller_limit_ma`.
- Wichtige Felder auf `/outputs` und `/power` haben einen kleinen Hilfe-Link
  zur passenden Feldbeschreibung.
- Der eingebaute deutsche Offline-Fallback enthält jetzt die verlinkten
  Ausgabe- und Power-Felder.

## 0.0.294 – Hilfe-Download HTTPS repariert

- Hilfe-Download nutzt GitHub-HTTPS jetzt wie OTA mit `setInsecure()`,
  Timeout und HTTP/1.0.
- HTTP `-1` wird als Netzwerk-/DNS-/TLS-Verbindungsfehler gemeldet.

## 0.0.293 – Hilfe-Registerkarte mit Online-Aktualisierung

- Neue WebUI-Seite `/help` mit Suche, Statusanzeige und Button
  `Hilfe aktualisieren`.
- Eingebaute Basis-Hilfe als Offline-Fallback für DE/EN/FR/ES/IT/NL/PL.
- Online-Hilfe wird je Sprache von
  `releases/firmware/klc/help/help_<sprache>.json` geladen und lokal in
  LittleFS gespeichert.
- Neue APIs: `/api/help`, `/api/help/data`, `/api/help/download`.

## 0.0.292 – Platinen-Ausgänge korrekt auf 5 begrenzt

- `/api/outputs`: `max_outputs` kommt jetzt aus dem Boardprofil
  (`KLC_BOARD_LED_OUTPUT_COUNT`) statt aus der internen Array-Reserve.
- `/outputs`: Kachel heißt `Platinen-Ausgänge` und zeigt für die KLC-Platine 5.
- Konfigurationsprüfung lehnt mehr Ausgänge als im Boardprofil vorgesehen ab.

## 0.0.291 – Ausgänge-Diagnose verständlicher

- `/outputs`: deaktivierte Ausgänge werden in der aktuellen Diagnose mit Status
  `deaktiviert` angezeigt und nicht mehr als normale Live-Last interpretiert.
- Zusammenfassung getrennt in vorgesehene Ausgänge, aktive Ausgänge und
  Hardware-Maximum; `5 / 8` ist dadurch nicht mehr missverständlich.
- Kachel `Manuelle Tests` als aktive manuelle Tests benannt.

## 0.0.290 – klc_16m_w: WLAN parallel zu Ethernet, getrennte Updatepfade

- **WLAN-Vollausbau für klc_16m_w:** Neues Modul `klc_wifi` (nur bei
  `KLC_WIFI_BACKEND_ACTIVE` kompiliert): WLAN Client (wlan0) mit
  nicht-blockierender Zustandsmaschine + Backoff, optionaler Access Point (ap0,
  Servicezugang, SSID automatisch `KLC-xxxxxx`), echtes **AP+STA parallel**
  (Arduino-Pico-Core, zweite CYW43-Instanz mit eigenem DHCP-Server).
  Ethernet (eth0) und WLAN laufen auf demselben lwIP-Stack — die WebUI ist über
  alle aktiven Interfaces erreichbar. LED (PIO+DMA) und KNX/BAOS bleiben von
  WLAN-Ausfällen unberührt.
- **Ausgehende Priorität:** Default-Route wird nach Konfiguration gesteuert
  (Automatisch/Ethernet/WLAN, Standard: Ethernet zuerst); Failover auf WLAN,
  wenn Ethernet ausfällt. Anzeige „Ausgehende Route" in WebUI/JSON.
- **Netzwerkseite je Interface:** getrennte Statusblöcke eth0/wlan0/ap0
  (Link, IP, MAC, SSID, RSSI, Clients, Modus); WLAN-Formular nur beim W-Build,
  klc_16m zeigt keine WLAN-Felder. `/api/network` liefert `interfaces{}`.
- **Passwortschutz:** WLAN-/AP-Passwörter werden nie im HTML angezeigt und
  fehlen im Standard-Export (`*_password_set`-Flag statt Klartext). Leeres
  Feld/Import behält das gespeicherte Passwort. **Schema 52 → 53** (neue
  `wifi`-Sektion, alte Konfigurationen laden unverändert mit Defaults).
- **Build-Targets & Updatepfade strikt getrennt:** `KLC_BUILD_TARGET`
  (klc_16m/klc_16m_w) und `KLC_RELEASE_PRODUCT` (klc/klc_w). klc_16m_w
  released jetzt unter `firmware/klc_w/klc_16m_w/…`; klc_16m bleibt unverändert
  (Bestandsgeräte). Manifest-/Dateinamens-/Board-/Flash-Prüfungen verhindern
  weiterhin Fremd-Updates. WebUI zeigt Build-Target, Produkt, Board, Flash,
  Update-Kanal.
- **Build:** Für das Waveshare RP2350B-Plus-W existiert im Core 5.6.1 noch
  keine Board-Definition; der W-Build nutzt übergangsweise die Variante
  **Pimoroni PicoPlus2W** (gleicher RP2350B, 16 MB, CYW43). Das W-Pinprofil
  bleibt bewusst als geprüfter Platzhalter markiert, bis das echte
  Waveshare-Pinout vorliegt. Hardware-Test von AP+STA-Parallelbetrieb steht
  noch aus.

## 0.0.289 – Mehrsprachigkeit vervollständigt, Update-Dialoge repariert

- **Bugfix Update-Seite:** In `/update.js` standen `\n`-Escapes als echte
  Zeilenumbrüche in JS-Strings → SyntaxError, alle Bestätigungsdialoge
  (Beta-Schalter, Auto-Install, Installation, Upload), Dateiauswahl,
  Versionsliste und Auto-Reload waren wirkungslos. Behoben (`\\n`).
- **i18n-Vervollständigung:** ~420 neue Wörterbucheinträge (737 → 1160) in
  EN/FR/ES/IT/NL/PL: KNX-Tooltips/Labels, OTA-/Update-Statusmeldungen,
  System/Netzwerk/Backup/LED/Power/Vorschau, BAOS-Diagnose, Selfcheck,
  Storage-Migrationstexte.
- **Dialoge übersetzbar:** globale `klcT()`-Brücke in allen `i18n.js`-Varianten;
  alle confirm/alert-Stellen übersetzt; `document.title` wird mitübersetzt;
  `i18n.js` auch auf den Upload-Ergebnisseiten; `&uuml;`-Inkonsistenz behoben.
- Bewusst deutsch bleiben: Serial-Log-Zeilen, RAM-Notfallseite (ohne JS),
  Sprach-Eigennamen im Sprachmenü, rein technische Begriffe.

## 0.0.254 – Tetris-Performance: Build-Dauer memoisiert

- **Build-Dauer in einem Durchlauf:** `klcScenesTetrisBuildDurationMs` lief vorher
  über `BlockCount` **plus** einen zweiten Plan-Walk. Jetzt ein einziger Durchlauf
  (`…Compute`), die echte Blockanzahl wird nicht mehr separat gebraucht.
- **Memoisierung:** Die Build-Dauer wird deterministisch gecacht (Schlüssel: Szene,
  LED-Anzahl, Zyklus, Phase, Sync-Salt + Konfig-Generation). Der Cache wird bei
  `klcScenesBegin`/`klcScenesApplyConfigUpdate` über `g_tetris_plan_gen` entwertet,
  kann also nie veraltete Zyklenlängen liefern.
- **Wirkung:** Der Dispatch berechnete die Dauer 2×/Frame, der **Segment-Pfad sogar
  ~4× pro Pixel** — das ist jetzt ein Cache-Treffer. Zusammen mit dem block-weisen
  Abbau (0.0.253) bleibt jeder Frame CPU-leicht, sodass die Tetris-Clock nicht mehr
  in Zeitlupe gerät. Web-Vorschau nutzt denselben linearen Durchlauf.
- Keine Schemaänderung (Schema 50).

## 0.0.253 – Tetris-Performance: flüssiger Abbau

- **Abbau block-weise statt pro Pixel:** `klcScenesRenderTetrisFrame` zeichnet den
  Teardown jetzt in **einem** Plan-Durchlauf (O(Blöcke + LEDs)). Vorher lief er pro
  Pixel über `sampleOrder`; der Default **Original-Abbau** war dabei sogar
  **O(LEDs × Blöcke²)** (jeder Block per `BlockByIndex` nachgeschlagen). Bei vielen
  Blöcken kostete ein Teardown-Frame zig Millisekunden.
- **Symptom behoben:** Dadurch wurden echte Frames langsam → die Tetris-Clock
  deckelte den Zeitfortschritt → die Animation **ruckelte** und lief bei schneller
  Einstellung in **Zeitlupe (träge)**. Jetzt bleiben Aufbau- und Abbau-Frame
  CPU-leicht; die Animation läuft flüssig und „schnell" ist wirklich schnell.
- `klcScenesTetrisOriginalDropAlpha` (Segment-Pfad) und die Web-Vorschau nutzen
  ebenfalls den linearen Inline-Durchlauf. Kein einzelner Block-Index-Zugriff mehr.
- Keine Schemaänderung (Schema 50, Logik aus 0.0.252 unverändert).

## 0.0.252 – Tetris-Animation neu entwickelt (Schema 50)

Die Tetris-Animation wurde nicht weiter geflickt, sondern als saubere LED-Aufbau-/
Abbauanimation mit fallenden Blöcken neu aufgebaut. Der separate WLED-Effekt
**Tetrix** ist davon nicht betroffen.

- **Plan-/blockbasiert:** Pro Zyklus wird aus einem Seed deterministisch eine
  Blockliste abgeleitet (Größe, Farbe, Fallzeit, Pause, Frühstart). Stabil über den
  ganzen Zyklus, kein Neuwürfeln im Frame, kein Springen. Web-Vorschau spiegelt
  exakt dieselben Funktionen.
- **Aufbau:** Blöcke starten außerhalb der Leiste, fallen sichtbar herein, landen und
  bleiben liegen – **lückenlos** (Einstellung *Abstand* entfernt).
- **Zufall mit Anti-Wiederholung:** Blockgrößen, Fallzeiten und Pausen meiden den
  jeweils ähnlichen Vorgängerwert. Zufallsfarben sind frei aus **HSV** mit
  einstellbarem Mindestabstand (kein Fast-Weiß, gut unterscheidbar).
- **Richtung:** nur noch `1 → X` / `X → 1` plus neue Option **Richtung wechseln**
  (kippt je Zyklus). *Mitte spiegeln* und *Richtung zufällig* entfernt.
- **Nächsten einblenden** (nur Aufbau, Variante A): der nächste Block gleitet schon
  sichtbar von außen herein und wartet, bis er fällt.
- **Gelegentlicher Frühstart:** der nächste Block startet manchmal vor der Landung
  des vorherigen (kurz zwei Blöcke unterwegs, kein Dauerfeuer).
- **6 Abbauarten:** Fade Out, Original-Abbau (blockweises Herausfallen in Aufbau-
  Reihenfolge/-Richtung), Weiterrollen, Ausperlen, Abwechselnd, Zufällig.
- **Alle Kanäle/Segmente synchron:** gleicher Plan auf allen Ausgängen; der Haken
  wird ausgeblendet, wenn aktive Ausgänge unterschiedliche LED-Anzahl haben
  (gespeicherter Wert bleibt erhalten).
- **Feinwerte** (Blockgröße min/max, Pause min/max, Fallzeit min/max, Frühstart-%,
  HSV-Mindestabstand) nur noch in der **Experten/Admin-Ansicht**; Werte bleiben
  gespeichert und wirksam. Fallzeit ist klar als **ms pro LED** definiert.
- **Migration (Schema 49 → 50):** alte Abbauart-Slugs werden gemappt
  (`reverse`/`random_drop` → Original-Abbau, `wipe` → Weiterrollen,
  `soft_dissolve` → Ausperlen), entfernte Optionen werden ignoriert; additive
  Felder sind rückwärtskompatibel.

## 0.0.235 – UI- & Effekt-Feinschliff

- **System → Darstellung:** Das Statusfeld zeigt jetzt „Admin eingeloggt – alles
  sichtbar", wenn der Admin angemeldet ist (vorher blieb es bei „Einfache
  Ansicht"). Aktualisiert sich beim Seitenaufbau.
- **Ablauf-Editor:** Schieberegler sind wieder rund — die Thumb-Form ist jetzt
  robust (`border-radius:50% !important`), falls eine andere Regel sie eckig
  überlagerte.
- **Feuerwerk:** Die beiden Slider (Geschwindigkeit, Intensität) sind entfernt.
  Die optimale Einstellung ist fest hinterlegt: Geschwindigkeit 0 (ruhiger Takt)
  und maximale Funkenzahl. Renderer und Vorschau ignorieren die alten Felder.
- **Tetrix-Geschwindigkeit 0:** ergibt jetzt eine **Zufallsgeschwindigkeit je
  Block** (deterministisch, analog zu Breite 0 = Zufallsgröße) statt immer der
  langsamsten Geschwindigkeit. Engine + Vorschau angepasst.
- **Tetris-Fallfarbe:** Ein fallender Block behält jetzt auch auf **segmentierten
  Strings** seine eigene Farbe. Vorher übernahm er im Segment-Renderpfad die
  Farbe der Position, durch die er gerade fiel (neuer Helfer
  `klcScenesTetrisActiveBlockIndex`). Der Nicht-Segment-Pfad war bereits korrekt.
- **Tetris-Teardown „zufällig rausfallen":** Blöcke rutschen jetzt mit
  zufälligem Startzeitpunkt **und Zufallstempo** komplett aus der Leiste heraus
  (echte Fallbewegung statt Ausblenden am Platz). Die Leiste ist bei
  Fortschritt 1 garantiert leer, sodass der Füll-Zyklus sauber neu starten kann.
  Neuer deterministischer Sampler `klcScenesTetrisRandomDropSample`, eingebunden
  in beide Render-Pfade (Segment + Nicht-Segment) und die Vorschau.
- Keine Schemaänderung (Schema 49).

## 0.0.234 – Beta-Update-Schnellbutton zieht letzte Beta (1 Klick)

- Der Beta-Schnellbutton oben rechts (nur Admin) macht jetzt mit **einem Klick**:
  Beta-Manifest prüfen → die genannte BIN herunterladen → installieren → Neustart.
- **Force-Install:** Die letzte Beta wird auch dann gezogen, wenn die Version
  gleich ist (Entwicklungs-Workflow). Kein separates „nach Update suchen →
  installieren" mehr nötig.
- Technisch: `klcOtaInstallManifestUpdate(bool force)` überspringt bei `force`
  nur die „neuer?"-Prüfung; die Profil-/Sicherheitsprüfung (Firmwarekennung,
  Board-ID, Flashgröße) im Manifest-Check bleibt aktiv.
- Zu **Fehler 1420**: bedeutet „update.json ohne `firmware`-Feld" und ist ein
  veralteter `last_error`, sobald der Check wieder erfolgreich ist (er wird beim
  erfolgreichen Download auf 0 zurückgesetzt). Die Server-`update.json` muss das
  Feld `"firmware"` mit der Firmwarekennung dieses Geräts enthalten.
- Keine Schemaänderung (Schema 49).

## 0.0.233 – Versatz „Segmente oder Kanäle" (Feature ④)

- Der **Stopp-Versatz** (`string_segment_stop_delay_ms`) gilt jetzt automatisch:
  hat ein Ausgang String-Segmente → je Segment (wie bisher); hat er keine
  Segmente → er staffelt die **Kanäle** (Ausgänge) beim Ausschalten. Jeder
  Ausgang wartet seinen Kanal-Versatz ab, spielt den Aus-Effekt und geht aus.
- Der **Start-Versatz** (`global_delay_ms`) staffelt die Kanäle beim Einschalten
  wie bisher.
- Bei Versatz **0** bleibt das bisherige **Sofort-Aus** für nicht-segmentierte
  Ausgänge erhalten (rückwärtskompatibel).
- Engine: `klcScenesBeginStringSegmentStop` für nicht-segmentierte Ausgänge
  verallgemeinert; `klcScenesRenderOutput` wertet jetzt `rt.stopping` aus
  (Kanal-Versatz → Aus-Effekt → aus). Segmentierte Ausgänge unverändert über
  `klcScenesRenderStringSegments`.
- UI-Labels präzisiert: „Start-Versatz je Kanal ms" und „Stopp-Versatz je
  Segment/Kanal ms".
- Keine Schemaänderung (Schema 49) – es kamen keine neuen Konfigurationsfelder
  hinzu.

## 0.0.232 – Feuerwerk/Tetrix als Start-/Aus-Effekt (Feature ③)

- Start- und Aus-Dropdown haben jetzt denselben Effektumfang wie „Ein": **Feuerwerk**
  und **Tetrix (WLED)** sind als Phasen-Effekte verfügbar (neue
  `KLC_SCENE_PHASE_FIREWORKS=25` / `KLC_SCENE_PHASE_TETRIX=26`).
- Gerendert über dieselben Renderer wie der „Ein"-Effekt (`klcScenesPhaseAlpha`
  ruft `klcScenesFireworksAlpha`/`klcScenesTetrixAlpha`), mit Farb-Override für
  Phase 1/3 in beiden Render-Pfaden (Segment- und Nicht-Segment-Ausgänge). Die
  Slider Geschwindigkeit/Intensität bzw. Geschwindigkeit/Blockbreite gelten wie
  beim Haupteffekt.
- Canvas-Vorschau (`stateStart`/`stateEnd`) zeigt die neuen Phasen; Auto-Dauer
  greift, wenn Start-/Aus-Dauer 0 ist.
- Backup/Restore: Phasen-Slugs `fireworks`/`tetrix` ergänzt; Bounds in
  `klc_web_server.cpp` und `klc_config.cpp` auf `KLC_SCENE_PHASE_TETRIX` erweitert.
- **Code-Hinweis** (klc_config.h): Erweiterungen am „Ein"-Effekt immer auch bei
  Start und Aus sowie in den Dropdowns/Bounds nachziehen.
- Konfig-Schema **49**.

## 0.0.231 – 3 Ansichten User/Experte/Admin (Feature ①)

- Neuer Schalter **„Erweiterte Ansicht"** unter *System → Darstellung* (neben
  Dark-Mode), gespeichert in der Gerätekonfiguration nach demselben Muster wie
  `ui.dark_mode` (`ui.advanced_view`, Toggle-Route `/ui/view`).
- **AUS = User:** nur Dashboard + vereinfachter Ablauf-Editor (Farbe, Effekt,
  Slider Geschwindigkeit/Intensität). Keine Start-/Aus-Phasen, kein Versatz,
  keine Experten-Parameter; Experten-Tabs (LED/Steuerung/KNX) und die
  System-Kacheln sind ausgeblendet.
- **AN = Experte:** heutige Vollansicht (ohne Admin).
- **Admin** (bestehender Admin-Login) zeigt unverändert alles.
- Umsetzung per CSS-Klasse `.klc-adv`: Die Ausblend-Regel wird **live je
  Seitenaufbau** (Nav bzw. Editor-iframe) gesendet, ist also unabhängig vom
  `/ui.css`-Cache und greift sofort beim Umschalten. Ausgeblendete Felder
  bleiben im DOM und werden weiter gespeichert — gespeicherte Experten-Werte
  gehen nicht verloren.
- Konfig-Schema **48** (neues Feld `ui.advanced_view`; alte Backups werden
  rückwärtskompatibel auf den Default *einfache Ansicht* migriert).

## 0.0.230 – Tetrix: Blockbreite 0 = zufällige Blockgrößen

- Tetrix: Blockbreite-Regler auf 0 erzeugt jetzt **zufällige Blockgrößen je
  Block** (2–8 Pixel); Werte > 0 ergeben weiterhin eine feste Blockgröße.

## 0.0.229 – Slider zuverlässig (cache-fest, inline)

- Root-Cause: Die Slider wurden über `/flow/edit.js` erzeugt — diese Datei kann
  der Browser aus einer älteren Firmware noch zwischengespeichert haben (ohne den
  Slider-Code), daher „keine Schieber".
- Fix: Das Sliderize läuft jetzt **inline in der frischen (no-store)
  Editor-Seite**, also unabhängig vom JS-Cache. Zusätzlich `?v=Version` als
  Cachebuster an `/flow/edit.js`.
- Damit erscheinen Slider für Farbe (R/G/B/W), Helligkeit,
  Geschwindigkeit/Intensität (Feuerwerk/Tetrix), Füllgrad, Funkeln, Tetris u. a.;
  das Zahlenfeld bleibt für die exakte Eingabe daneben.

## 0.0.228 – Slider sichtbar + Farb-/Helligkeits-Slider

- Fix: Die generische Eingabefeld-CSS (Rahmen/Hintergrund/Padding) machte die
  Schieberegler unsichtbar (dunkles Kästchen statt Regler). Range-Felder haben
  jetzt eigenes Styling und werden korrekt als Slider dargestellt — auch bei
  Feuerwerk und Tetrix.
- Zusätzlich Slider für Farbe (R/G/B/W) und Helligkeit; der Slider erscheint
  neben dem Zahlenfeld, die genaue Eingabe bleibt möglich.
- Hinweis: Der Umschalter User/Experte (3 Ansichten, Feature ①) ist noch nicht
  gebaut und als nächstes geplant.

## 0.0.227 – Einheitliche Slider für alle Animationen

- Alle Animations-Parameter im Ablauf-Editor bekommen denselben Schieberegler-
  Look wie Feuerwerk/Tetrix: Füllgrad, Segment, Nachleuchten, Schrittzeit,
  Funkeln (Tempo/Füllgrad/Lebenszeit) und Tetris (Gruppe/Abstand/Pause/Schritt).
  Das Zahlenfeld bleibt zusätzlich für die exakte Eingabe erhalten.
- Umsetzung rein clientseitig (JS „Sliderize"), daher kein Einfluss auf
  Speicherung, Server-Templates oder Backups.
- Pulsperiode und Dauer bleiben reine Zahlenfelder (zu großer Wertebereich für
  einen sinnvollen Slider).
- Ablauf-Zeile zeigt jetzt kontextabhängig „kein Start-Effekt" bzw.
  „kein Aus-Effekt".
- „Globale Effekt-Einstellungen": Feuerwerk- und Tetrix-Regler werden jetzt
  serverseitig nur noch beim passenden Ein-Effekt eingeblendet (vorher konnten
  beide gleichzeitig sichtbar sein).

## 0.0.226 – Feuerwerk & Tetrix: eigene WLED-Slider (Geschwindigkeit + Intensität)

- Beide Effekte haben jetzt **zwei eigene Schieberegler (0–255)** im Ablauf-
  Editor, analog zu WLED:
  - Feuerwerk: **Geschwindigkeit** (höher = schnellerer Takt und kürzere
    Explosion) und **Intensität** (Anzahl/Dichte der Funken).
  - Tetrix: **Geschwindigkeit** (Falltempo) und **Blockbreite**.
- Das Feuerwerk ist dadurch standardmäßig deutlich **ruhiger/langsamer** (Standard
  Geschwindigkeit 90 → Explosion dauert mehrere Sekunden statt Sekundenbruchteile).
- Die bisherige Feld-Wiederverwendung (Pulsperiode / Funkel-Lebenszeit /
  Schrittzeit) entfällt für diese Effekte; die Geschwindigkeitsregler ersetzen sie.
- Neue Konfigfelder `fireworks_speed`, `fireworks_intensity`, `tetrix_speed`,
  `tetrix_width`. Konfigurationsschema → 47; ältere Backups laden mit Standardwerten.

## 0.0.225 – Feuerwerk: weicheres, gestaffeltes Ausklingen

- Jede Funke hat jetzt eine eigene, zufällige Lebensdauer (~50–100 % der
  eingestellten Funkel-Lebenszeit) und ein weicheres, länger nachleuchtendes
  Ausklingen. Eine Explosion erlischt dadurch nicht mehr schlagartig — die
  Pixel gehen nacheinander/zufällig aus.
- Stellschrauben: „Ein-Funkel-Lebenszeit" = Nachleuchtdauer, „Ein-Pulsperiode"
  = Abstand zwischen den Explosionen.
- Hinweis Tetrix: Geschwindigkeit über „Ein-Schrittzeit" (ms pro Pixel,
  kleiner = schneller).
- Canvas-Vorschau an die neue Funkenlogik angepasst (Vorschau = Hardware).

## 0.0.224 – Effekte Feuerwerk & Tetrix

- Neuer Haupteffekt **Feuerwerk** (WLED-inspiriert): aufsteigende Rakete, die in
  bunte Funken explodiert. Deterministisch über eine geschlossene Wurfparabel
  berechnet (kein persistenter Partikelspeicher), läuft über die bestehende
  Szenen-Engine inklusive String-Segmenten und Canvas-Vorschau.
- Neuer Haupteffekt **Tetrix** (WLED-Stil): einzelne Blöcke fallen nacheinander
  herab und stapeln sich zur vollen Leiste, danach kurzer Halt und Neustart;
  jeder Block in optionaler Zufallsfarbe.
- Beide Effekte nutzen vorhandene Szenenfelder weiter (keine neuen Konfigfelder):
  - Feuerwerk: Ein-Pulsperiode = Takt zwischen Explosionen,
    Ein-Funkel-Lebenszeit = Funkendauer, Ein-Füllgrad = Funkenzahl.
  - Tetrix: Tetris-Gruppe min/max = Blockgröße, Ein-Schrittzeit = Falltempo,
    Tetris-Zufallsfarben = Farbe je Block.
- Helligkeit folgt der Szenenfarbe (schwarze Farbe = aus).
- Konfigurationsschema unverändert (46); bestehende Backups bleiben kompatibel.

## 0.0.2 – Arduino-Startstruktur

- Firmware auf Arduino-Sketch-Struktur umgestellt.
- `firmware/klc_main/klc_main.ino` ergänzt.
- Alle `klc_*.h/.cpp` in den Sketch-Ordner gelegt.
- Arduino-Hinweise ergänzt.
- Dummy-Module für Konfiguration, Storage, LED, Power, KNX, Ethernet, Web, OTA und Diagnose angelegt.

## 0.0.1 – Grundstruktur

- Erste Projektstruktur.
- `klc_` als Dateipräfix festgelegt.
- Hardwareziel dokumentiert.
