#pragma once
#include "klc_config.h"
#include "klc_stream_writer.h"
#include <stddef.h>


enum KlcStorageConfigFileState : uint8_t {
  KLC_STORAGE_CONFIG_FILE_UNKNOWN = 0,
  KLC_STORAGE_CONFIG_FILE_VALID,
  KLC_STORAGE_CONFIG_FILE_MISSING,
  KLC_STORAGE_CONFIG_FILE_SYNTAX_INVALID,
  KLC_STORAGE_CONFIG_FILE_SEMANTIC_INVALID,
  KLC_STORAGE_CONFIG_FILE_READ_ERROR
};

enum KlcStorageConfigSource : uint8_t {
  KLC_STORAGE_CONFIG_SOURCE_NONE = 0,
  KLC_STORAGE_CONFIG_SOURCE_ACTIVE,
  KLC_STORAGE_CONFIG_SOURCE_PREVIOUS,
  KLC_STORAGE_CONFIG_SOURCE_DEFAULTS
};

struct KlcStorageRecoveryStatus {
  KlcStorageConfigSource source;
  KlcStorageConfigFileState active_state;
  KlcStorageConfigFileState previous_state;
  bool recovery_active;
  bool fresh_defaults;
  bool interrupted_commit_detected;
  char active_error[96];
  char previous_error[96];
};

bool klcStorageBegin();
bool klcStorageIsReady();
const char* klcStorageGetConfigPath();
const char* klcStorageGetLastError();
bool klcStorageConfigExists();
size_t klcStorageGetConfigSize();
bool klcStorageValidateJson(const char* json);
uint16_t klcStorageGetLastSchemaVersion();
bool klcStorageWasLastMigrationApplied();
const char* klcStorageGetLastMigrationMessage();

bool klcStorageLoadConfig(KlcDeviceConfig& cfg);
// Startpfad fuer die komplette Konfiguration: aktive Datei vollstaendig
// validieren, bei Fehler /klc_config_prev.json pruefen und nur im RAM nutzen.
// Eine Rettungskopie wird dabei nie automatisch zur aktiven Datei gemacht.
bool klcStorageLoadConfigWithRecovery(KlcDeviceConfig& cfg);
bool klcStorageLoadPreviousConfig(KlcDeviceConfig& cfg);
bool klcStoragePreviousConfigExists();
const KlcStorageRecoveryStatus& klcStorageGetRecoveryStatus();
const char* klcStorageConfigFileStateText(KlcStorageConfigFileState state);
const char* klcStorageConfigSourceText(KlcStorageConfigSource source);
// Bewusste Benutzeraktion: validierte Rettungskopie atomar als neue aktive
// Pending-Konfiguration uebernehmen, ohne sie zuvor mit der defekten aktiven
// Datei zu ueberschreiben.
bool klcStorageAdoptPreviousAsPending(KlcDeviceConfig& adopted_cfg);
bool klcStorageLoadLastKnownGood(KlcDeviceConfig& cfg);
bool klcStorageLastKnownGoodExists();
// Byteidentitaet der vollstaendig geschriebenen JSON-Dateien. Der Hash ist
// derselbe begrenzte FNV-1a-Pfad, den die vorhandenen kleinen Journale nutzen.
bool klcStorageGetConfigIdentity(uint32_t& size, uint32_t& hash);
bool klcStorageGetLastKnownGoodIdentity(uint32_t& size, uint32_t& hash);
// Erst nach vollstaendig erfolgreicher LED-Aktivierung aufrufen. Kopiert die
// bereits atomar gespeicherte und rueckgelesene Hauptkonfiguration in das
// eigene atomare Last-Known-Good-Journal.
bool klcStoragePromoteCurrentToLastKnownGood(uint32_t writer_token = 0U);
bool klcStorageSaveConfig(const KlcDeviceConfig& cfg);
// true nur, wenn der letzte erfolgreiche SaveConfig-Aufruf die Hauptdatei
// tatsaechlich ersetzt hat. Ein byteidentischer, bereits dauerhafter Stand
// gilt als erfolgreicher Commit, verursacht aber keinen Flash-Schreibvorgang.
bool klcStorageLastSaveWrote();
// Speichert einen vollstaendig validierten Kandidaten atomar und markiert
// dessen Dateiidentitaet zweiphasig als noch zu aktivierendes Pending. Die
// Hauptdatei wird dadurch ausdruecklich noch nicht zu Last-Known-Good.
bool klcStorageSaveConfigAsPending(const KlcDeviceConfig& cfg,
                                   bool recovery_override = false);
// Expliziter Recovery-Import: wie SaveConfigAsPending, aber die bereits
// validierte /klc_config_prev.json wird beim Commit nicht durch eine defekte
// aktive Datei ersetzt.
bool klcStorageSaveConfigAsRecoveryPending(const KlcDeviceConfig& cfg);
// Sperrt alle regulaeren Schreibpfade fuer die Hauptkonfiguration. Der
// Recovery-Schreibpfad bleibt ausschliesslich fuer explizite, vom Benutzer
// ausgeloeste Import-/Reset-Aktionen verfuegbar.
void klcStorageSetConfigWriteLocked(bool locked);
bool klcStorageIsConfigWriteLocked();
bool klcStorageSaveConfigRecoveryOverride(const KlcDeviceConfig& cfg);
// Persistiert nur die explizite UI-Sprache in einer kleinen atomaren
// Auswahl-Datei. Das vermeidet beim Sprachpaketwechsel ein erneutes Schreiben
// der kompletten grossen Hauptkonfiguration, waehrend .tmp/.bak-Pakete liegen.
bool klcStorageSaveUiLanguage(const char* language);
bool klcStorageClearUiLanguageSelection();
// Theme/Ansicht werden geraeteweit in einem kleinen atomaren Journal statt
// durch einen Vollwrite der Hauptkonfiguration gespeichert. Queue buendelt
// schnelle Aenderungen; Tick fuehrt den Write nach 1,5 s Ruhe aus.
// Explizite Web-POSTs verwenden CommitUiState, damit g_config erst nach der
// bestaetigten atomaren Journalaktivierung geaendert wird.
bool klcStorageCommitUiState(bool dark_mode, bool advanced_view);
bool klcStorageQueueUiState(bool dark_mode, bool advanced_view);
void klcStorageUiStateTick();
bool klcStorageExportJson(const KlcDeviceConfig& cfg, char* buffer, size_t buffer_len);
bool klcStorageMeasureExportJson(const KlcDeviceConfig& cfg,
                                 size_t& json_length);
bool klcStorageExportJsonStream(const KlcDeviceConfig& cfg,
                                KlcStreamWriter& writer);
bool klcStorageImportJson(KlcDeviceConfig& cfg, const char* json);
