#include "klc_web_server_internal.h"
#include "klc_storage_coordinator.h"
#include "klc_scene_store.h"
#include "klc_setup_wizard_http_internal.h"

static bool klcWebServerReadU16Arg(const char* name, uint16_t& target, char* message, size_t message_len)
{
  if (name == nullptr || !g_server.hasArg(name)) {
    return true;
  }

  String text = g_server.arg(name);
  text.trim();
  if (text.length() == 0) {
    snprintf(message, message_len, "KNX-Objekt %s fehlt", name);
    return false;
  }
  uint32_t value = 0;
  if (!klcWebServerParseUint32Strict(text, value, true)) {
    snprintf(message, message_len,
             "KNX-Objekt %s muss eine vollstaendige dezimale Ganzzahl sein", name);
    return false;
  }
  if (value > 65535UL) {
    snprintf(message, message_len, "KNX-Objekt %s außerhalb 0..65535", name);
    return false;
  }

  target = (uint16_t)value;
  return true;
}

static bool klcWebServerReadSecondsAsMsArg(const char* name, uint16_t& target_ms, char* message, size_t message_len)
{
  if (name == nullptr || !g_server.hasArg(name)) {
    return true;
  }

  String text = g_server.arg(name);
  text.trim();
  if (text.length() == 0) {
    snprintf(message, message_len, "KNX-Zeit %s fehlt", name);
    return false;
  }

  uint32_t seconds = 0;
  uint16_t milliseconds = 0;
  uint8_t fraction_digits = 0;
  bool separator_seen = false;
  bool digit_seen = false;

  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.charAt(i);
    if (c >= '0' && c <= '9') {
      digit_seen = true;
      const uint8_t digit = (uint8_t)(c - '0');
      if (!separator_seen) {
        seconds = seconds * 10U + digit;
        if (seconds > 60U) {
          snprintf(message, message_len, "KNX-Zeit %s außerhalb 0..60 Sekunden", name);
          return false;
        }
      } else {
        if (fraction_digits >= 3U) {
          snprintf(message, message_len, "KNX-Zeit %s hat mehr als drei Nachkommastellen", name);
          return false;
        }
        milliseconds = (uint16_t)(milliseconds * 10U + digit);
        ++fraction_digits;
      }
      continue;
    }

    if ((c == '.' || c == ',') && !separator_seen) {
      separator_seen = true;
      continue;
    }

    snprintf(message, message_len, "KNX-Zeit %s ist ungültig", name);
    return false;
  }

  if (!digit_seen) {
    snprintf(message, message_len, "KNX-Zeit %s ist ungültig", name);
    return false;
  }
  while (fraction_digits < 3U) {
    milliseconds = (uint16_t)(milliseconds * 10U);
    ++fraction_digits;
  }
  if (seconds == 60U && milliseconds != 0U) {
    snprintf(message, message_len, "KNX-Zeit %s außerhalb 0..60 Sekunden", name);
    return false;
  }

  target_ms = (uint16_t)(seconds * 1000U + milliseconds);
  return true;
}

static bool klcWebServerReadU8Arg(const char* name, uint8_t& target, uint8_t max_value, char* message, size_t message_len)
{
  if (name == nullptr || !g_server.hasArg(name)) {
    return true;
  }

  String text = g_server.arg(name);
  text.trim();
  if (text.length() == 0) {
    snprintf(message, message_len, "KNX-Wert %s fehlt", name);
    return false;
  }

  uint16_t value = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.charAt(i);
    if (c < '0' || c > '9') {
      snprintf(message, message_len, "KNX-Wert %s ist ungültig", name);
      return false;
    }
    value = (uint16_t)(value * 10U + (uint8_t)(c - '0'));
    if (value > max_value) {
      snprintf(message, message_len, "KNX-Wert %s außerhalb 0..%u", name, max_value);
      return false;
    }
  }

  target = (uint8_t)value;
  return true;
}

static KlcConfigIdLookupState klcWebServerFindKnxOutputIndexById(
  uint8_t output_id, uint8_t& index, char* message, size_t message_len)
{
  const KlcConfigIdLookupResult lookup = klcConfigFindOutputById(g_config, output_id);
  if (lookup.state == KLC_CONFIG_ID_UNIQUE) {
    index = lookup.first_index;
  } else if (lookup.state == KLC_CONFIG_ID_AMBIGUOUS) {
    snprintf(message, message_len,
             "Ausgangs-ID %u ist mehrdeutig: Ausgang %u (%.31s) und Ausgang %u (%.31s)",
             (unsigned)output_id,
             (unsigned)(lookup.first_index + 1U), g_config.outputs[lookup.first_index].name,
             (unsigned)(lookup.second_index + 1U), g_config.outputs[lookup.second_index].name);
  } else {
    snprintf(message, message_len, "Ausgang %u nicht gefunden", (unsigned)output_id);
  }
  return lookup.state;
}

static bool klcWebServerApplyKnxGlobalConfig(char* message, size_t message_len, bool& saved)
{
  saved = false;
  KlcConfigWorkspaceLease candidate(
    "Zentrale KNX-Konfiguration speichern", g_config);
  if (!candidate) {
    snprintf(message, message_len,
             "Konfigurations-Arbeitsbereich ist belegt; keine Aenderung");
    return false;
  }
  KlcDeviceConfig& next_cfg = *candidate;

  const bool form_seen = g_server.hasArg("status_send_mode") || g_server.hasArg("status_interval_seconds") ||
                         g_server.hasArg("global_switch") || g_server.hasArg("global_scene") ||
                         g_server.hasArg("global_color_r") || g_server.hasArg("global_color_g") ||
                         g_server.hasArg("global_color_b") || g_server.hasArg("global_color_w") ||
                         g_server.hasArg("status_power_limit") || g_server.hasArg("status_error");

  if (g_server.hasArg("enabled")) {
    const String enabled = g_server.arg("enabled");
    next_cfg.knx.enabled = !(enabled == "0" || enabled == "false" || enabled == "FALSE");
  } else if (form_seen) {
    next_cfg.knx.enabled = false;
  }

  if (g_server.hasArg("baudrate")) {
    const long baudrate = klcWebServerArgToLongStrict("baudrate");
    if (baudrate != (long)KLC_BAOS_BAUD_RATE) {
      snprintf(message, message_len,
               "BAOS-Baudrate ist fest auf 19200 Baud, 8E1 eingestellt und nicht aenderbar");
      return false;
    }
  }
  // Auch Kandidaten aus alten Konfigurationen werden vor dem Speichern
  // normalisiert; das API-Kompatibilitaetsfeld ist nur noch ein No-op.
  next_cfg.knx.baudrate = KLC_BAOS_BAUD_RATE;

  if (!klcWebServerReadU8Arg("status_send_mode", next_cfg.knx.status_send_mode, KLC_KNX_STATUS_CHANGE_OR_INTERVAL, message, message_len)) return false;
  if (g_server.hasArg("status_interval_seconds")) {
    // Rohwert als Text pruefen und als uint32_t einlesen; erst die zentrale
    // Normalisierung (feste Stufen 1 s .. 1 h, sonst 10 s) schreibt in das
    // uint16_t-Feld. Kein Cast vor der Validierung, kein Ueberlauf mehr.
    String interval = g_server.arg("status_interval_seconds");
    interval.trim();
    uint32_t seconds = 0;
    if (!klcWebServerParseUint32Strict(interval, seconds, true) || seconds > 3600UL) {
      snprintf(message, message_len,
               "status_interval_seconds muss eine Ganzzahl in 0..3600 sein");
      return false;
    }
    next_cfg.knx.status_interval_seconds = klcKnxStatusIntervalNormalize(seconds);
  } else {
    // Ein fehlendes Feld ist auf jedem Eingangsweg eindeutig 10 Sekunden.
    // Insbesondere darf ein manipulierter zeitbasierter Request nicht still
    // den zuvor gespeicherten Wert weiterverwenden.
    next_cfg.knx.status_interval_seconds = klcKnxStatusIntervalNormalize(0);
  }

  if (!klcWebServerReadU16Arg("global_switch", next_cfg.knx.global.global_switch, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_scene", next_cfg.knx.global.global_scene, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_next_scene", next_cfg.knx.global.global_next_scene, message, message_len)) return false;
  next_cfg.knx.global.global_brightness = 0;
  next_cfg.knx.global.global_lock = 0;
  next_cfg.knx.global.global_force = 0;
  if (!klcWebServerReadU16Arg("global_color_r", next_cfg.knx.global.global_color_r, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_color_g", next_cfg.knx.global.global_color_g, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_color_b", next_cfg.knx.global.global_color_b, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_color_w", next_cfg.knx.global.global_color_w, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_eth_enable", next_cfg.knx.global.global_eth_enable, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("global_wlan_enable", next_cfg.knx.global.global_wlan_enable, message, message_len)) return false;
  next_cfg.knx.global.status_scene = 0;
  next_cfg.knx.global.status_brightness = 0;
  if (!klcWebServerReadU16Arg("status_power_limit", next_cfg.knx.global.status_power_limit, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("status_error", next_cfg.knx.global.status_error, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("status_eth_enabled", next_cfg.knx.global.status_eth_enabled, message, message_len)) return false;
  if (!klcWebServerReadU16Arg("status_wlan_enabled", next_cfg.knx.global.status_wlan_enabled, message, message_len)) return false;

  if (!klcConfigValidateDetailed(next_cfg, message, message_len)) {
    return false;
  }

  // Transaktional: erst dauerhaft speichern, dann aktivieren. Bei einem
  // Speicherfehler bleibt die alte Konfiguration vollstaendig aktiv, es gibt
  // keine teilweise Uebernahme in den RAM.
  saved = klcStorageSaveConfig(next_cfg);
  if (!saved) {
    snprintf(message, message_len, "KNX-Einstellungen nicht gespeichert, Konfiguration unverändert");
    return false;
  }
  g_config = next_cfg;
  snprintf(message, message_len, "zentrale KNX-Objekte gespeichert");
  return true;
}

static bool klcWebServerApplyKnxOutputConfig(char* message, size_t message_len, bool& saved)
{
  saved = false;
  if (!g_server.hasArg("id")) {
    snprintf(message, message_len, "Ausgangs-ID fehlt");
    return false;
  }

  const long id_long = klcWebServerArgToLongStrict("id");
  if (id_long <= 0 || id_long > 255) {
    snprintf(message, message_len, "Ausgangs-ID ungültig");
    return false;
  }

  uint8_t index = 0;
  if (klcWebServerFindKnxOutputIndexById(
        (uint8_t)id_long, index, message, message_len) != KLC_CONFIG_ID_UNIQUE) {
    return false;
  }

  KlcConfigWorkspaceLease candidate(
    "KNX-Ausgangskonfiguration speichern", g_config);
  if (!candidate) {
    snprintf(message, message_len,
             "Konfigurations-Arbeitsbereich ist belegt; keine Aenderung");
    return false;
  }
  KlcDeviceConfig& next_cfg = *candidate;
  KlcOutputConfig& out = next_cfg.outputs[index];
  KlcKnxOutputObjectConfig& map = next_cfg.knx.outputs[index];

  if (!klcWebServerReadSecondsAsMsArg("knx_on_ramp_seconds", out.knx_on_ramp_ms, message, message_len)) return false;
  if (!klcWebServerReadSecondsAsMsArg("knx_off_ramp_seconds", out.knx_off_ramp_ms, message, message_len)) return false;
  uint8_t knx_on_mode = (uint8_t)out.knx_on_mode;
  if (!klcWebServerReadU8Arg("knx_on_mode", knx_on_mode, KLC_KNX_ON_SCENE, message, message_len)) return false;
  out.knx_on_mode = (KlcKnxOnMode)knx_on_mode;
  if (!klcWebServerReadU8Arg("knx_on_r", out.knx_on_r, 100, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("knx_on_g", out.knx_on_g, 100, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("knx_on_b", out.knx_on_b, 100, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("knx_on_w", out.knx_on_w, 100, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("knx_on_scene", out.knx_on_scene, KLC_SCENE_MAX_PUBLIC, message, message_len)) return false;
  if (out.knx_on_mode == KLC_KNX_ON_SCENE && out.knx_on_scene < 1U) {
    snprintf(message, message_len, "KNX-Einschaltszene muss zwischen 1 und %u liegen", KLC_SCENE_MAX_PUBLIC);
    return false;
  }

  const bool admin = klcWebServerCurrentRole() == KLC_AUTH_ROLE_ADMIN;
  if (admin) {
    if (!klcWebServerReadU16Arg("switch", map.switch_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("scene", map.scene_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("next_scene", map.next_scene_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("brightness", map.brightness_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("lock", map.lock_obj, message, message_len)) return false;
  }
  if (!klcWebServerReadU8Arg("lock_active_level", map.lock_behavior.active_level, 1, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("lock_active_action", map.lock_behavior.active_action, KLC_KNX_ACTION_ON, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("lock_inactive_action", map.lock_behavior.inactive_action, KLC_KNX_ACTION_ON, message, message_len)) return false;
  if (admin && !klcWebServerReadU16Arg("force", map.force_obj, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("force_active_level", map.force_behavior.active_level, 1, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("force_active_action", map.force_behavior.active_action, KLC_KNX_ACTION_ON, message, message_len)) return false;
  if (!klcWebServerReadU8Arg("force_inactive_action", map.force_behavior.inactive_action, KLC_KNX_ACTION_ON, message, message_len)) return false;
  if (admin) {
    if (!klcWebServerReadU16Arg("color_r", map.color_r_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("color_g", map.color_g_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("color_b", map.color_b_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("color_w", map.color_w_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_scene", map.status_scene_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_brightness", map.status_brightness_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_lock", map.status_lock_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_force", map.status_force_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_switch", map.status_switch_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_color_r", map.status_color_r_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_color_g", map.status_color_g_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_color_b", map.status_color_b_obj, message, message_len)) return false;
    if (!klcWebServerReadU16Arg("status_color_w", map.status_color_w_obj, message, message_len)) return false;
  }

  if (!klcConfigValidateDetailed(next_cfg, message, message_len)) {
    return false;
  }

  // Transactional, matching the global KNX form: persist the complete
  // candidate first and expose it to runtime only after a confirmed write.
  saved = klcStorageSaveConfig(next_cfg);
  if (!saved) {
    snprintf(message, message_len, "KNX-Objekte für OUT%ld nicht gespeichert, Konfiguration unverändert", id_long);
    return false;
  }
  g_config = next_cfg;
  snprintf(message, message_len, "KNX-Objekte für OUT%ld gespeichert", id_long);
  return true;
}

bool klcWebServerStreamKnxPage(int status_code, const char* message,
                               bool error)
{
  if (!klcLanguageActiveIsValid(g_config.ui.language) &&
      !klcWebServerLanguagePhaseIsBusy()) {
    klcLanguageRequestDownload(g_config.ui.language);
  }

  KlcWebResponseStreamGuard stream(
    status_code, "text/html; charset=utf-8", true);
  if (!stream.started()) return false;

  (void)klcWebUiKnxHtml(message, error);
  return stream.finish();
}

void klcWebServerHandleKnxConfigApiPost()
{
  char message[128];
  bool saved = false;
  const bool ok = klcWebServerApplyKnxGlobalConfig(message, sizeof(message), saved);
  // 400 = Request ungueltig, 500 = gueltig, aber Flash-Speichern fehlgeschlagen
  // (Konfiguration bleibt dann unveraendert aktiv).
  const int status = ok ? (saved ? 200 : 500) : 400;
  klcWebServerSendGenerated(status, "application/json; charset=utf-8", klcWebUiResultJson(ok && saved, message));
}

void klcWebServerHandleKnxConfigFormPost()
{
  char message[128];
  bool saved = false;
  const bool ok = klcWebServerApplyKnxGlobalConfig(message, sizeof(message), saved);
  if (!ok || !saved) {
    (void)klcWebServerStreamKnxPage(ok ? 500 : 400, message, true);
    return;
  }
  g_server.sendHeader("Location", klcWebServerSafeReturnPath("/knx"));
  g_server.send(303, "text/plain; charset=utf-8", message);
}

void klcWebServerHandleKnxOutputApiPost()
{
  char message[128];
  bool saved = false;
  const bool ok = klcWebServerApplyKnxOutputConfig(message, sizeof(message), saved);
  const int status = ok ? (saved ? 200 : 500) : 400;
  klcWebServerSendGenerated(status, "application/json; charset=utf-8", klcWebUiResultJson(ok && saved, message));
}

void klcWebServerHandleKnxOutputFormPost()
{
  char message[128];
  bool saved = false;
  const bool ok = klcWebServerApplyKnxOutputConfig(message, sizeof(message), saved);
  if (!ok || !saved) {
    (void)klcWebServerStreamKnxPage(ok ? 500 : 400, message, true);
    return;
  }
  g_server.sendHeader("Location", klcWebServerSafeReturnPath("/knx"));
  g_server.send(303, "text/plain; charset=utf-8", message);
}

static bool klcWebServerReadKnxRequest(uint16_t& object_id, uint32_t& value)
{
  if (!g_server.hasArg("object") || !g_server.hasArg("value")) {
    return false;
  }

  const long object_long = klcWebServerArgToLongStrict("object");
  const long value_long = klcWebServerArgToLongStrict("value");
  if (object_long <= 0 || object_long > 65535L || value_long < 0) {
    return false;
  }

  object_id = (uint16_t)object_long;
  value = (uint32_t)value_long;
  return true;
}

static bool klcWebServerApplyKnxRequest(char* message, size_t message_len)
{
  uint16_t object_id = 0;
  uint32_t value = 0;

  if (!klcWebServerReadKnxRequest(object_id, value)) {
    snprintf(message, message_len, "ungültige KNX-Dummyanforderung");
    return false;
  }

  const bool ok = klcKnxObjectsSimulateValue(object_id, value);
  snprintf(message, message_len, ok ? "KNX Objekt %u mit Wert %lu simuliert" : "KNX Simulation fehlgeschlagen", object_id, (unsigned long)value);
  return ok;
}

// API-Endpunkt für simulierte KNX-Objektwerte.
void klcWebServerHandleKnxSimApiPost()
{
  char message[96];
  const bool ok = klcWebServerApplyKnxRequest(message, sizeof(message));
  klcWebServerSendGenerated(ok ? 200 : 400, "application/json; charset=utf-8", klcWebUiResultJson(ok, message));
}

// Formular-Endpunkt für die KNX-Seite. Danach zurück zur KNX-Diagnose.
void klcWebServerHandleKnxSimFormPost()
{
  char message[96];
  (void)klcWebServerApplyKnxRequest(message, sizeof(message));
  g_server.sendHeader("Location", "/knx");
  g_server.send(303, "text/plain; charset=utf-8", message);
}

static bool klcWebServerApplyKnxTxTest(char* message, size_t message_len)
{
  if (!g_server.hasArg("test")) {
    snprintf(message, message_len, "kein BAOS-TX-Test angegeben");
    return false;
  }

  const String test = g_server.arg("test");
  return klcKnxBaosSendTxTest(test.c_str(), message, message_len);
}

// API-Endpunkt fuer manuelle BAOS-UART-TX-Diagnose.
void klcWebServerHandleKnxTxTestApiPost()
{
  char message[128];
  const bool ok = klcWebServerApplyKnxTxTest(message, sizeof(message));
  klcWebServerSendGenerated(ok ? 200 : 400, "application/json; charset=utf-8", klcWebUiResultJson(ok, message));
}

// Formular-Endpunkt fuer manuelle BAOS-UART-TX-Diagnose. Danach zurueck zur KNX-Seite.
void klcWebServerHandleKnxTxTestFormPost()
{
  char message[128];
  (void)klcWebServerApplyKnxTxTest(message, sizeof(message));
  g_server.sendHeader("Location", "/knx");
  g_server.send(303, "text/plain; charset=utf-8", message);
}

// Exportiert die komplette Laufzeitkonfiguration als JSON.
static void klcWebServerStreamConfigExport()
{
  // Ein Backup ist immer ein nachweislich persistenter Snapshot. Solange
  // eine Szene nur im RAM lebt oder ihr Commit laeuft, wird kein mehrdeutiges
  // JSON erzeugt, das spaeter faelschlich als dauerhaft interpretiert wird.
  if(klcSceneStoreHasDirtyScenes()||klcSceneStoreIsBusy()){
    g_server.send(409,"application/json; charset=utf-8",
      "{\"ok\":false,\"error\":\"Backup gesperrt: mindestens eine Szene ist nur im RAM aktiv oder wird noch gespeichert\"}");
    return;
  }
  size_t measured_length = 0;
  if (!klcStorageMeasureExportJson(g_config, measured_length) ||
      measured_length == 0) {
    klcWebServerSendGenerated(
      500, "application/json; charset=utf-8",
      "{\"ok\":false,\"error\":\"Configuration export preflight failed\"}");
    return;
  }
  if (!klcWebServerBeginStreamedResponse(
        200, "application/json; charset=utf-8")) return;
  const bool ok = klcStorageExportJsonStream(g_config,
                                              g_web_response_writer);
  klcWebServerFinishStreamedResponse(ok);
}

void klcWebServerHandleConfigGet()
{
  klcWebServerStreamConfigExport();
}

// Browserfreundlicher Dateidownload. Im Gegensatz zur Roh-API ist dieser Weg
// auch fuer normale Benutzer der erweiterten Ansicht freigegeben. Der Export
// enthaelt weiterhin keine WLAN- oder Konto-Passwoerter.
void klcWebServerHandleBackupExportGet()
{
  if (!klcWebServerConfigPageAccessAllowed()) {
    g_server.send(403, "text/plain; charset=utf-8",
                  "Backup-Export ist in der erweiterten Ansicht verfuegbar");
    return;
  }

  char disposition[96];
  snprintf(disposition, sizeof(disposition),
           "attachment; filename=klc-backup-%s.json", KLC_VERSION);
  g_server.sendHeader("Content-Disposition", disposition);
  g_server.sendHeader("X-Content-Type-Options", "nosniff");
  klcWebServerStreamConfigExport();
}

// Liefert Backup-/Restore-Metadaten als JSON.
void klcWebServerHandleBackupJson()
{
  klcWebServerSendGenerated(200, "application/json; charset=utf-8", klcWebUiBackupJson());
}


static String klcWebServerReadJsonBodyArg()
{
  String body = g_server.arg("plain");
  if (body.length() == 0 && g_server.hasArg("config")) {
    body = g_server.arg("config");
  }
  return body;
}

// Validiert JSON ohne Speichern, damit Backups vor dem Restore geprüft werden können.
void klcWebServerHandleConfigValidatePost()
{
  String body = klcWebServerReadJsonBodyArg();
  if (body.length() == 0) {
    klcWebServerSendGenerated(400, "application/json; charset=utf-8", klcWebUiResultJson(false, "keine JSON-Daten empfangen"));
    return;
  }

  const bool ok = klcStorageValidateJson(body.c_str());
  klcWebServerSendGenerated(ok ? 200 : 400, "application/json; charset=utf-8", klcWebUiResultJson(ok, ok ? "JSON gültig" : klcStorageGetLastError()));
}

// Importiert eine JSON-Konfiguration, validiert sie und speichert sie in LittleFS.
void klcWebServerHandleConfigPost()
{
  String body = klcWebServerReadJsonBodyArg();

  if (body.length() == 0) {
    klcWebServerSendGenerated(400, "application/json; charset=utf-8", klcWebUiResultJson(false, "keine JSON-Daten empfangen"));
    return;
  }

  KlcConfigWorkspaceLease candidate(
    "Konfiguration ueber API importieren", g_config);
  if (!candidate) {
    klcWebServerSendGenerated(503, "application/json; charset=utf-8",
      klcWebUiResultJson(false,
        "Konfigurations-Arbeitsbereich ist belegt; Import abgebrochen"));
    return;
  }
  KlcDeviceConfig& next_cfg = *candidate;

  if (!klcStorageImportJson(next_cfg, body.c_str())) {
    klcWebServerSendGenerated(400, "application/json; charset=utf-8", klcWebUiResultJson(false, klcStorageGetLastError()));
    return;
  }

  KlcChainTopology imported_topology;
  char imported_topology_error[160];
  if (!klcChainBuildTopology(next_cfg, imported_topology, imported_topology_error, sizeof(imported_topology_error))) {
    klcWebServerSendGenerated(400, "application/json; charset=utf-8",
      klcWebUiResultJson(false, imported_topology_error));
    return;
  }

  char scene_txn_message[160];
  if (!klcSceneStorePrepareConfigReplacement(next_cfg,scene_txn_message,
                                               sizeof(scene_txn_message))) {
    klcWebServerSendGenerated(500, "application/json; charset=utf-8",
      klcWebUiResultJson(false,
        scene_txn_message));
    return;
  }
  const bool saved = g_web_config_recovery_locked
    ? klcStorageSaveConfigAsRecoveryPending(next_cfg)
    : klcStorageSaveConfigAsPending(next_cfg, true);
  if (!saved) {
    klcSceneStoreCancelConfigReplacement();
    klcWebServerSendGenerated(500, "application/json; charset=utf-8", klcWebUiResultJson(false, klcStorageGetLastError()));
    return;
  }
  if(!klcSceneStoreConfirmConfigReplacement()){
    klcWebServerSendGenerated(500,"application/json; charset=utf-8",
      klcWebUiResultJson(false,"Szenen-Transaktionsjournal fehlt"));return;
  }

  klcOutputPendingCaptureSaved(next_cfg);
  klcWebServerSetConfigRecoveryLocked(false);
  char response[384];snprintf(response,sizeof(response),
    "{\"ok\":true,\"message\":\"Konfiguration importiert und als Pending gespeichert; Neustart zur Aktivierung erforderlich\",\"replacement_operation_id\":\"%llu\",\"replacement_status_url\":\"/api/scene/replacement-status?operation_id=%llu\"}",
    (unsigned long long)klcSceneStoreReplacementOperationId(),
    (unsigned long long)klcSceneStoreReplacementOperationId());
  klcWebServerSendGenerated(200,"application/json; charset=utf-8",response);
}


void klcWebServerHandleConfigAdoptPreviousPost()
{
  if (klcWebServerRejectIfUpdateBusy(
        "Rettungskopie kann waehrend Update oder geplantem Neustart nicht uebernommen werden")) {
    return;
  }

  KlcConfigWorkspaceLease adopted_workspace(
    "Konfigurations-Rettungskopie uebernehmen", g_config);
  if (!adopted_workspace) {
    if (g_server.uri().startsWith("/api/")) {
      klcWebServerSendGenerated(503, "application/json; charset=utf-8",
        klcWebUiResultJson(false,
          "Konfigurations-Arbeitsbereich zum Validieren ist belegt"));
    } else {
      klcWebServerSendLocalizedFormError(
        503, "Konfigurations-Arbeitsbereich zum Validieren ist belegt", "/config");
    }
    return;
  }
  KlcDeviceConfig& adopted = *adopted_workspace;

  if (!klcStorageAdoptPreviousAsPending(adopted)) {
    char error[160];
    snprintf(error, sizeof(error),
             "Rettungskopie wurde nicht uebernommen: %.104s",
             klcStorageGetLastError());
    if (g_server.uri().startsWith("/api/")) {
      klcWebServerSendGenerated(400, "application/json; charset=utf-8",
        klcWebUiResultJson(false, error));
    } else {
      klcWebServerSendLocalizedFormError(400, error, "/config");
    }
    return;
  }

  klcOutputPendingCaptureSaved(adopted);
  klcWebServerSetConfigRecoveryLocked(false);
  klcWebServerScheduleControlledReboot(
    "Validierte Konfigurations-Rettungskopie uebernommen", 3000UL);

  const char* message =
    "Rettungskopie wurde als neue aktive Konfiguration gespeichert und verifiziert. Der Controller startet neu.";
  if (g_server.uri().startsWith("/api/")) {
    char response[384];snprintf(response,sizeof(response),
      "{\"ok\":true,\"message\":\"%s\",\"replacement_operation_id\":\"%llu\",\"replacement_status_url\":\"/api/scene/replacement-status?operation_id=%llu\"}",
      message,(unsigned long long)klcSceneStoreReplacementOperationId(),
      (unsigned long long)klcSceneStoreReplacementOperationId());
    klcWebServerSendGenerated(200,"application/json; charset=utf-8",response);
    return;
  }
  char location[96];snprintf(location,sizeof(location),
    "/config?replacement_operation_id=%llu",
    (unsigned long long)klcSceneStoreReplacementOperationId());
  g_server.sendHeader("Location",location);
  g_server.send(303, "text/plain; charset=utf-8", message);
}


static constexpr size_t KLC_WEB_BACKUP_IMPORT_MAX_BYTES = 240000U;
static const char KLC_WEB_BACKUP_IMPORT_STAGE_PATH[] =
  "/klc-backup-import.tmp";
static File g_web_backup_import_file;
static bool g_web_backup_import_authorized = false;
static bool g_web_backup_import_complete = false;
static bool g_web_backup_import_too_large = false;
static bool g_web_backup_import_storage_error = false;
static bool g_web_backup_import_conflict = false;
static size_t g_web_backup_import_bytes = 0;
static bool g_web_backup_import_writer_held = false;
static constexpr char KLC_WEB_BACKUP_IMPORT_JOB[] = "Backupimport";
static constexpr size_t KLC_WEB_BACKUP_READ_SLICE_BYTES = 4096U;
static constexpr uint32_t KLC_WEB_BACKUP_PARSE_BUDGET_US = 100000UL;
static constexpr uint32_t KLC_WEB_BACKUP_COMMIT_BUDGET_US = 250000UL;

static void klcWebServerBackupImportReleaseWriter()
{
  if(!g_web_backup_import_writer_held)return;
  klcStorageWriterRelease(KLC_STORAGE_WRITER_IMPORT);
  g_web_backup_import_writer_held=false;
}

static void klcWebServerBackupImportRawReset(bool remove_stage)
{
  if (g_web_backup_import_file) g_web_backup_import_file.close();
  if (remove_stage && klcStorageIsReady()) {
    LittleFS.remove(KLC_WEB_BACKUP_IMPORT_STAGE_PATH);
  }
  g_web_backup_import_authorized = false;
  g_web_backup_import_complete = false;
  g_web_backup_import_too_large = false;
  g_web_backup_import_storage_error = false;
  g_web_backup_import_conflict = false;
  klcWebServerBackupImportReleaseWriter();
  g_web_backup_import_bytes = 0;
  if (klcDiagBackgroundIsOwnedBy(KLC_WEB_BACKUP_IMPORT_JOB)) {
    klcDiagBackgroundEnd(false, "Backupimport abgebrochen");
  }
}

// Raw application/json wird blockweise in LittleFS gestaged. Dadurch liegt
// ein grosses Backup nicht gleichzeitig als WebServer-Argument, URL-dekodierte
// Kopie und Importpuffer im Heap. Berechtigung und Groessenlimit gelten schon
// fuer den ersten Datenblock; unberechtigte Bytes werden nicht gespeichert.
void klcWebServerHandleBackupImportRaw()
{
  HTTPRaw& raw = g_server.raw();
  if (raw.status == RAW_START) {
    klcWebServerBackupImportRawReset(true);
    const KlcAuthRole role = klcWebServerCurrentRole();
    const bool user_allowed = role == KLC_AUTH_ROLE_USER ||
                              role == KLC_AUTH_ROLE_ADMIN ||
                              !g_config.auth.user_login_enabled;
    g_web_backup_import_authorized = user_allowed &&
      klcWebServerCsrfTokenValid() &&
      klcWebServerConfigPageAccessAllowed() &&
      (!klcStorageIsConfigWriteLocked() ||
       role == KLC_AUTH_ROLE_ADMIN);
    if (!g_web_backup_import_authorized) return;

    if (klcOtaIsUpdateRunning() || klcOtaIsDownloadRunning() ||
        klcOtaIsRebootRequested() || klcOtaIsBootselRequested() ||
        !klcDiagBackgroundBegin(KLC_WEB_BACKUP_IMPORT_JOB,
                                "Upload empfangen")) {
      g_web_backup_import_conflict = true;
      return;
    }

    const int content_length = g_server.clientContentLength();
    if (content_length <= 0 ||
        (size_t)content_length >= KLC_WEB_BACKUP_IMPORT_MAX_BYTES) {
      g_web_backup_import_too_large = content_length > 0;
      return;
    }
    if (!klcStorageIsReady()) {
      g_web_backup_import_storage_error = true;
      return;
    }
    if(!klcStorageWriterTryAcquire(KLC_STORAGE_WRITER_IMPORT)){
      g_web_backup_import_conflict=true;return;
    }
    g_web_backup_import_writer_held=true;
    g_web_backup_import_file =
      LittleFS.open(KLC_WEB_BACKUP_IMPORT_STAGE_PATH, "w");
    if (!g_web_backup_import_file) {
      g_web_backup_import_storage_error = true;
      klcWebServerBackupImportReleaseWriter();
    }
    return;
  }

  if (raw.status == RAW_WRITE) {
    if (!g_web_backup_import_authorized ||
        g_web_backup_import_too_large ||
        g_web_backup_import_storage_error ||
        !g_web_backup_import_file || raw.currentSize == 0) return;
    if (g_web_backup_import_bytes + raw.currentSize >=
        KLC_WEB_BACKUP_IMPORT_MAX_BYTES) {
      g_web_backup_import_too_large = true;
      g_web_backup_import_file.close();
      LittleFS.remove(KLC_WEB_BACKUP_IMPORT_STAGE_PATH);
      klcWebServerBackupImportReleaseWriter();
      return;
    }
    const size_t written = g_web_backup_import_file.write(
      raw.buf, raw.currentSize);
    if (written != raw.currentSize) {
      g_web_backup_import_storage_error = true;
      g_web_backup_import_file.close();
      LittleFS.remove(KLC_WEB_BACKUP_IMPORT_STAGE_PATH);
      klcWebServerBackupImportReleaseWriter();
      return;
    }
    g_web_backup_import_bytes += written;
    const int content_length = g_server.clientContentLength();
    if (content_length > 0) {
      const uint8_t progress = (uint8_t)(
        (g_web_backup_import_bytes * 15U) / (size_t)content_length);
      klcDiagBackgroundSetStep("Upload empfangen", progress);
    }
    return;
  }

  if (raw.status == RAW_END) {
    if (g_web_backup_import_file) g_web_backup_import_file.close();
    klcWebServerBackupImportReleaseWriter();
    const int content_length = g_server.clientContentLength();
    g_web_backup_import_complete = g_web_backup_import_authorized &&
      !g_web_backup_import_too_large &&
      !g_web_backup_import_storage_error &&
      content_length > 0 && g_web_backup_import_bytes > 0 &&
      g_web_backup_import_bytes == (size_t)content_length;
    if (!g_web_backup_import_complete && klcStorageIsReady()) {
      LittleFS.remove(KLC_WEB_BACKUP_IMPORT_STAGE_PATH);
    }
    return;
  }

  if (raw.status == RAW_ABORTED) {
    klcWebServerBackupImportRawReset(true);
  }
}

void klcWebServerHandleBackupImportFormPost()
{
  if (!klcWebServerRequireAccess(KLC_WEB_USER, KLC_WEB_FLAG_CSRF)) {
    klcWebServerBackupImportRawReset(true);
    return;
  }
  const bool full_admin_import =
    klcWebServerCurrentRole() == KLC_AUTH_ROLE_ADMIN;
  if (!klcWebServerConfigPageAccessAllowed()) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(403,
      "Backup-Import ist in der erweiterten Ansicht verfuegbar", "/system");
    return;
  }
  // Im Recovery-Zustand darf weiterhin nur ein Admin die absichtlich
  // gesperrte Hauptkonfiguration ersetzen. Ein normaler Benutzerimport ist
  // dort kein geeigneter Wiederherstellungsweg.
  if (klcStorageIsConfigWriteLocked() && !full_admin_import) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(403,
      "Konfigurations-Recovery aktiv; Backup-Import erfordert Adminrechte",
      "/config");
    return;
  }
  if (g_web_backup_import_too_large) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(413,
      "Backup-Datei ist zu gross", "/config");
    return;
  }
  if (g_web_backup_import_storage_error) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(500,
      "Backup konnte nicht sicher zwischengespeichert werden", "/config");
    return;
  }
  if (g_web_backup_import_conflict) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(409,
      "Backupimport kollidiert mit einem laufenden Hintergrundvorgang",
      "/config");
    return;
  }
  if (!g_web_backup_import_authorized || !g_web_backup_import_complete ||
      g_web_backup_import_bytes == 0 ||
      g_web_backup_import_bytes >= KLC_WEB_BACKUP_IMPORT_MAX_BYTES) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(400,
      "Backup-Datei ist leer oder unvollstaendig", "/config");
    return;
  }

  if (!klcWebServerSetupAbortWifiTestForExternalNetworkChange(
        "Backup-Import verwirft den temporaeren WLAN-Test")) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(409,
      "WLAN-Konfiguration wird gerade dauerhaft angewendet", "/config");
    return;
  }
  const size_t body_len = g_web_backup_import_bytes;
  KlcConfigWorkspaceLease candidate(
    "Backup aus Zwischendatei importieren", g_config);
  if (!candidate) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(503,
      "Konfigurations-Arbeitsbereich ist belegt; Import abgebrochen", "/config");
    return;
  }
  KlcDeviceConfig& next_cfg = *candidate;
  char* body = static_cast<char*>(malloc(body_len + 1U));
  if (body == nullptr) {
    klcWebServerBackupImportRawReset(true);
    klcWebServerSendLocalizedFormError(503,
      "zu wenig RAM zum Einlesen des Backups", "/config");
    return;
  }

  klcDiagBackgroundSetStep("Datei einlesen", 20U);
  const uint32_t read_started_us = micros();
  File input = LittleFS.open(KLC_WEB_BACKUP_IMPORT_STAGE_PATH, "r");
  size_t read_len = 0;
  while (input && read_len < body_len) {
    const size_t remaining = body_len - read_len;
    const size_t slice_len = remaining > KLC_WEB_BACKUP_READ_SLICE_BYTES
      ? KLC_WEB_BACKUP_READ_SLICE_BYTES : remaining;
    const size_t got = input.read(reinterpret_cast<uint8_t*>(body) + read_len,
                                  slice_len);
    if (got == 0U) break;
    read_len += got;
    // Nur der nicht-atomare Dateileseschritt wird portioniert. Der spaetere
    // Commit bleibt bewusst exklusiv; hier darf der Netzwerkstack Luft holen.
    klcEthernetServiceNow();
    yield();
  }
  klcDiagBackgroundRecordStep("Backup-Datei einlesen",
                              micros() - read_started_us,
                              KLC_WEB_BACKUP_PARSE_BUDGET_US);
  if (input) input.close();
  LittleFS.remove(KLC_WEB_BACKUP_IMPORT_STAGE_PATH);
  g_web_backup_import_authorized = false;
  g_web_backup_import_complete = false;
  g_web_backup_import_bytes = 0;
  if (read_len != body_len) {
    free(body);
    klcDiagBackgroundEnd(false, "Backup-Zwischendatei ist unvollstaendig");
    klcWebServerSendLocalizedFormError(500,
      "Backup-Zwischendatei ist unvollstaendig", "/config");
    return;
  }
  body[body_len] = '\0';
  klcDiagBackgroundSetStep("Konfiguration migrieren", 45U);
  const uint32_t parse_started_us = micros();
  const bool imported = klcStorageImportJson(next_cfg, body);
  klcDiagBackgroundRecordStep("Backup migrieren und validieren",
                              micros() - parse_started_us,
                              KLC_WEB_BACKUP_PARSE_BUDGET_US);
  free(body);

  if (imported) {
    if (!full_admin_import) {
      // Ein externer Vollimport koennte sonst Adminpfade umgehen (z.B.
      // Benutzer-Login abschalten, Netzwerkschnittstellen veraendern oder
      // KNX-Systemobjekte umbelegen). Normale Benutzer duerfen dieselben
      // Bereiche wiederherstellen, die sie auch ueber die WebUI bearbeiten:
      // LED-Ausgaenge, Szenen/Ablaufe, Leistung und Darstellung.
      snprintf(next_cfg.device_name, sizeof(next_cfg.device_name), "%s",
               g_config.device_name);
      snprintf(next_cfg.hostname, sizeof(next_cfg.hostname), "%s",
               g_config.hostname);
      next_cfg.network = g_config.network;
      next_cfg.wifi = g_config.wifi;
      next_cfg.update = g_config.update;
      next_cfg.auth = g_config.auth;
      next_cfg.knx = g_config.knx;
      next_cfg.ui.setup_wizard_completed =
        g_config.ui.setup_wizard_completed;
    }

    KlcChainTopology imported_topology;
    char imported_topology_error[160];
    if (!klcChainBuildTopology(next_cfg, imported_topology, imported_topology_error, sizeof(imported_topology_error))) {
      klcDiagBackgroundEnd(false, imported_topology_error);
      klcWebServerSendLocalizedFormError(400, imported_topology_error, "/config");
      return;
    }
    char scene_txn_message[160];
    if (!klcSceneStorePrepareConfigReplacement(next_cfg,scene_txn_message,
                                                 sizeof(scene_txn_message))) {
      klcDiagBackgroundEnd(false,
        scene_txn_message);
      klcWebServerSendLocalizedFormError(500,
        scene_txn_message,
        "/config");
      return;
    }
    klcDiagBackgroundSetStep("Atomar speichern und verifizieren", 80U);
    const uint32_t commit_started_us = micros();
    const bool saved = g_web_config_recovery_locked
      ? klcStorageSaveConfigAsRecoveryPending(next_cfg)
      : klcStorageSaveConfigAsPending(next_cfg, true);
    klcDiagBackgroundRecordStep("Backup atomar speichern",
                                micros() - commit_started_us,
                                KLC_WEB_BACKUP_COMMIT_BUDGET_US);
    if (!saved) {
      klcSceneStoreCancelConfigReplacement();
      klcDiagBackgroundEnd(false, klcStorageGetLastError());
      klcWebServerSendLocalizedFormError(500, "Backup gueltig, Speichern fehlgeschlagen; bisherige Konfiguration bleibt aktiv", "/config");
      return;
    }
    if(!klcSceneStoreConfirmConfigReplacement()){
      klcDiagBackgroundEnd(false,"Szenen-Transaktionsjournal fehlt");
      klcWebServerSendLocalizedFormError(500,
        "Hauptkonfiguration gespeichert, Szenen-Transaktionsjournal fehlt",
        "/config");return;
    }
    klcOutputPendingCaptureSaved(next_cfg);
    klcWebServerSetConfigRecoveryLocked(false);
    klcDiagBackgroundEnd(true);
    char location[96];snprintf(location,sizeof(location),
      "/config?replacement_operation_id=%llu",
      (unsigned long long)klcSceneStoreReplacementOperationId());
    g_server.sendHeader("Location",location);
    g_server.send(303, "text/plain; charset=utf-8",
      full_admin_import
        ? "Backup vollstaendig importiert und als Pending gespeichert; Neustart zur Aktivierung erforderlich"
        : "Benutzerkonfiguration als Pending gespeichert; geschuetzte Systemeinstellungen bleiben erhalten");
    return;
  }

  klcDiagBackgroundEnd(false, klcStorageGetLastError());
  klcWebServerSendLocalizedFormError(400, klcStorageGetLastError(), "/config");
}

void klcWebServerHandleBackupDefaultsFormPost()
{
  char message[160];
  bool saved = false;
  const bool ok = klcWebServerResetFactoryDefaults(message, sizeof(message), saved);
  if (!ok || !saved) {
    klcWebServerSendLocalizedFormError(500, message, "/config");
    return;
  }

  g_server.sendHeader("Location", "/config");
  g_server.send(303, "text/plain; charset=utf-8", message);
}

// Setzt bewusst nur auf POST zurück, damit ein Browser-Refresh nichts verändert.
void klcWebServerHandleDefaultsPost()
{
  char message[160];
  bool saved = false;
  const bool ok = klcWebServerResetFactoryDefaults(message, sizeof(message), saved);
  klcWebServerSendGenerated(ok && saved ? 200 : 500, "application/json; charset=utf-8",
    klcWebUiResultJson(ok && saved, message));
}
