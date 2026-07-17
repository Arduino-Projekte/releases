#include "klc_web_server_internal.h"
#include "klc_setup_wizard_http_internal.h"
#include "klc_storage_coordinator.h"
#include "klc_scene_store.h"

// Besitzer von Upload-, Import- und Ergebniszustand des Setup-Backups.

static char g_setup_backup_import_token[33] = "";
static uint32_t g_setup_backup_import_completed_ms = 0;
static int g_setup_backup_import_status = 0;
static bool g_setup_backup_import_ok = false;
static char g_setup_backup_import_message[256] = "";
static KlcSetupBackupImportInfo g_setup_backup_import_info = {
  "en", 0, false, KLC_SETUP_BACKUP_PROFILE_NONE
};

// Der Setup-Import verwendet bewusst denselben blockweisen LittleFS-Weg wie
// der normale Backup-Import. Ein aktuelles Backup liegt bereits nahe am
// Storage-Limit; g_server.arg("plain") wuerde daneben mindestens eine weitere
// komplette String-Kopie im Heap halten und kann die HTTP-Antwort verlieren.
static constexpr size_t KLC_SETUP_BACKUP_IMPORT_MAX_BYTES = 240000U;
static const char KLC_SETUP_BACKUP_IMPORT_STAGE_PATH[] =
  "/klc-setup-backup-import.tmp";
static File g_setup_backup_import_file;
static constexpr char KLC_SETUP_BACKUP_JOB[] = "Setup-Backupimport";
static constexpr size_t KLC_SETUP_BACKUP_READ_SLICE_BYTES = 4096U;
static char g_setup_backup_upload_token[33] = "";
static size_t g_setup_backup_upload_bytes = 0;
static bool g_setup_backup_upload_complete = false;
static int g_setup_backup_upload_error_status = 0;
static char g_setup_backup_upload_error[256] = "";
static KlcStorageWriterToken g_setup_backup_writer_token = 0U;

static void klcWebServerSetupBackupReleaseWriter()
{
  if (g_setup_backup_writer_token == 0U) return;
  klcStorageWriterRelease(KLC_STORAGE_WRITER_SETUP_BACKUP,
                          g_setup_backup_writer_token);
  g_setup_backup_writer_token = 0U;
}
static String klcWebServerSetupBackupOperationToken()
{
  if (g_server.hasHeader(KLC_OPERATION_ID_HEADER_NAME)) {
    String token = g_server.header(KLC_OPERATION_ID_HEADER_NAME);
    token.trim();
    if (klcWebServerSetupOperationTokenValid(token.c_str())) return token;
  }
  if (g_server.hasArg("id")) {
    String token = g_server.arg("id");
    token.trim();
    return token;
  }
  return String("");
}

static const char* klcWebServerSetupBackupProfileAdjustmentName(
  KlcSetupBackupProfileAdjustment adjustment)
{
  switch (adjustment) {
    case KLC_SETUP_BACKUP_PROFILE_KEEP_TARGET_WIFI:
      return "keep_target_wifi";
    case KLC_SETUP_BACKUP_PROFILE_ADAPT_TO_LAN:
      return "adapt_to_lan";
    default:
      return "none";
  }
}

static void klcWebServerSendSetupBackupJsonResult(
  int status, bool ok, const char* message,
  const KlcSetupBackupImportInfo* import_info)
{
  if (!ok || import_info == nullptr) {
    klcWebServerSendSetupJsonResult(status, ok, message);
    return;
  }

  g_server.sendHeader("Cache-Control", "no-store");
  KlcWebResponseStreamGuard stream(status,
    "application/json; charset=utf-8", false);
  if (!stream.started()) return;
  // Erfolgsantworten liefern strukturierte Daten statt eines fest verdrahteten
  // deutschen Satzes. Die Offline-Wizard-UI setzt daraus die Meldung in der
  // Sprache des importierten Backups zusammen.
  (void)message;
  (void)klcWebServerWriteResponseText("{\"ok\":true,\"language\":");
  klcWebServerSendJsonStringValue(
    klcLanguageNormalize(import_info->language));
  klcWebServerSendContentFmt(
    ",\"schema_migrated\":%s,\"imported_schema\":%u,"
    "\"target_schema\":%u,\"profile_adjustment\":",
    import_info->schema_migrated ? "true" : "false",
    (unsigned)import_info->imported_schema,
    (unsigned)KLC_CONFIG_SCHEMA_VERSION);
  klcWebServerSendJsonStringValue(
    klcWebServerSetupBackupProfileAdjustmentName(
      import_info->profile_adjustment));
  klcWebServerSendContentFmt(",\"restarting\":true,\"scene_replacement_operation_id\":\"%llu\"}",
    (unsigned long long)klcSceneStoreReplacementOperationId());
  (void)stream.finish();
}

static void klcWebServerSetupBackupUploadReset(bool remove_stage)
{
  if (g_setup_backup_import_file) g_setup_backup_import_file.close();
  if (remove_stage && klcStorageIsReady()) {
    LittleFS.remove(KLC_SETUP_BACKUP_IMPORT_STAGE_PATH);
  }
  g_setup_backup_upload_token[0] = '\0';
  g_setup_backup_upload_bytes = 0;
  g_setup_backup_upload_complete = false;
  g_setup_backup_upload_error_status = 0;
  g_setup_backup_upload_error[0] = '\0';
  if (klcDiagBackgroundIsOwnedBy(KLC_SETUP_BACKUP_JOB)) {
    klcDiagBackgroundEnd(false, "Setup-Backupimport abgebrochen");
  }
  klcWebServerSetupBackupReleaseWriter();
}

static void klcWebServerSetupBackupUploadFail(int status,
                                               const char* message,
                                               bool remove_stage = true)
{
  if (g_setup_backup_import_file) g_setup_backup_import_file.close();
  if (remove_stage && klcStorageIsReady()) {
    LittleFS.remove(KLC_SETUP_BACKUP_IMPORT_STAGE_PATH);
  }
  g_setup_backup_upload_complete = false;
  g_setup_backup_upload_error_status = status;
  snprintf(g_setup_backup_upload_error,
           sizeof(g_setup_backup_upload_error), "%s",
           message != nullptr ? message : "Backup-Upload fehlgeschlagen");
  if (klcDiagBackgroundIsOwnedBy(KLC_SETUP_BACKUP_JOB)) {
    klcDiagBackgroundEnd(false, g_setup_backup_upload_error);
  }
  klcWebServerSetupBackupReleaseWriter();
}

static bool klcWebServerSetupBackupResultFresh(const char* token)
{
  return g_setup_backup_import_completed_ms != 0 &&
    (uint32_t)(millis() - g_setup_backup_import_completed_ms) <= 120000UL &&
    klcWebServerSetupOperationTokenValid(token) &&
    strcmp(token, g_setup_backup_import_token) == 0;
}

static void klcWebServerSetupRememberBackupResult(
  const char* token, int status, bool ok, const char* message,
  const KlcSetupBackupImportInfo* import_info = nullptr)
{
  if (!klcWebServerSetupOperationTokenValid(token)) return;
  snprintf(g_setup_backup_import_token,
           sizeof(g_setup_backup_import_token), "%s", token);
  snprintf(g_setup_backup_import_message,
           sizeof(g_setup_backup_import_message), "%s",
           message != nullptr ? message : "");
  g_setup_backup_import_status = status;
  g_setup_backup_import_ok = ok;
  if (ok && import_info != nullptr) {
    g_setup_backup_import_info = *import_info;
  } else {
    snprintf(g_setup_backup_import_info.language,
             sizeof(g_setup_backup_import_info.language), "%s", "en");
    g_setup_backup_import_info.imported_schema = 0;
    g_setup_backup_import_info.schema_migrated = false;
    g_setup_backup_import_info.profile_adjustment =
      KLC_SETUP_BACKUP_PROFILE_NONE;
  }
  g_setup_backup_import_completed_ms = millis();
  if (g_setup_backup_import_completed_ms == 0) {
    g_setup_backup_import_completed_ms = 1;
  }
}

static void klcWebServerSetupSendAndRememberBackupResult(
  const char* token, int status, bool ok, const char* message,
  const KlcSetupBackupImportInfo* import_info = nullptr)
{
  klcWebServerSetupRememberBackupResult(
    token, status, ok, message, import_info);
  klcWebServerSendSetupBackupJsonResult(
    status, ok, message, ok ? &g_setup_backup_import_info : nullptr);
}

void klcWebServerHandleSetupBackupImportRaw()
{
  HTTPRaw& raw = g_server.raw();

  if (raw.status == RAW_START) {
    klcWebServerSetupBackupUploadReset(true);
    const String token = klcWebServerSetupBackupOperationToken();
    if (klcWebServerSetupOperationTokenValid(token.c_str())) {
      snprintf(g_setup_backup_upload_token,
               sizeof(g_setup_backup_upload_token), "%s", token.c_str());
    } else {
      klcWebServerSetupBackupUploadFail(400,
        "Ungueltige Backup-Vorgangskennung");
      return;
    }

    // Der Raw-Callback darf unberechtigte Requestdaten nicht einmal temporaer
    // speichern. Der Abschluss-Handler wiederholt diese Pruefungen, bevor er
    // die gestagten Bytes auswertet oder eine Antwort sendet.
    if (g_config.ui.setup_wizard_completed) {
      klcWebServerSetupBackupUploadFail(409,
        "Backup-Import im Wizard ist nur waehrend der Ersteinrichtung verfuegbar");
      return;
    }
    if (!klcWebServerSetupCancelWifiAttempt(
          "Backup-Import verwirft den temporaeren WLAN-Test")) {
      klcWebServerSetupBackupUploadFail(409,
        "WLAN-Konfiguration wird gerade dauerhaft angewendet");
      return;
    }
    if (!klcWebServerSetupAccessAllowed(false) ||
        !klcWebServerCsrfTokenValid()) {
      klcWebServerSetupBackupUploadFail(403,
        "Backup-Import ist nicht autorisiert");
      return;
    }
    if (g_web_config_recovery_locked &&
        klcWebServerCurrentRole() != KLC_AUTH_ROLE_ADMIN) {
      klcWebServerSetupBackupUploadFail(409,
        "Backup-Import ist wegen der Speichersperre nur nach Admin-Anmeldung moeglich");
      return;
    }
    if (!klcWebServerSetupCommitCanStart()) {
      klcWebServerSetupBackupUploadFail(409,
        "Ein Update oder Neustart laeuft bereits");
      return;
    }
    if (!klcDiagBackgroundBegin(KLC_SETUP_BACKUP_JOB, "Upload empfangen")) {
      klcWebServerSetupBackupUploadFail(409,
        "Ein anderer Hintergrundvorgang laeuft bereits");
      return;
    }

    const int content_length = g_server.clientContentLength();
    if (content_length <= 0) {
      klcWebServerSetupBackupUploadFail(400,
        "Backup-Datei ist leer");
      return;
    }
    if ((size_t)content_length >= KLC_SETUP_BACKUP_IMPORT_MAX_BYTES) {
      klcWebServerSetupBackupUploadFail(413,
        "Backup-Datei ist zu gross");
      return;
    }
    if (!klcStorageIsReady()) {
      klcWebServerSetupBackupUploadFail(503,
        "LittleFS ist fuer den Backup-Import nicht bereit");
      return;
    }
    g_setup_backup_writer_token = klcStorageWriterNewToken();
    if (!klcStorageWriterTryAcquire(KLC_STORAGE_WRITER_SETUP_BACKUP,
                                    g_setup_backup_writer_token)) {
      g_setup_backup_writer_token = 0U;
      klcWebServerSetupBackupUploadFail(409,
        "LittleFS wird von einem anderen Vorgang verwendet", false);
      return;
    }

    g_setup_backup_import_file =
      LittleFS.open(KLC_SETUP_BACKUP_IMPORT_STAGE_PATH, "w");
    if (!g_setup_backup_import_file) {
      klcWebServerSetupBackupUploadFail(500,
        "Backup konnte nicht sicher zwischengespeichert werden");
      return;
    }

    Serial.print("[SETUP] Backup-Upload gestartet, Bytes erwartet: ");
    Serial.println(content_length);
    return;
  }

  if (raw.status == RAW_WRITE) {
    if (g_setup_backup_upload_error_status != 0 ||
        !g_setup_backup_import_file || raw.currentSize == 0) return;
    if (g_setup_backup_upload_bytes + raw.currentSize >=
        KLC_SETUP_BACKUP_IMPORT_MAX_BYTES) {
      klcWebServerSetupBackupUploadFail(413,
        "Backup-Datei ist zu gross");
      return;
    }
    const size_t written = g_setup_backup_import_file.write(
      raw.buf, raw.currentSize);
    if (written != raw.currentSize) {
      klcWebServerSetupBackupUploadFail(500,
        "Backup konnte nicht vollstaendig zwischengespeichert werden");
      return;
    }
    g_setup_backup_upload_bytes += written;
    const int content_length = g_server.clientContentLength();
    if (content_length > 0) {
      const uint8_t progress = (uint8_t)(
        (g_setup_backup_upload_bytes * 15U) / (size_t)content_length);
      klcDiagBackgroundSetStep("Upload empfangen", progress);
    }
    return;
  }

  if (raw.status == RAW_END) {
    if (g_setup_backup_import_file) g_setup_backup_import_file.close();
    if (g_setup_backup_upload_error_status != 0) return;

    const int content_length = g_server.clientContentLength();
    g_setup_backup_upload_complete = content_length > 0 &&
      g_setup_backup_upload_bytes > 0 &&
      g_setup_backup_upload_bytes == (size_t)content_length;
    if (!g_setup_backup_upload_complete) {
      klcWebServerSetupBackupUploadFail(400,
        "Backup-Datei ist leer oder unvollstaendig");
      return;
    }

    Serial.print("[SETUP] Backup-Upload vollstaendig gestaged: ");
    Serial.print(g_setup_backup_upload_bytes);
    Serial.println(" Bytes");
    return;
  }

  if (raw.status == RAW_ABORTED) {
    klcWebServerSetupBackupUploadFail(400,
      "Backup-Upload wurde abgebrochen");
  }
}

void klcWebServerHandleSetupBackupImportPost()
{
  const String token = klcWebServerSetupBackupOperationToken();
  if (!klcWebServerSetupOperationTokenValid(token.c_str())) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSendSetupJsonResult(400, false,
      "Ungueltige Backup-Vorgangskennung");
    return;
  }

  // Diese Route wird wegen des blockweisen Raw-Callbacks ohne den normalen
  // Routenwrapper registriert. Deshalb werden CSRF und Setup-Zugriff hier
  // nochmals verbindlich geprueft.
  if (!klcWebServerRequireAccess(KLC_WEB_PUBLIC, KLC_WEB_FLAG_CSRF)) {
    klcWebServerSetupBackupUploadReset(true);
    return;
  }
  if (!klcWebServerSetupAccessAllowed()) {
    klcWebServerSetupBackupUploadReset(true);
    return;
  }

  // Wiederholte Requests mit derselben Vorgangskennung liefern exakt das
  // bereits bekannte terminale Ergebnis. Das gilt fuer Erfolg und Fehler.
  if (klcWebServerSetupBackupResultFresh(token.c_str())) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSendSetupBackupJsonResult(
      g_setup_backup_import_status, g_setup_backup_import_ok,
      g_setup_backup_import_message,
      g_setup_backup_import_ok ? &g_setup_backup_import_info : nullptr);
    return;
  }

  if (g_config.ui.setup_wizard_completed) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 409, false,
      "Backup-Import im Wizard ist nur waehrend der Ersteinrichtung verfuegbar");
    return;
  }
  if (!klcWebServerSetupAbortWifiTestForExternalNetworkChange(
        "Backup-Import verwirft den temporaeren WLAN-Test")) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 409, false,
      "WLAN-Konfiguration wird gerade dauerhaft angewendet");
    return;
  }
  if (g_web_config_recovery_locked &&
      klcWebServerCurrentRole() != KLC_AUTH_ROLE_ADMIN) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 409, false,
      "Backup-Import ist wegen der Speichersperre nur nach Admin-Anmeldung moeglich");
    return;
  }
  if (!klcWebServerSetupCommitCanStart()) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 409, false,
      "Ein Update oder Neustart laeuft bereits");
    return;
  }

  if (strcmp(token.c_str(), g_setup_backup_upload_token) != 0) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 400, false,
      "Backup-Upload und Vorgangskennung passen nicht zusammen");
    return;
  }
  if (g_setup_backup_upload_error_status != 0) {
    const int status = g_setup_backup_upload_error_status;
    char error[sizeof(g_setup_backup_upload_error)];
    snprintf(error, sizeof(error), "%s", g_setup_backup_upload_error);
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), status, false,
      error);
    Serial.print("[SETUP] Backup-Import vor Verarbeitung abgelehnt: ");
    Serial.println(error);
    return;
  }
  if (!g_setup_backup_upload_complete ||
      g_setup_backup_upload_bytes == 0 ||
      g_setup_backup_upload_bytes >= KLC_SETUP_BACKUP_IMPORT_MAX_BYTES) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 400, false,
      "Backup-Datei ist leer oder unvollstaendig");
    return;
  }

  const size_t body_len = g_setup_backup_upload_bytes;

  KlcConfigWorkspaceLease candidate(
    "Setup-Backup importieren", g_config);
  if (!candidate) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 503, false,
      "Konfigurations-Arbeitsbereich ist belegt; Backup-Import abgebrochen");
    return;
  }

  char* body = static_cast<char*>(malloc(body_len + 1U));
  if (body == nullptr) {
    klcWebServerSetupBackupUploadReset(true);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 503, false,
      "Zu wenig RAM zum Einlesen des gestagten Backups");
    return;
  }

  size_t read_len = 0;
  {
    // Der File-Wrapper liegt absichtlich in einem eigenen Scope. Damit sind
    // auch seine internen Heapobjekte sicher zerstoert, bevor der grosse
    // Uploadpuffer freigegeben und der Commit-Puffer allokiert wird.
    klcDiagBackgroundSetStep("Datei einlesen", 20U);
    File input = LittleFS.open(KLC_SETUP_BACKUP_IMPORT_STAGE_PATH, "r");
    const uint32_t read_started_us = micros();
    while (input && read_len < body_len) {
      const size_t remaining = body_len - read_len;
      const size_t slice_len = remaining > KLC_SETUP_BACKUP_READ_SLICE_BYTES
        ? KLC_SETUP_BACKUP_READ_SLICE_BYTES : remaining;
      const size_t got = input.read(reinterpret_cast<uint8_t*>(body) + read_len,
                                    slice_len);
      if (got == 0U) break;
      read_len += got;
      klcEthernetServiceNow();
      yield();
    }
    klcDiagBackgroundRecordStep("Setup-Backup einlesen",
                                micros() - read_started_us, 100000UL);
    if (input) input.close();
  }
  LittleFS.remove(KLC_SETUP_BACKUP_IMPORT_STAGE_PATH);
  klcWebServerSetupBackupReleaseWriter();
  g_setup_backup_upload_complete = false;
  g_setup_backup_upload_bytes = 0;
  g_setup_backup_upload_token[0] = '\0';

  if (read_len != body_len) {
    free(body);
    klcDiagBackgroundEnd(false, "Backup-Zwischendatei ist unvollstaendig");
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 500, false,
      "Backup-Zwischendatei ist unvollstaendig");
    return;
  }
  body[body_len] = '\0';

  const char* cursor = body;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
         *cursor == '\n') ++cursor;
  if (*cursor != '{') {
    free(body);
    klcDiagBackgroundEnd(false, "Backup-Datei ist kein JSON-Objekt");
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 400, false,
      "Backup-Datei ist kein JSON-Objekt");
    return;
  }

  char message[256];
  bool runtime_ok = false;
  KlcSetupBackupImportInfo import_info = {
    "en", 0, false, KLC_SETUP_BACKUP_PROFILE_NONE
  };
  // Die Hilfsfunktion uebernimmt body und gibt ihn in jedem Rueckgabepfad frei.
  // Die Kandidaten-Lease bleibt beim Aufrufer und wird automatisch geloest.
  klcDiagBackgroundSetStep("Migrieren, validieren und atomar speichern", 55U);
  const uint32_t import_started_us = micros();
  const bool imported = klcWebServerSetupApplyBackupJson(
    *candidate, body, message, sizeof(message), runtime_ok, import_info);
  klcDiagBackgroundRecordStep("Setup-Backup pruefen und speichern",
                              micros() - import_started_us, 250000UL);

  if (!imported) {
    klcDiagBackgroundEnd(false, message);
    klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 400, false,
      message);
    Serial.print("[SETUP] Backup-Import fehlgeschlagen: ");
    Serial.println(message);
    return;
  }

  klcDiagBackgroundEnd(true);
  klcWebServerSetupSendAndRememberBackupResult(token.c_str(), 200, true,
    message, &import_info);
  Serial.print("[SETUP] Backup-Import erfolgreich: ");
  Serial.println(message);
  klcWebServerScheduleControlledReboot(
    "Backup im Einrichtungsassistenten wiederhergestellt", 5000UL);
}

void klcWebServerHandleSetupBackupStatusGet()
{
  const String token = klcWebServerSetupBackupOperationToken();
  if (!klcWebServerSetupBackupResultFresh(token.c_str())) {
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(404, "application/json; charset=utf-8",
                  "{\"ok\":false,\"error\":\"backup result not found\"}");
    return;
  }
  klcWebServerSendSetupBackupJsonResult(
    g_setup_backup_import_status, g_setup_backup_import_ok,
    g_setup_backup_import_message,
    g_setup_backup_import_ok ? &g_setup_backup_import_info : nullptr);
}


