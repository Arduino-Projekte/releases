#pragma once

#include "klc_config.h"
#include <Arduino.h>

// Persistentes Szenenformat KLS1, Payload-Schema 1. Jede konfigurierbare Szene
// besitzt zwei unabhaengige A/B-Slots. Die Runtime arbeitet ausschliesslich
// mit g_config; dieses Modul koordiniert nur deren dauerhafte Abbilder.
inline constexpr uint16_t KLC_SCENE_STORE_SCHEMA = 1U;
inline constexpr uint8_t KLC_SCENE_STORE_QUEUE_CAPACITY = 2U;

enum KlcSceneStoreState : uint8_t {
  KLC_SCENE_STORE_IDLE = 0,
  KLC_SCENE_STORE_QUEUED,
  KLC_SCENE_STORE_RUNTIME_APPLY,
  KLC_SCENE_STORE_PREPARE,
  KLC_SCENE_STORE_OPEN,
  KLC_SCENE_STORE_WRITE_HEADER,
  KLC_SCENE_STORE_WRITE_PAYLOAD,
  KLC_SCENE_STORE_FLUSH,
  KLC_SCENE_STORE_CLOSE,
  KLC_SCENE_STORE_VERIFY_OPEN,
  KLC_SCENE_STORE_VERIFY_HEADER,
  KLC_SCENE_STORE_VERIFY_PAYLOAD,
  KLC_SCENE_STORE_COMMIT,
  KLC_SCENE_STORE_DONE,
  KLC_SCENE_STORE_FAILED,
  KLC_SCENE_STORE_SUPERSEDED
};

enum KlcSceneStoreFailurePhase : uint8_t {
  KLC_SCENE_FAILURE_NONE = 0,
  KLC_SCENE_FAILURE_RUNTIME_APPLY,
  KLC_SCENE_FAILURE_RUNTIME_ROLLBACK,
  KLC_SCENE_FAILURE_STORAGE_OPEN,
  KLC_SCENE_FAILURE_STORAGE_WRITE,
  KLC_SCENE_FAILURE_STORAGE_VERIFY,
  KLC_SCENE_FAILURE_COMMIT
};

struct KlcSceneStoreStatus {
  bool initialized;
  bool busy;
  bool ram_only;
  uint8_t scene_id;
  uint8_t slot;
  uint8_t queue_depth;
  KlcSceneStoreState state;
  uint64_t operation_id;
  uint32_t operation_revision;
  uint32_t accepted_revision;
  uint32_t applied_revision;
  uint32_t current_scene_revision;
  uint32_t ram_revision;
  uint32_t persisted_revision;
  uint32_t storage_sequence;
  uint32_t bytes_written;
  uint32_t bytes_read;
  uint32_t started_ms;
  uint32_t completed_ms;
  uint32_t max_step_us;
  uint32_t free_heap_start;
  uint32_t free_heap_end;
  uint32_t littlefs_total_bytes;
  uint32_t littlefs_used_bytes;
  uint32_t recovery_count;
  bool runtime_applied;
  bool rollback_failed;
  KlcSceneStoreFailurePhase failure_phase;
  char error[128];
};

struct KlcScenePersistenceStatus {
  uint8_t scene_id;
  uint8_t active_slot;
  bool dirty;
  bool ram_only;
  bool queued;
  bool slot_a_valid;
  bool slot_b_valid;
  uint32_t ram_revision;
  uint32_t accepted_revision;
  uint32_t applied_revision;
  uint32_t persisted_revision;
  uint32_t active_sequence;
  uint64_t active_operation_id;
  uint64_t last_success_operation_id;
  uint32_t last_success_timestamp;
  uint32_t last_error_code;
  char last_error_text[96];
  char last_recovery_reason[96];
};

struct KlcSceneReplacementStatus {
  bool found;
  uint8_t state;
  uint8_t last_confirmed_scene;
  uint8_t failed_scene;
  uint64_t operation_id;
  uint32_t error_code;
  uint32_t config_generation;
  bool main_config_activated;
  bool kls_generation_committed;
  bool completed;
  char fallback_source[24];
};

// Boot: bei vorhandener Markierung A/B laden; andernfalls die Szenen der
// bereits validierten Legacy-JSON-Konfiguration einmalig migrieren.
bool klcSceneStoreBegin(KlcDeviceConfig& cfg,
                        bool loaded_config_is_authoritative = false);
bool klcSceneStoreFinalizeActivatedConfig(KlcDeviceConfig& cfg,
                                          const char* fallback_source);
void klcSceneStoreTick();

// Enqueue prueft die Browserrevision, fasst noch nicht gestartete Auftraege
// derselben Szene zusammen und liefert eine eindeutige Vorgangsnummer.
bool klcSceneStoreEnqueue(uint8_t scene_id, const KlcSceneConfig& scene,
                          uint32_t expected_revision,
                          uint64_t& operation_id,
                          uint32_t& new_revision,
                          char* message, size_t message_len);

uint32_t klcSceneStoreRevision(uint8_t scene_id);
uint32_t klcSceneStorePersistedRevision(uint8_t scene_id);
bool klcSceneStoreGetStatus(uint64_t operation_id,
                            KlcSceneStoreStatus& status);
bool klcSceneStoreGetSceneStatus(uint8_t scene_id,
                                 KlcScenePersistenceStatus& status);
bool klcSceneStoreGetReplacementStatus(uint64_t operation_id,
                                       KlcSceneReplacementStatus& status);
uint64_t klcSceneStoreReplacementOperationId();
const KlcSceneStoreStatus& klcSceneStoreGetLastStatus();
const char* klcSceneStoreStateText(KlcSceneStoreState state);
const char* klcSceneStoreFailurePhaseText(KlcSceneStoreFailurePhase phase);
uint32_t klcSceneStoreBootGeneration();
bool klcSceneStoreIsBusy();
bool klcSceneStoreHasDirtyScenes();
bool klcSceneStoreSceneDirty(uint8_t scene_id);

// Vollimport und Werksreset entfernen nur die Autoritaetsmarkierung. Die alten
// A/B-Slots bleiben bis zum bestaetigten Hauptconfig-Commit als Rueckfallebene
// erhalten; beim folgenden Boot werden alle Szenen aus dessen JSON migriert.
bool klcSceneStoreInvalidateForMigration();
// Stellt bei einem fehlgeschlagenen Vollconfig-Commit die bisherige A/B-
// Autoritaet wieder her. Die Slots wurden beim Invalidieren nicht geloescht.
bool klcSceneStoreRestoreAuthorityMarker();
// Bereitet einen Vollimport/Werksreset transaktional vor. Die bisherige
// KLS-Autoritaet bleibt dabei bis zum verifizierten Abschluss erhalten.
bool klcSceneStorePrepareConfigReplacement(const KlcDeviceConfig& candidate,
                                           char* message, size_t message_len);
void klcSceneStoreCancelConfigReplacement();
bool klcSceneStoreConfirmConfigReplacement();
bool klcSceneStoreRemoveAll();

// Testbare, endian-feste Codec-Helfer. Kein Struct-Dump, kein Padding.
bool klcSceneStoreEncodePayload(const KlcSceneConfig& scene,
                                uint8_t* out, size_t out_len,
                                uint16_t& payload_len);
bool klcSceneStoreDecodePayload(uint8_t scene_id,
                                const uint8_t* payload,
                                uint16_t payload_len,
                                KlcSceneConfig& scene);
uint32_t klcSceneStoreCrc32(const uint8_t* data, size_t length,
                            uint32_t seed = 0U);
bool klcSceneStoreSequenceNewer(uint32_t candidate, uint32_t reference);
