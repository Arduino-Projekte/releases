#include "klc_storage_internal.h"
#include "klc_knx_baos.h"
#include "klc_storage_coordinator.h"

bool klcStorageBegin()
{
  g_storage_ready = LittleFS.begin();
  if (!g_storage_ready) {
    klcStorageSetLastError("LittleFS konnte nicht gestartet werden");
    Serial.println("[STORAGE] WARNUNG: LittleFS konnte nicht gestartet werden.");
    return false;
  }

  if (!klcStorageResolveConfigJournal()) {
    Serial.print("[STORAGE] WARNUNG: Konfigurations-Journal konnte nicht bereinigt werden: ");
    Serial.println(klcStorageGetLastError());
    // Das Dateisystem selbst ist weiterhin erreichbar. Load/Save melden den
    // konkreten Fehler; die Recovery-WebUI darf deshalb weiter starten.
    return true;
  }
  if (!klcStorageResolveLastKnownGoodJournal()) {
    Serial.print("[STORAGE] WARNUNG: Last-Known-Good-Journal konnte nicht bereinigt werden: ");
    Serial.println(klcStorageGetLastError());
    return true;
  }

  klcStorageSetLastError("ok");
  Serial.println("[STORAGE] LittleFS bereit.");
  return true;
}

bool klcStorageIsReady()
{
  return g_storage_ready;
}

void klcStorageSetConfigWriteLocked(bool locked)
{
  g_storage_config_write_locked = locked;
  if (locked) g_storage_ui_state_pending = false;
}

bool klcStorageIsConfigWriteLocked()
{
  return g_storage_config_write_locked;
}

const char* klcStorageGetConfigPath()
{
  return KLC_CONFIG_PATH;
}

const char* klcStorageGetLastError()
{
  return g_storage_last_error;
}

uint16_t klcStorageGetLastSchemaVersion()
{
  return g_storage_last_schema_version;
}

bool klcStorageWasLastMigrationApplied()
{
  return g_storage_last_migration_applied;
}

const char* klcStorageGetLastMigrationMessage()
{
  return g_storage_last_migration;
}

bool klcStorageConfigExists()
{
  if (!g_storage_ready) {
    return false;
  }

  return LittleFS.exists(KLC_CONFIG_PATH);
}

bool klcStorageLastKnownGoodExists()
{
  return g_storage_ready && LittleFS.exists(KLC_CONFIG_LKG_PATH);
}

bool klcStorageGetConfigIdentity(uint32_t& size, uint32_t& hash)
{
  return klcStorageReadFileIdentity(KLC_CONFIG_PATH, size, hash);
}

bool klcStorageGetLastKnownGoodIdentity(uint32_t& size, uint32_t& hash)
{
  return klcStorageReadFileIdentity(KLC_CONFIG_LKG_PATH, size, hash);
}

bool klcStoragePromoteCurrentToLastKnownGood(uint32_t writer_token)
{
  KlcStorageWriterLease writer(KLC_STORAGE_WRITER_CONFIG,writer_token);
  if(!writer.acquired()){
    klcStorageSetLastError("LittleFS-Schreiber fuer LKG-Promotion belegt");
    return false;
  }
  if (!g_storage_ready || !LittleFS.exists(KLC_CONFIG_PATH)) {
    klcStorageSetLastError("Hauptkonfiguration fuer Last-Known-Good fehlt");
    return false;
  }

  uint32_t source_size = 0, source_hash = 0;
  if (!klcStorageReadFileIdentity(KLC_CONFIG_PATH, source_size, source_hash)) {
    klcStorageSetLastError("Hauptkonfiguration fuer Last-Known-Good nicht lesbar");
    return false;
  }

  LittleFS.remove(KLC_CONFIG_LKG_TMP_PATH);
  if (!klcStorageCopyFile(KLC_CONFIG_PATH, KLC_CONFIG_LKG_TMP_PATH)) {
    klcStorageSetLastError("Last-Known-Good-Tempdatei konnte nicht geschrieben werden");
    return false;
  }
  uint32_t temp_size = 0, temp_hash = 0;
  if (!klcStorageReadFileIdentity(KLC_CONFIG_LKG_TMP_PATH,
                                  temp_size, temp_hash) ||
      temp_size != source_size || temp_hash != source_hash) {
    LittleFS.remove(KLC_CONFIG_LKG_TMP_PATH);
    klcStorageSetLastError("Last-Known-Good-Tempdatei konnte nicht verifiziert werden");
    return false;
  }

  LittleFS.remove(KLC_CONFIG_LKG_PREV_PATH);
  const bool had_lkg = LittleFS.exists(KLC_CONFIG_LKG_PATH);
  if (had_lkg &&
      !klcStorageCopyFile(KLC_CONFIG_LKG_PATH, KLC_CONFIG_LKG_PREV_PATH)) {
    LittleFS.remove(KLC_CONFIG_LKG_TMP_PATH);
    klcStorageSetLastError("Vorherige Last-Known-Good-Datei konnte nicht gesichert werden");
    return false;
  }
  if (had_lkg && !LittleFS.remove(KLC_CONFIG_LKG_PATH)) {
    LittleFS.remove(KLC_CONFIG_LKG_TMP_PATH);
    klcStorageSetLastError("Last-Known-Good-Datei konnte nicht ausgetauscht werden");
    return false;
  }
  if (!LittleFS.rename(KLC_CONFIG_LKG_TMP_PATH, KLC_CONFIG_LKG_PATH)) {
    if (had_lkg) {
      (void)klcStorageCopyFile(KLC_CONFIG_LKG_PREV_PATH,
                               KLC_CONFIG_LKG_PATH);
    }
    klcStorageSetLastError("Last-Known-Good-Commit fehlgeschlagen");
    return false;
  }

  uint32_t lkg_size = 0, lkg_hash = 0;
  if (!klcStorageReadFileIdentity(KLC_CONFIG_LKG_PATH, lkg_size, lkg_hash) ||
      lkg_size != source_size || lkg_hash != source_hash) {
    LittleFS.remove(KLC_CONFIG_LKG_PATH);
    if (had_lkg) {
      (void)klcStorageCopyFile(KLC_CONFIG_LKG_PREV_PATH,
                               KLC_CONFIG_LKG_PATH);
    }
    klcStorageSetLastError("Last-Known-Good nach Commit nicht verifizierbar");
    return false;
  }

  klcStorageSetLastError("ok");
  Serial.println("[STORAGE] Last-Known-Good atomar aktualisiert und verifiziert.");
  return true;
}

size_t klcStorageGetConfigSize()
{
  if (!g_storage_ready || !LittleFS.exists(KLC_CONFIG_PATH)) {
    return 0;
  }

  File file = LittleFS.open(KLC_CONFIG_PATH, "r");
  if (!file) {
    return 0;
  }

  const size_t len = file.size();
  file.close();
  return len;
}

bool klcStorageValidateJson(const char* json)
{
  if (json == nullptr || json[0] == '\0') {
    klcStorageSetLastError("Validierung: JSON leer");
    return false;
  }

  KlcConfigWorkspaceLease candidate("Konfigurations-JSON validieren");
  if (!candidate) {
    klcStorageSetLastError(
      "Konfigurations-Arbeitsbereich zur JSON-Validierung ist belegt");
    return false;
  }
  const bool ok = klcStorageImportJson(*candidate, json);
  if (ok) {
    klcStorageSetLastError("Validierung ok");
  }
  return ok;
}

// Baut das Konfigurations-JSON auf. include_secrets steuert, ob WLAN-Passwoerter
// mitgeschrieben werden: true nur fuer die lokale Konfigurationsdatei auf dem
// Geraet, false fuer jeden Export/Download (dort erscheint nur *_password_set).
bool klcStorageBuildJson(const KlcDeviceConfig& cfg, char* buffer,
                                size_t buffer_len, bool include_secrets,
                                size_t* json_len)
{
  const bool measure_only = buffer == nullptr && buffer_len == 0;
  const bool stream_mode = buffer == &g_storage_export_stream_sentinel &&
                           g_storage_export_stream_writer != nullptr;
  if (!measure_only && !stream_mode &&
      (buffer == nullptr || buffer_len == 0)) {
    klcStorageSetLastError("Exportpuffer fehlt");
    return false;
  }

  size_t used = 0;
  if (buffer != nullptr && !stream_mode) buffer[0] = '\0';

  bool ok = true;
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, "{\n  \"schema_version\": %u,\n  \"project\": ", KLC_CONFIG_SCHEMA_VERSION);
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, KLC_PROJECT_NAME);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n  \"firmware_version\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, KLC_VERSION);
  // Die Herkunft ist reine Kompatibilitaetsmetadaten. Sie bindet das Backup
  // nicht an ein Board, sondern sagt dem Zielprofil nur, ob die WLAN-Felder
  // auf dem Quellgeraet ueberhaupt wirksam waren. Unbekannte/alte Backups
  // bleiben weiterhin importierbar.
  ok &= klcStorageAppendRaw(buffer, buffer_len, used,
                           ",\n  \"backup\": {\n    \"format_version\": 1,\n    \"source_target\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, KLC_BUILD_TARGET);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                               ",\n    \"wifi_supported\": %s\n  },\n  \"device\": {\n    \"name\": ",
                               KLC_WIFI_BACKEND_ACTIVE ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.device_name);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"hostname\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.hostname);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n  },\n  \"network\": {\n");
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, "    \"dhcp\": %s,\n    \"static_ip\": ", cfg.network.dhcp ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.static_ip);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"gateway\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.gateway);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"subnet\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.subnet);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"dns\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.dns);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n    \"fallback_enabled\": %s,\n    \"fallback_ip\": ", cfg.network.fallback_enabled ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.fallback_ip);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"fallback_gateway\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.fallback_gateway);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"fallback_subnet\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.fallback_subnet);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"fallback_dns\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.network.fallback_dns);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n  },\n  \"wifi\": {\n");
  ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                               "    \"eth_enabled\": %s,\n    \"wlan_enabled\": %s,\n    \"sta_enabled\": %s,\n    \"sta_ever_connected\": %s,\n    \"sta_ssid\": ",
                               cfg.wifi.eth_enabled ? "true" : "false",
                               cfg.wifi.wlan_enabled ? "true" : "false",
                               cfg.wifi.sta_enabled ? "true" : "false",
                               cfg.wifi.sta_ever_connected ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.sta_ssid);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n    \"sta_password_set\": %s", cfg.wifi.sta_password[0] ? "true" : "false");
  if (include_secrets) {
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"sta_password\": ");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.sta_password);
  }
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n    \"sta_dhcp\": %s,\n    \"sta_static_ip\": ", cfg.wifi.sta_dhcp ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.sta_static_ip);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"sta_gateway\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.sta_gateway);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"sta_subnet\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.sta_subnet);
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"sta_dns\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.sta_dns);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n    \"ap_enabled\": %s,\n    \"ap_ssid\": ", cfg.wifi.ap_enabled ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.ap_ssid);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n    \"ap_password_set\": %s", cfg.wifi.ap_password[0] ? "true" : "false");
  if (include_secrets) {
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"ap_password\": ");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.ap_password);
  }
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"ap_ip\": ");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.wifi.ap_ip);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n    \"priority\": %u\n  },\n  \"update\": {\n", (unsigned)cfg.wifi.priority);
  ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                               "    \"beta_program\": %s,\n    \"auto_check\": %s,\n    \"auto_install\": %s,\n    \"auto_check_interval_minutes\": %u,\n    \"auto_install_delay_minutes\": %u\n  },\n  \"ui\": {\n    \"dark_mode\": %s,\n    \"language\": ",
                               cfg.update.beta_program ? "true" : "false",
                               cfg.update.auto_check ? "true" : "false",
                               cfg.update.auto_install ? "true" : "false",
                               cfg.update.auto_check_interval_minutes,
                               cfg.update.auto_install_delay_minutes,
                               cfg.ui.dark_mode ? "true" : "false");
  ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.ui.language[0] ? cfg.ui.language : "en");
  ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                               ",\n    \"advanced_view\": %s,\n    \"setup_wizard_completed\": %s\n  },\n  \"auth\": {\n",
                               cfg.ui.advanced_view ? "true" : "false",
                               cfg.ui.setup_wizard_completed ? "true" : "false");
  // Benutzer-/Adminsystem: Der Export/Download enthaelt nur den Schalter
  // user_login_enabled. Salts und PBKDF2-Hashes werden ausschliesslich in die
  // lokale Konfigurationsdatei geschrieben (include_secrets), damit Backups
  // keine wiederverwendbaren Anmeldedaten enthalten und ein Import fremde
  // Zugangsdaten nicht unbemerkt uebernimmt.
  ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                               "    \"user_login_enabled\": %s,\n    \"schema\": %u",
                               cfg.auth.user_login_enabled ? "true" : "false",
                               (unsigned)cfg.auth.schema);
  if (include_secrets) {
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"user_password_salt\": ");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.auth.user_password_salt);
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"user_password_hash\": ");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.auth.user_password_hash);
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"admin_password_salt\": ");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.auth.admin_password_salt);
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, ",\n    \"admin_password_hash\": ");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, cfg.auth.admin_password_hash);
  }
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n  },\n  \"led\": {\n");
  // Legacy-Feld fuer alte Backupleser; Zentralhelligkeit ist fest neutral.
  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "    \"global_brightness\": 255,\n    \"outputs\": [\n");

  for (uint8_t i = 0; i < cfg.output_count && i < KLC_MAX_OUTPUTS; ++i) {
    const KlcOutputConfig& out = cfg.outputs[i];
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, i == 0 ? "      {\n" : ",\n      {\n");
    ok &= klcStorageAppendFormat(buffer, buffer_len, used, "        \"id\": %u,\n        \"enabled\": %s,\n        \"name\": ", out.id, out.enabled ? "true" : "false");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, out.name);
    ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n        \"follows_output\": %u,\n        \"chain_reverse\": %s,\n        \"gpio\": %u,\n        \"pixels\": %u,\n        \"string_segment_count\": %u,\n        \"pixel_offset\": %d,\n        \"power_on_state_on\": %s,\n        \"power_on_mode\": %u,\n        \"power_on_r\": %u,\n        \"power_on_g\": %u,\n        \"power_on_b\": %u,\n        \"power_on_w\": %u,\n        \"power_on_scene\": %u,\n        \"led_send_mode\": %u,\n        \"led_send_interval_seconds\": %u,\n        \"knx_on_mode\": %u,\n        \"knx_on_r\": %u,\n        \"knx_on_g\": %u,\n        \"knx_on_b\": %u,\n        \"knx_on_w\": %u,\n        \"knx_on_scene\": %u,\n        \"knx_on_ramp_ms\": %u,\n        \"knx_off_ramp_ms\": %u,\n        \"type\": \"%s\",\n        \"chipset\": \"%s\",\n        \"color_order\": \"%s\",\n        \"power_group\": %u,\n        \"limit_ma\": %lu,\n        \"max_brightness\": %u\n      }",
                                  (unsigned)out.follows_output,
                                  out.chain_reverse ? "true" : "false",
                                  out.gpio,
                                  out.pixels,
                                  out.string_segment_count,
                                  (int)klcClampPixelOffset(out.pixel_offset, out.pixels),
                                  out.power_on_state_on ? "true" : "false",
                                  (unsigned)out.power_on_mode,
                                  (unsigned)out.power_on_r,
                                  (unsigned)out.power_on_g,
                                  (unsigned)out.power_on_b,
                                  (unsigned)out.power_on_w,
                                  (unsigned)out.power_on_scene,
                                   (unsigned)out.led_send_mode,
                                   (unsigned)out.led_send_interval_seconds,
                                   (unsigned)out.knx_on_mode,
                                  (unsigned)out.knx_on_r,
                                  (unsigned)out.knx_on_g,
                                  (unsigned)out.knx_on_b,
                                  (unsigned)out.knx_on_w,
                                   (unsigned)out.knx_on_scene,
                                   (unsigned)out.knx_on_ramp_ms,
                                   (unsigned)out.knx_off_ramp_ms,
                                   klcStorageLedTypeToText(out.type),
                                  klcStorageLedChipsetToText(out.chipset),
                                  klcStorageColorOrderToText(out.color_order),
                                  out.power_group,
                                  (unsigned long)out.limit_ma,
                                  out.max_brightness);  }

  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n    ]\n  },\n  \"power\": {\n");
  ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                               "    \"controller_limit_ma\": %lu,\n    \"idle_ma\": %lu,\n    \"safety_percent\": %u,\n    \"r_ma_per_pixel\": %u,\n    \"g_ma_per_pixel\": %u,\n    \"b_ma_per_pixel\": %u,\n    \"w_ma_per_pixel\": %u,\n    \"standby_ma_per_pixel\": %u,\n    \"groups\": [\n",
                               (unsigned long)cfg.power.controller_limit_ma,
                               (unsigned long)cfg.power.idle_ma,
                               cfg.power.safety_percent,
                               cfg.power.r_ma_per_pixel,
                               cfg.power.g_ma_per_pixel,
                               cfg.power.b_ma_per_pixel,
                               cfg.power.w_ma_per_pixel,
                               cfg.power.standby_ma_per_pixel);

  for (uint8_t g = 0; g < cfg.power.group_count && g < KLC_MAX_POWER_GROUPS; ++g) {
    const KlcPowerGroupConfig& group = cfg.power.groups[g];
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, g == 0 ? "      {\n" : ",\n      {\n");
    ok &= klcStorageAppendFormat(buffer, buffer_len, used, "        \"id\": %u,\n        \"name\": ", group.id);
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, group.name);
    ok &= klcStorageAppendFormat(buffer, buffer_len, used, ",\n        \"limit_ma\": %lu\n      }", (unsigned long)group.limit_ma);
  }

  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n    ]\n  },\n  \"scenes\": [\n");

  for (uint8_t s = 1; s <= KLC_SCENE_MAX_PUBLIC; ++s) {
    const KlcSceneConfig& scene = cfg.scenes[s];
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, s == 1 ? "    {\n" : ",\n    {\n");
    ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                                 "      \"id\": %u,\n      \"enabled\": %s,\n      \"name\": ",
                                 scene.id,
                                 scene.enabled ? "true" : "false");
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, scene.name);
    ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                                 ",\n      \"string_segment_start_delay_ms\": %u,\n      \"string_segment_stop_delay_ms\": %u,\n      \"in_pool\": %s",
                                 scene.string_segment_start_delay_ms,
                                 scene.string_segment_stop_delay_ms,
                                 scene.in_pool ? "true" : "false");
    ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                                 ",\n      \"color\": {\"r\": %u, \"g\": %u, \"b\": %u, \"w\": %u},\n      \"brightness\": %u,\n      \"effect_type\": \"%s\",\n      \"direction\": \"%s\",\n      \"pixel_mode\": \"%s\",\n      \"lit_percent\": %u,\n      \"start_fill_percent\": %u,\n      \"main_fill_percent\": %u,\n      \"end_fill_percent\": %u,\n      \"lit_pixels\": %u,\n      \"speed_ms\": %u,\n      \"start_step_ms\": %u,\n      \"main_step_ms\": %u,\n      \"end_step_ms\": %u,\n      \"sync_mode\": \"%s\",\n      \"global_delay_ms\": %u,\n      \"start_effect\": \"%s\",\n      \"main_effect\": \"%s\",\n      \"end_effect\": \"%s\",\n      \"start_duration_ms\": %lu,\n      \"main_duration_ms\": %lu,\n      \"end_duration_ms\": %lu,\n      \"transition_duration_ms\": %lu,\n      \"pulse_period_ms\": %lu,\n      \"start_pulse_period_ms\": %lu,\n      \"main_pulse_period_ms\": %lu,\n      \"end_pulse_period_ms\": %lu,\n      \"segment_percent\": %u,\n      \"start_segment_percent\": %u,\n      \"main_segment_percent\": %u,\n      \"end_segment_percent\": %u,\n      \"segment_soft_edge_pixels\": %u,\n      \"tail_percent\": %u,\n      \"start_tail_percent\": %u,\n      \"main_tail_percent\": %u,\n      \"end_tail_percent\": %u,\n      \"wave_bounce\": %s,\n      \"start_reverse_direction\": %s,\n      \"main_reverse_direction\": %s,\n      \"end_reverse_direction\": %s,\n      \"start_mirror_center\": %s,\n      \"main_mirror_center\": %s,\n      \"end_mirror_center\": %s,\n      \"start_wave_bounce\": %s,\n      \"main_wave_bounce\": %s,\n      \"end_wave_bounce\": %s,\n      \"tetris_group_min\": %u,\n      \"tetris_group_max\": %u,\n      \"tetris_random_colors\": %s,\n      \"tetris_reverse_direction\": %s,\n      \"tetris_mirror_center\": %s,\n      \"tetris_random_direction\": %s,\n      \"tetris_direction\": \"%s\",\n      \"tetris_gap\": %u,\n      \"tetris_teardown_mode\": \"%s\",\n      \"tetris_random_timing\": %s,\n      \"tetris_pause_min_ms\": %u,\n      \"tetris_pause_max_ms\": %u,\n      \"tetris_step_min_ms\": %u,\n      \"tetris_step_max_ms\": %u,\n      \"start_tetris_group_min\": %u,\n      \"main_tetris_group_min\": %u,\n      \"end_tetris_group_min\": %u,\n      \"start_tetris_group_max\": %u,\n      \"main_tetris_group_max\": %u,\n      \"end_tetris_group_max\": %u,\n      \"start_tetris_gap\": %u,\n      \"main_tetris_gap\": %u,\n      \"end_tetris_gap\": %u,\n      \"start_tetris_teardown_mode\": \"%s\",\n      \"main_tetris_teardown_mode\": \"%s\",\n      \"end_tetris_teardown_mode\": \"%s\",\n      \"start_tetris_random_colors\": %s,\n      \"main_tetris_random_colors\": %s,\n      \"end_tetris_random_colors\": %s,\n      \"start_tetris_reverse_direction\": %s,\n      \"main_tetris_reverse_direction\": %s,\n      \"end_tetris_reverse_direction\": %s,\n      \"start_tetris_mirror_center\": %s,\n      \"main_tetris_mirror_center\": %s,\n      \"end_tetris_mirror_center\": %s,\n      \"start_tetris_random_direction\": %s,\n      \"main_tetris_random_direction\": %s,\n      \"end_tetris_random_direction\": %s,\n      \"tetris2_next_preview\": %s,\n      \"tetris2_sync_segments\": %s,\n      \"tetris2_direction_alternate\": %s,\n      \"tetris2_random_colors\": %s,\n      \"tetris2_reverse_direction\": %s,\n      \"tetris2_block_min\": %u,\n      \"tetris2_block_max\": %u,\n      \"tetris2_teardown_mode\": \"%s\",\n      \"tetris2_pause_min_ms\": %u,\n      \"tetris2_pause_max_ms\": %u,\n      \"tetris2_pixels_per_meter\": %u,\n      \"tetris2_speed_min_mm_s\": %u,\n      \"tetris2_speed_max_mm_s\": %u,\n      \"tetris2_early_start_chance_pct\": %u,\n      \"tetris2_early_start_min_pct\": %u,\n      \"tetris2_early_start_max_pct\": %u,\n      \"tetris2_hsv_min_distance\": %u,\n      \"sparkle_speed_ms\": %u,\n      \"start_sparkle_speed_ms\": %u,\n      \"main_sparkle_speed_ms\": %u,\n      \"end_sparkle_speed_ms\": %u,\n      \"sparkle_fill_percent\": %u,\n      \"start_sparkle_fill_percent\": %u,\n      \"main_sparkle_fill_percent\": %u,\n      \"end_sparkle_fill_percent\": %u,\n      \"sparkle_lifetime_ms\": %u,\n      \"start_sparkle_lifetime_ms\": %u,\n      \"main_sparkle_lifetime_ms\": %u,\n      \"end_sparkle_lifetime_ms\": %u,\n      \"fireworks_speed\": %u,\n      \"fireworks_intensity\": %u,\n      \"tetrix_speed\": %u,\n      \"tetrix_width\": %u\n    }",
                                 scene.r,
                                 scene.g,
                                 scene.b,
                                 scene.w,
                                 scene.brightness,
                                 klcStorageSceneEffectToText(scene.effect_type),
                                 klcStorageSceneDirectionToText(scene.direction),
                                 klcStorageScenePixelModeToText(scene.pixel_mode),
                                 scene.lit_percent,
                                 scene.start_fill_percent,
                                 scene.main_fill_percent,
                                 scene.end_fill_percent,
                                 scene.lit_pixels,
                                 scene.speed_ms,
                                 scene.start_step_ms,
                                 scene.main_step_ms,
                                 scene.end_step_ms,
                                 klcStorageSceneSyncModeToText(scene.sync_mode),
                                 scene.global_delay_ms,
                                 klcStorageScenePhaseEffectToText(scene.start_effect),
                                 klcStorageSceneMainEffectToText(scene.main_effect),
                                 klcStorageScenePhaseEffectToText(scene.end_effect),
                                 (unsigned long)scene.start_duration_ms,
                                 (unsigned long)scene.main_duration_ms,
                                 (unsigned long)scene.end_duration_ms,
                                 (unsigned long)scene.transition_duration_ms,
                                 (unsigned long)scene.pulse_period_ms,
                                 (unsigned long)scene.start_pulse_period_ms,
                                 (unsigned long)scene.main_pulse_period_ms,
                                 (unsigned long)scene.end_pulse_period_ms,
                                 scene.segment_percent,
                                 scene.start_segment_percent,
                                 scene.main_segment_percent,
                                 scene.end_segment_percent,
                                 scene.segment_soft_edge_pixels,
                                 scene.tail_percent,
                                 scene.start_tail_percent,
                                 scene.main_tail_percent,
                                 scene.end_tail_percent,
                                 scene.wave_bounce ? "true" : "false",
                                 scene.start_reverse_direction ? "true" : "false",
                                 scene.main_reverse_direction ? "true" : "false",
                                 scene.end_reverse_direction ? "true" : "false",
                                 scene.start_mirror_center ? "true" : "false",
                                 scene.main_mirror_center ? "true" : "false",
                                 scene.end_mirror_center ? "true" : "false",
                                 scene.start_wave_bounce ? "true" : "false",
                                 scene.main_wave_bounce ? "true" : "false",
                                 scene.end_wave_bounce ? "true" : "false",
                                 scene.tetris_group_min,
                                 scene.tetris_group_max,
                                 scene.tetris_random_colors ? "true" : "false",
                                 scene.tetris_reverse_direction ? "true" : "false",
                                 scene.tetris_mirror_center ? "true" : "false",
                                 scene.tetris_random_direction ? "true" : "false",
                                 klcStorageSceneTetrisDirectionToText(scene.tetris_direction),
                                 scene.tetris_gap,
                                 klcStorageSceneTetrisTeardownToText(scene.tetris_teardown_mode),
                                 scene.tetris_random_timing ? "true" : "false",
                                 scene.tetris_pause_min_ms,
                                 scene.tetris_pause_max_ms,
                                 scene.tetris_step_min_ms,
                                 scene.tetris_step_max_ms,
                                 scene.start_tetris_group_min,
                                 scene.main_tetris_group_min,
                                 scene.end_tetris_group_min,
                                 scene.start_tetris_group_max,
                                 scene.main_tetris_group_max,
                                 scene.end_tetris_group_max,
                                 scene.start_tetris_gap,
                                 scene.main_tetris_gap,
                                 scene.end_tetris_gap,
                                 klcStorageSceneTetrisTeardownToText(scene.start_tetris_teardown_mode),
                                 klcStorageSceneTetrisTeardownToText(scene.main_tetris_teardown_mode),
                                 klcStorageSceneTetrisTeardownToText(scene.end_tetris_teardown_mode),
                                 scene.start_tetris_random_colors ? "true" : "false",
                                 scene.main_tetris_random_colors ? "true" : "false",
                                 scene.end_tetris_random_colors ? "true" : "false",
                                 scene.start_tetris_reverse_direction ? "true" : "false",
                                 scene.main_tetris_reverse_direction ? "true" : "false",
                                 scene.end_tetris_reverse_direction ? "true" : "false",
                                 scene.start_tetris_mirror_center ? "true" : "false",
                                 scene.main_tetris_mirror_center ? "true" : "false",
                                 scene.end_tetris_mirror_center ? "true" : "false",
                                 scene.start_tetris_random_direction ? "true" : "false",
                                 scene.main_tetris_random_direction ? "true" : "false",
                                 scene.end_tetris_random_direction ? "true" : "false",
                                 scene.tetris2_next_preview ? "true" : "false",
                                 scene.tetris2_sync_segments ? "true" : "false",
                                 scene.tetris2_direction_alternate ? "true" : "false",
                                 scene.tetris2_random_colors ? "true" : "false",
                                 scene.tetris2_reverse_direction ? "true" : "false",
                                 scene.tetris2_block_min,
                                 scene.tetris2_block_max,
                                 klcStorageSceneTetrisTeardownToText(scene.tetris2_teardown_mode),
                                 scene.tetris2_pause_min_ms,
                                 scene.tetris2_pause_max_ms,
                                 scene.tetris2_pixels_per_meter,
                                 scene.tetris2_speed_min_mm_s,
                                 scene.tetris2_speed_max_mm_s,
                                 scene.tetris2_early_start_chance_pct,
                                 scene.tetris2_early_start_min_pct,
                                 scene.tetris2_early_start_max_pct,
                                 scene.tetris2_hsv_min_distance,
                                 scene.sparkle_speed_ms,
                                 scene.start_sparkle_speed_ms,
                                 scene.main_sparkle_speed_ms,
                                 scene.end_sparkle_speed_ms,
                                 scene.sparkle_fill_percent,
                                 scene.start_sparkle_fill_percent,
                                 scene.main_sparkle_fill_percent,
                                 scene.end_sparkle_fill_percent,
                                 scene.sparkle_lifetime_ms,
                                 scene.start_sparkle_lifetime_ms,
                                 scene.main_sparkle_lifetime_ms,
                                 scene.end_sparkle_lifetime_ms,
                                 scene.fireworks_speed,
                                 scene.fireworks_intensity,
                                 scene.tetrix_speed,
                                 scene.tetrix_width);
  }

  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n  ],\n  \"knx\": {\n");
  ok &= klcStorageAppendFormat(buffer, buffer_len, used, "    \"enabled\": %s,\n    \"baudrate\": %lu,\n    \"status_send_mode\": %u,\n    \"status_interval_seconds\": %u,\n    \"global\": {\n      \"global_switch\": %u,\n      \"global_scene\": %u,\n      \"global_next_scene\": %u,\n      \"global_brightness\": %u,\n      \"global_color_r\": %u,\n      \"global_color_g\": %u,\n      \"global_color_b\": %u,\n      \"global_color_w\": %u,\n      \"global_eth_enable\": %u,\n      \"global_wlan_enable\": %u,\n      \"status_scene\": %u,\n      \"status_brightness\": %u,\n      \"status_power_limit\": %u,\n      \"status_error\": %u,\n      \"status_eth_enabled\": %u,\n      \"status_wlan_enabled\": %u\n    },\n    \"outputs\": [\n",
                               cfg.knx.enabled ? "true" : "false",
                               (unsigned long)KLC_BAOS_BAUD_RATE,
                               cfg.knx.status_send_mode,
                               cfg.knx.status_interval_seconds,
                               cfg.knx.global.global_switch,
                               cfg.knx.global.global_scene,
                               cfg.knx.global.global_next_scene,
                               0U,
                               cfg.knx.global.global_color_r,
                               cfg.knx.global.global_color_g,
                               cfg.knx.global.global_color_b,
                               cfg.knx.global.global_color_w,
                               cfg.knx.global.global_eth_enable,
                               cfg.knx.global.global_wlan_enable,
                               cfg.knx.global.status_scene,
                               cfg.knx.global.status_brightness,
                               cfg.knx.global.status_power_limit,
                               cfg.knx.global.status_error,
                               cfg.knx.global.status_eth_enabled,
                               cfg.knx.global.status_wlan_enabled);

  for (uint8_t i = 0; i < cfg.output_count && i < KLC_MAX_OUTPUTS; ++i) {
    const KlcOutputConfig& out = cfg.outputs[i];
    const KlcKnxOutputObjectConfig& map = cfg.knx.outputs[i];
    ok &= klcStorageAppendRaw(buffer, buffer_len, used, i == 0 ? "      {\n" : ",\n      {\n");
    ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                                 "        \"id\": %u,\n        \"name\": ",
                                 out.id);
    ok &= klcStorageAppendEscaped(buffer, buffer_len, used, out.name);
    ok &= klcStorageAppendFormat(buffer, buffer_len, used,
                                 ",\n        \"switch\": %u,\n        \"scene\": %u,\n        \"next_scene\": %u,\n        \"brightness\": %u,\n        \"lock\": %u,\n        \"force\": %u,\n        \"color_r\": %u,\n        \"color_g\": %u,\n        \"color_b\": %u,\n        \"color_w\": %u,\n        \"status_scene\": %u,\n        \"status_brightness\": %u,\n        \"status_lock\": %u,\n        \"status_force\": %u,\n        \"status_switch\": %u,\n        \"status_color_r\": %u,\n        \"status_color_g\": %u,\n        \"status_color_b\": %u,\n        \"status_color_w\": %u,\n        \"lock_active_level\": %u,\n        \"lock_active_action\": %u,\n        \"lock_inactive_action\": %u,\n        \"force_active_level\": %u,\n        \"force_active_action\": %u,\n        \"force_inactive_action\": %u\n      }",
                                 map.switch_obj,
                                 map.scene_obj,
                                 map.next_scene_obj,
                                 map.brightness_obj,
                                 map.lock_obj,
                                 map.force_obj,
                                 map.color_r_obj,
                                 map.color_g_obj,
                                 map.color_b_obj,
                                 map.color_w_obj,
                                 map.status_scene_obj,
                                 map.status_brightness_obj,
                                 map.status_lock_obj,
                                 map.status_force_obj,
                                 map.status_switch_obj,
                                 map.status_color_r_obj,
                                 map.status_color_g_obj,
                                 map.status_color_b_obj,
                                 map.status_color_w_obj,
                                 map.lock_behavior.active_level,
                                 map.lock_behavior.active_action,
                                 map.lock_behavior.inactive_action,
                                 map.force_behavior.active_level,
                                 map.force_behavior.active_action,
                                 map.force_behavior.inactive_action);
  }

  ok &= klcStorageAppendRaw(buffer, buffer_len, used, "\n    ]\n  }\n}\n");

  if (!ok) {
    klcStorageSetLastError(
      measure_only
        ? "Konfigurationsgroesse konnte nicht sicher berechnet werden"
        : (stream_mode ? "Konfigurationsstream wurde abgebrochen"
                       : "Exportpuffer zu klein"));
    return false;
  }

  if (json_len != nullptr) *json_len = used;
  klcStorageSetLastError("ok");
  return true;
}

// Oeffentlicher Export fuer WebUI/Backup: bewusst OHNE WLAN-Passwoerter.
// Nur die lokale Konfigurationsdatei (klcStorageSaveConfig) enthaelt sie.
bool klcStorageExportJson(const KlcDeviceConfig& cfg, char* buffer, size_t buffer_len)
{
  return klcStorageBuildJson(cfg, buffer, buffer_len, false);
}

bool klcStorageMeasureExportJson(const KlcDeviceConfig& cfg,
                                 size_t& json_length)
{
  json_length = 0;
  return klcStorageBuildJson(cfg, nullptr, 0, false, &json_length);
}

bool klcStorageExportJsonStream(const KlcDeviceConfig& cfg,
                                KlcStreamWriter& writer)
{
  if (g_storage_export_stream_writer != nullptr || writer.failed) {
    klcStorageSetLastError("Konfigurationsstream ist bereits aktiv");
    return false;
  }
  g_storage_export_stream_writer = &writer;
  const bool ok = klcStorageBuildJson(
    cfg, &g_storage_export_stream_sentinel, SIZE_MAX, false);
  g_storage_export_stream_writer = nullptr;
  return ok && !writer.failed;
}

// Older factory configurations stored editable display names in German even
// when the device language is now explicitly English. Convert only the exact
// historical factory values; arbitrary user names remain untouched.
