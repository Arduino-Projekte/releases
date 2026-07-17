#include "klc_web_server_internal.h"
#include "klc_scene_public.h"
#include "klc_scene_store.h"
#include <stdlib.h>

static bool klcWebServerReadSceneRequest(uint8_t& scene_id, uint8_t& output_id, bool& has_output)
{
  if (!g_server.hasArg("scene")) {
    return false;
  }

  const int scene = klcWebServerArgToLongStrict("scene");
  if (scene < 0 || klcScenePublicClassify((uint32_t)scene) == KLC_SCENE_PUBLIC_INVALID) {
    return false;
  }

  scene_id = (uint8_t)scene;
  output_id = 0;
  has_output = false;

  if (g_server.hasArg("output")) {
    const int output = klcWebServerArgToLongStrict("output");
    if (output <= 0 || output > 255) {
      return false;
    }
    output_id = (uint8_t)output;
    has_output = true;
  }

  return true;
}

static bool klcWebServerApplySceneRequest(char* message, size_t message_len)
{
  if (g_server.hasArg("action")) {
    String action = g_server.arg("action");
    action.trim();
    if (action == "off" || action == "direct_off") {
      if (g_server.hasArg("output")) {
        const int output = klcWebServerArgToLongStrict("output");
        if (output <= 0 || output > 255) {
          snprintf(message, message_len, "Ausgang ungültig");
          return false;
        }
        const bool ok = klcScenesDirectOffOutput((uint8_t)output);
        snprintf(message, message_len, ok ? "Ausgang %u direkt ausgeschaltet" : "Ausgang %u nicht gefunden", (unsigned)output);
        return ok;
      }

      const bool ok = klcScenesDirectOffGlobal();
      snprintf(message, message_len, ok ? "Alle Ausgänge direkt ausgeschaltet" : "Direkt-Aus fehlgeschlagen");
      return ok;
    }
  }

  uint8_t scene_id = 1U;
  uint8_t output_id = 0;
  bool has_output = false;

  if (!klcWebServerReadSceneRequest(scene_id, output_id, has_output)) {
    snprintf(message, message_len, "ungültige Szenenanforderung");
    return false;
  }

  // Szene 0 ist ausschliesslich der oeffentliche Ausschaltbefehl. Sie bleibt
  // ausserhalb der Szenenkonfiguration und verwendet den prioritaetsgeprueften
  // zentralen Direct-Off-Pfad.
  if (scene_id == 0U) {
    const bool ok = has_output
      ? klcScenesDirectOffOutput(output_id)
      : klcScenesDirectOffGlobal();
    if (has_output) {
      snprintf(message, message_len,
               ok ? "Ausgang %u direkt ausgeschaltet" : "Ausgang %u nicht gefunden",
               output_id);
    } else {
      snprintf(message, message_len,
               ok ? "Alle Ausgaenge direkt ausgeschaltet" : "Direkt-Aus fehlgeschlagen");
    }
    return ok;
  }

  bool ok = false;
  if (has_output) {
    ok = klcScenesStartOutput(output_id, scene_id);
    snprintf(message, message_len, ok ? "Ausgang %u auf Szene %u gesetzt" : "Ausgang %u oder Szene %u ungültig", output_id, scene_id);
  } else {
    ok = klcScenesStartGlobal(scene_id);
    snprintf(message, message_len, ok ? "Globale Szene %u gesetzt" : "Globale Szene %u ungültig", scene_id);
  }

  return ok;
}

// API-Endpunkt für Szenensteuerung.
void klcWebServerHandleSceneApiPost()
{
  char message[96];
  const bool ok = klcWebServerApplySceneRequest(message, sizeof(message));
  klcWebServerSendGenerated(ok ? 200 : 400, "application/json; charset=utf-8", klcWebUiResultJson(ok, message));
}

// Alter Formular-Endpunkt. Danach zur Abläufe-Seite.
void klcWebServerHandleSceneFormPost()
{
  char message[96];
  (void)klcWebServerApplySceneRequest(message, sizeof(message));
  g_server.sendHeader("Location", "/flows");
  g_server.send(303, "text/plain; charset=utf-8", message);
}


// WebUI-Hilfsfunktionen fuer ausgelagertes Nachleuchten: In der Oberfläche
// werden Wellen nur einmal angezeigt; das optionale Nachleuchten wird per
// Checkbox zugeschaltet und intern wieder auf die bisherigen Tail-Effekte
// abgebildet.
static bool klcWebServerArgChecked(const char* name)
{
  if (!g_server.hasArg(name)) {
    return false;
  }
  String value = g_server.arg(name);
  value.trim();
  return !(value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "OFF");
}

static uint8_t klcWebServerPhaseEffectWithTail(uint8_t effect, bool tail)
{
  return klcEffectWithTail(KLC_EFFECT_DOMAIN_PHASE, effect, tail);
}

// Webformular, Import und Laufzeitpruefung verwenden dieselben Grenzen aus
// dem zentralen Effektkatalog. Die Browserpruefung bleibt nur Bedienkomfort.
static bool klcWebServerEffectParamInRange(KlcEffectParamId id, long value)
{
  return value >= INT32_MIN && value <= INT32_MAX &&
         klcEffectParamInRange(id, static_cast<int32_t>(value));
}

static uint8_t klcWebServerMainEffectWithTail(uint8_t effect, bool tail)
{
  return klcEffectWithTail(KLC_EFFECT_DOMAIN_MAIN, effect, tail);
}

struct KlcSceneConfigCommitResult {
  bool saved;
  bool queued;
  bool applied;
  bool restart_required;
  uint64_t operation_id;
  uint32_t revision;
  uint32_t applied_revision;
};

static bool klcWebServerApplySceneConfigRequest(
  char* message, size_t message_len, KlcSceneConfigCommitResult& result)
{
  result = { false, false, false, false, 0U, 0U, 0U };
  if (!g_server.hasArg("id")) {
    snprintf(message, message_len, "Szenen-ID fehlt");
    return false;
  }

  const long id_long = klcWebServerArgToLongStrict("id");
  if (id_long < 0 || !klcScenePublicIsConfigurable((uint32_t)id_long)) {
    snprintf(message, message_len, "Szenen-ID ungültig");
    return false;
  }

  // Nur die ausgewaehlte Szene kopieren. Der bisherige KlcDeviceConfig-
  // Arbeitsbereich (inklusive aller Szenen, Netzwerk-, KNX- und Ausgangsdaten)
  // wird fuer eine reine Ablaufaenderung nicht mehr benoetigt.
  KlcSceneConfig scene = g_config.scenes[(uint8_t)id_long];
  const uint8_t previous_main_effect = scene.main_effect;
  scene.id = (uint8_t)id_long;

  const bool form_seen = g_server.hasArg("name") || g_server.hasArg("r") || g_server.hasArg("g") ||
                         g_server.hasArg("b") || g_server.hasArg("w") || g_server.hasArg("brightness") ||
                         g_server.hasArg("effect_type") || g_server.hasArg("direction") ||
                         g_server.hasArg("pixel_mode") || g_server.hasArg("lit_percent") || g_server.hasArg("start_fill_percent") ||
                         g_server.hasArg("main_fill_percent") || g_server.hasArg("end_fill_percent") || g_server.hasArg("lit_pixels") ||
                         g_server.hasArg("speed_ms") || g_server.hasArg("start_step_ms") || g_server.hasArg("main_step_ms") || g_server.hasArg("end_step_ms") || g_server.hasArg("sync_mode") || g_server.hasArg("global_delay_ms") ||
                         g_server.hasArg("string_segment_start_delay_ms") || g_server.hasArg("string_segment_stop_delay_ms") ||
                         g_server.hasArg("start_effect") || g_server.hasArg("main_effect") || g_server.hasArg("end_effect") ||
                         g_server.hasArg("start_duration_ms") || g_server.hasArg("main_duration_ms") ||
                         g_server.hasArg("end_duration_ms") || g_server.hasArg("transition_duration_ms") ||
                         g_server.hasArg("pulse_period_ms") || g_server.hasArg("start_pulse_period_ms") || g_server.hasArg("main_pulse_period_ms") || g_server.hasArg("end_pulse_period_ms") || g_server.hasArg("segment_percent") || g_server.hasArg("start_segment_percent") || g_server.hasArg("main_segment_percent") || g_server.hasArg("end_segment_percent") ||
                         g_server.hasArg("segment_soft_edge_pixels") || g_server.hasArg("tail_percent") || g_server.hasArg("start_tail_percent") || g_server.hasArg("main_tail_percent") || g_server.hasArg("end_tail_percent") || g_server.hasArg("start_tail") ||
                         g_server.hasArg("main_tail") || g_server.hasArg("end_tail") || g_server.hasArg("wave_bounce") ||
                         g_server.hasArg("start_reverse_direction") || g_server.hasArg("main_reverse_direction") || g_server.hasArg("end_reverse_direction") ||
                         g_server.hasArg("start_mirror_center") || g_server.hasArg("main_mirror_center") || g_server.hasArg("end_mirror_center") ||
                         g_server.hasArg("start_wave_bounce") || g_server.hasArg("main_wave_bounce") || g_server.hasArg("end_wave_bounce") ||
                         g_server.hasArg("tetris_group_min") || g_server.hasArg("tetris_group_max") || g_server.hasArg("tetris_random_colors") ||
                         g_server.hasArg("tetris_reverse_direction") || g_server.hasArg("tetris_mirror_center") ||
                         g_server.hasArg("tetris_random_direction") || g_server.hasArg("tetris_direction") || g_server.hasArg("tetris_gap") ||
                         g_server.hasArg("tetris_teardown_mode") || g_server.hasArg("tetris_random_timing") ||
                         g_server.hasArg("tetris_pause_min_ms") || g_server.hasArg("tetris_pause_max_ms") ||
                         g_server.hasArg("tetris_step_min_ms") || g_server.hasArg("tetris_step_max_ms") ||
                         g_server.hasArg("tetris2_next_preview") || g_server.hasArg("tetris2_sync_segments") || g_server.hasArg("tetris2_direction_alternate") ||
                         g_server.hasArg("tetris2_random_colors") || g_server.hasArg("tetris2_reverse_direction") ||
                         g_server.hasArg("tetris2_block_min") || g_server.hasArg("tetris2_block_max") || g_server.hasArg("tetris2_teardown_mode") ||
                         g_server.hasArg("tetris2_pause_min_ms") || g_server.hasArg("tetris2_pause_max_ms") ||
                         g_server.hasArg("tetris2_pixels_per_meter") || g_server.hasArg("tetris2_speed_min_mm_s") || g_server.hasArg("tetris2_speed_max_mm_s") ||
                         g_server.hasArg("tetris2_early_start_chance_pct") || g_server.hasArg("tetris2_early_start_min_pct") ||
                         g_server.hasArg("tetris2_early_start_max_pct") || g_server.hasArg("tetris2_hsv_min_distance") || g_server.hasArg("sparkle_speed_ms") ||
                         g_server.hasArg("sparkle_fill_percent") || g_server.hasArg("sparkle_lifetime_ms") ||
                         g_server.hasArg("start_sparkle_speed_ms") || g_server.hasArg("main_sparkle_speed_ms") || g_server.hasArg("end_sparkle_speed_ms") ||
                         g_server.hasArg("start_sparkle_fill_percent") || g_server.hasArg("main_sparkle_fill_percent") || g_server.hasArg("end_sparkle_fill_percent") ||
                         g_server.hasArg("start_sparkle_lifetime_ms") || g_server.hasArg("main_sparkle_lifetime_ms") || g_server.hasArg("end_sparkle_lifetime_ms") ||
                         g_server.hasArg("fireworks_speed") || g_server.hasArg("fireworks_intensity") ||
                         g_server.hasArg("tetrix_speed") || g_server.hasArg("tetrix_width") ||
                         g_server.hasArg("flow_ui_v2");

  if (g_server.hasArg("enabled")) {
    const String enabled = g_server.arg("enabled");
    scene.enabled = !(enabled == "0" || enabled == "false" || enabled == "FALSE");
  } else if (form_seen) {
    scene.enabled = false;
  }

  if (g_server.hasArg("in_pool")) {
    const String in_pool = g_server.arg("in_pool");
    scene.in_pool = !(in_pool == "0" || in_pool == "false" || in_pool == "FALSE");
  } else if (form_seen) {
    scene.in_pool = false;
  }

  if (g_server.hasArg("name")) {
    String name = g_server.arg("name");
    name.trim();
    if (name.length() == 0) {
      snprintf(message, message_len, "Szenenname darf nicht leer sein");
      return false;
    }
    name.toCharArray(scene.name, KLC_MAX_NAME_LEN);
  }


  if (g_server.hasArg("effect_type")) {
    const long effect = klcWebServerArgToLongStrict("effect_type");
    if (effect < 0 || effect > UINT8_MAX ||
        !klcEffectIdIsValid(KLC_EFFECT_DOMAIN_LEGACY, static_cast<uint8_t>(effect))) {
      snprintf(message, message_len, "Effekt außerhalb des gültigen Bereichs");
      return false;
    }
    scene.effect_type = (uint8_t)effect;
  }

  if (g_server.hasArg("direction")) {
    const long direction = klcWebServerArgToLongStrict("direction");
    if (direction < KLC_SCENE_DIRECTION_FORWARD || direction > KLC_SCENE_DIRECTION_REVERSE) {
      snprintf(message, message_len, "Richtung außerhalb des gültigen Bereichs");
      return false;
    }
    scene.direction = (uint8_t)direction;
  }

  if (!klcWebServerReadByteArg("r", scene.r, scene.r, message, message_len) ||
      !klcWebServerReadByteArg("g", scene.g, scene.g, message, message_len) ||
      !klcWebServerReadByteArg("b", scene.b, scene.b, message, message_len) ||
      !klcWebServerReadByteArg("w", scene.w, scene.w, message, message_len) ||
      !klcWebServerReadByteArg(
        "brightness", scene.brightness, scene.brightness, message, message_len)) {
    return false;
  }

  if (g_server.hasArg("pixel_mode")) {
    const long mode = klcWebServerArgToLongStrict("pixel_mode");
    if (mode < KLC_SCENE_PIXELS_PERCENT || mode > KLC_SCENE_PIXELS_ABSOLUTE) {
      snprintf(message, message_len, "Pixel-Modus außerhalb des gültigen Bereichs");
      return false;
    }
    scene.pixel_mode = (uint8_t)mode;
  }

  if (g_server.hasArg("lit_percent")) {
    const long lit = klcWebServerArgToLongStrict("lit_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_FILL_PERCENT, lit)) {
      snprintf(message, message_len, "Pixel-Prozent außerhalb 0..100");
      return false;
    }
    scene.lit_percent = (uint8_t)lit;
    if (!g_server.hasArg("start_fill_percent")) scene.start_fill_percent = scene.lit_percent;
    if (!g_server.hasArg("main_fill_percent")) scene.main_fill_percent = scene.lit_percent;
    if (!g_server.hasArg("end_fill_percent")) scene.end_fill_percent = scene.lit_percent;
  }

  if (g_server.hasArg("start_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("start_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Start-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.start_fill_percent = (uint8_t)value;
  }

  if (g_server.hasArg("main_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("main_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Haupt-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.main_fill_percent = (uint8_t)value;
    scene.lit_percent = scene.main_fill_percent;
  }

  if (g_server.hasArg("end_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("end_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Aus-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.end_fill_percent = (uint8_t)value;
  }

  if (g_server.hasArg("lit_pixels")) {
    const long lit = klcWebServerArgToLongStrict("lit_pixels");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_LIT_PIXELS, lit)) {
      snprintf(message, message_len, "Pixel-Anzahl außerhalb 0..5000");
      return false;
    }
    scene.lit_pixels = (uint16_t)lit;
  }

  if (g_server.hasArg("speed_ms")) {
    const long speed = klcWebServerArgToLongStrict("speed_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_STEP_MS, speed)) {
      snprintf(message, message_len, "Tempo außerhalb 0..60000 ms");
      return false;
    }
    scene.speed_ms = (uint16_t)speed;
    if (!g_server.hasArg("start_step_ms")) scene.start_step_ms = scene.speed_ms;
    if (!g_server.hasArg("main_step_ms")) scene.main_step_ms = scene.speed_ms;
    if (!g_server.hasArg("end_step_ms")) scene.end_step_ms = scene.speed_ms;
  }

  if (g_server.hasArg("start_step_ms")) {
    const long value = klcWebServerArgToLongStrict("start_step_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_STEP_MS, value)) {
      snprintf(message, message_len, "Start-Schrittzeit außerhalb 0..60000 ms");
      return false;
    }
    scene.start_step_ms = (uint16_t)value;
  }

  if (g_server.hasArg("main_step_ms")) {
    const long value = klcWebServerArgToLongStrict("main_step_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_STEP_MS, value)) {
      snprintf(message, message_len, "Ein-Schrittzeit außerhalb 0..60000 ms");
      return false;
    }
    scene.main_step_ms = (uint16_t)value;
    scene.speed_ms = scene.main_step_ms;
  }

  if (g_server.hasArg("end_step_ms")) {
    const long value = klcWebServerArgToLongStrict("end_step_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_STEP_MS, value)) {
      snprintf(message, message_len, "Aus-Schrittzeit außerhalb 0..60000 ms");
      return false;
    }
    scene.end_step_ms = (uint16_t)value;
  }

  if (g_server.hasArg("global_delay_ms")) {
    const long delay = klcWebServerArgToLongStrict("global_delay_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_GLOBAL_DELAY_MS, delay)) {
      snprintf(message, message_len, "Zeitversatz außerhalb 0..60000 ms");
      return false;
    }
    scene.global_delay_ms = (uint16_t)delay;
  }

  if (g_server.hasArg("string_segment_start_delay_ms")) {
    const long delay = klcWebServerArgToLongStrict("string_segment_start_delay_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_STRING_SEGMENT_START_DELAY_MS, delay)) {
      snprintf(message, message_len, "String-Segment-Startversatz außerhalb 0..60000 ms");
      return false;
    }
    scene.string_segment_start_delay_ms = (uint16_t)delay;
  }

  if (g_server.hasArg("string_segment_stop_delay_ms")) {
    const long delay = klcWebServerArgToLongStrict("string_segment_stop_delay_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_STRING_SEGMENT_STOP_DELAY_MS, delay)) {
      snprintf(message, message_len, "String-Segment-Stopversatz außerhalb 0..60000 ms");
      return false;
    }
    scene.string_segment_stop_delay_ms = (uint16_t)delay;
  }

  if (g_server.hasArg("sync_mode")) {
    const long sync = klcWebServerArgToLongStrict("sync_mode");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SYNC_MODE, sync)) {
      snprintf(message, message_len, "Synchronisationsart außerhalb des gültigen Bereichs");
      return false;
    }
    scene.sync_mode = (uint8_t)sync;
  }


  if (g_server.hasArg("start_effect")) {
    const long effect = klcWebServerArgToLongStrict("start_effect");
    if (effect < 0 || effect > UINT8_MAX || !klcEffectIdIsValid(KLC_EFFECT_DOMAIN_PHASE, (uint8_t)effect)) {
      snprintf(message, message_len, "Start-Effekt außerhalb des gültigen Bereichs");
      return false;
    }
    uint8_t parsed_effect = (uint8_t)effect;
    if (g_server.hasArg("flow_ui_v2")) {
      parsed_effect = klcWebServerPhaseEffectWithTail(parsed_effect, klcWebServerArgChecked("start_tail"));
    }
    scene.start_effect = parsed_effect;
  }

  if (g_server.hasArg("main_effect")) {
    const long effect = klcWebServerArgToLongStrict("main_effect");
    if (effect < 0 || effect > UINT8_MAX || !klcEffectIdIsValid(KLC_EFFECT_DOMAIN_MAIN, (uint8_t)effect)) {
      snprintf(message, message_len, "Haupt-Effekt außerhalb des gültigen Bereichs");
      return false;
    }
    uint8_t parsed_effect = (uint8_t)effect;
    if (g_server.hasArg("flow_ui_v2")) {
      parsed_effect = klcWebServerMainEffectWithTail(parsed_effect, klcWebServerArgChecked("main_tail"));
    }
    scene.main_effect = parsed_effect;
  }

  if (g_server.hasArg("end_effect")) {
    const long effect = klcWebServerArgToLongStrict("end_effect");
    if (effect < 0 || effect > UINT8_MAX || !klcEffectIdIsValid(KLC_EFFECT_DOMAIN_PHASE, (uint8_t)effect)) {
      snprintf(message, message_len, "Aus-Effekt außerhalb des gültigen Bereichs");
      return false;
    }
    uint8_t parsed_effect = (uint8_t)effect;
    if (g_server.hasArg("flow_ui_v2")) {
      parsed_effect = klcWebServerPhaseEffectWithTail(parsed_effect, klcWebServerArgChecked("end_tail"));
    }
    scene.end_effect = parsed_effect;
  }

  if (g_server.hasArg("start_effect") || g_server.hasArg("flow_ui_v2")) {
    scene.start_reverse_direction = klcScenePhaseMotionReverseFromEffect(scene.start_effect);
    scene.start_mirror_center = klcScenePhaseMotionMirrorFromEffect(scene.start_effect);
  }
  if (g_server.hasArg("main_effect") || g_server.hasArg("flow_ui_v2")) {
    scene.main_reverse_direction = klcSceneMainMotionReverseFromEffect(scene.main_effect);
    scene.main_mirror_center = klcSceneMainMotionMirrorFromEffect(scene.main_effect);
  }
  if (g_server.hasArg("end_effect") || g_server.hasArg("flow_ui_v2")) {
    scene.end_reverse_direction = klcScenePhaseMotionReverseFromEffect(scene.end_effect);
    scene.end_mirror_center = klcScenePhaseMotionMirrorFromEffect(scene.end_effect);
  }

  if (g_server.hasArg("start_reverse_direction")) { scene.start_reverse_direction = klcWebServerArgChecked("start_reverse_direction"); scene.start_effect = klcScenePhaseMotionApplyFlags(scene.start_effect, scene.start_reverse_direction, scene.start_mirror_center); }
  if (g_server.hasArg("main_reverse_direction")) { scene.main_reverse_direction = klcWebServerArgChecked("main_reverse_direction"); scene.main_effect = klcSceneMainMotionApplyFlags(scene.main_effect, scene.main_reverse_direction, scene.main_mirror_center); }
  if (g_server.hasArg("end_reverse_direction")) { scene.end_reverse_direction = klcWebServerArgChecked("end_reverse_direction"); scene.end_effect = klcScenePhaseMotionApplyFlags(scene.end_effect, scene.end_reverse_direction, scene.end_mirror_center); }
  if (g_server.hasArg("start_mirror_center")) { scene.start_mirror_center = klcWebServerArgChecked("start_mirror_center"); scene.start_effect = klcScenePhaseMotionApplyFlags(scene.start_effect, scene.start_reverse_direction, scene.start_mirror_center); }
  if (g_server.hasArg("main_mirror_center")) { scene.main_mirror_center = klcWebServerArgChecked("main_mirror_center"); scene.main_effect = klcSceneMainMotionApplyFlags(scene.main_effect, scene.main_reverse_direction, scene.main_mirror_center); }
  if (g_server.hasArg("end_mirror_center")) { scene.end_mirror_center = klcWebServerArgChecked("end_mirror_center"); scene.end_effect = klcScenePhaseMotionApplyFlags(scene.end_effect, scene.end_reverse_direction, scene.end_mirror_center); }

  // Wechsel auf Pulsation: Bei sehr kleinem Ein-Fuellgrad waere die Wirkung
  // schwer sichtbar. Deshalb wird nur beim echten Wechsel auf Pulsation der
  // Ein-Fuellgrad einmalig auf 100 %% gesetzt. Danach darf der Nutzer ihn
  // manuell wieder reduzieren, solange Pulsation aktiv bleibt.
  if (g_server.hasArg("main_effect") && previous_main_effect != KLC_SCENE_MAIN_SOFT_PULSE &&
      scene.main_effect == KLC_SCENE_MAIN_SOFT_PULSE && scene.main_fill_percent < 50U) {
    scene.main_fill_percent = 100U;
    scene.lit_percent = 100U;
  }

  if (g_server.hasArg("start_duration_ms")) {
    const long value = klcWebServerArgToLongStrict("start_duration_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_DURATION_MS, value)) {
      snprintf(message, message_len, "Start-Dauer außerhalb 0..86400000 ms");
      return false;
    }
    scene.start_duration_ms = (uint32_t)value;
  }

  if (g_server.hasArg("main_duration_ms")) {
    const long value = klcWebServerArgToLongStrict("main_duration_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_DURATION_MS, value)) {
      snprintf(message, message_len, "Hauptdauer außerhalb 0..86400000 ms");
      return false;
    }
    scene.main_duration_ms = (uint32_t)value;
  }

  if (g_server.hasArg("end_duration_ms")) {
    const long value = klcWebServerArgToLongStrict("end_duration_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_DURATION_MS, value)) {
      snprintf(message, message_len, "Aus-Dauer außerhalb 0..86400000 ms");
      return false;
    }
    scene.end_duration_ms = (uint32_t)value;
  }

  if (g_server.hasArg("transition_duration_ms")) {
    const long value = klcWebServerArgToLongStrict("transition_duration_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TRANSITION_DURATION_MS, value)) {
      snprintf(message, message_len, "Übergangs-Dauer außerhalb 0..60000 ms");
      return false;
    }
    scene.transition_duration_ms = (uint32_t)value;
  }

  if (g_server.hasArg("pulse_period_ms")) {
    const long value = klcWebServerArgToLongStrict("pulse_period_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_PULSE_PERIOD_MS, value)) {
      snprintf(message, message_len, "Pulsperiode außerhalb 0..86400000 ms");
      return false;
    }
    scene.pulse_period_ms = (uint32_t)value;
  }

  if (g_server.hasArg("start_pulse_period_ms")) {
    const long value = klcWebServerArgToLongStrict("start_pulse_period_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_PULSE_PERIOD_MS, value)) {
      snprintf(message, message_len, "Start-Pulsperiode außerhalb 0..86400000 ms");
      return false;
    }
    scene.start_pulse_period_ms = (uint32_t)value;
  }

  if (g_server.hasArg("main_pulse_period_ms")) {
    const long value = klcWebServerArgToLongStrict("main_pulse_period_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_PULSE_PERIOD_MS, value)) {
      snprintf(message, message_len, "Ein-Pulsperiode außerhalb 0..86400000 ms");
      return false;
    }
    scene.main_pulse_period_ms = (uint32_t)value;
    scene.pulse_period_ms = scene.main_pulse_period_ms;
  } else {
    scene.main_pulse_period_ms = scene.pulse_period_ms;
  }

  if (g_server.hasArg("end_pulse_period_ms")) {
    const long value = klcWebServerArgToLongStrict("end_pulse_period_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_PULSE_PERIOD_MS, value)) {
      snprintf(message, message_len, "Aus-Pulsperiode außerhalb 0..86400000 ms");
      return false;
    }
    scene.end_pulse_period_ms = (uint32_t)value;
  }

  if (!g_server.hasArg("start_pulse_period_ms") && g_server.hasArg("pulse_period_ms")) scene.start_pulse_period_ms = scene.pulse_period_ms;
  if (!g_server.hasArg("end_pulse_period_ms") && g_server.hasArg("pulse_period_ms")) scene.end_pulse_period_ms = scene.pulse_period_ms;

  if (g_server.hasArg("segment_percent")) {
    const long value = klcWebServerArgToLongStrict("segment_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SEGMENT_PIXELS, value)) {
      snprintf(message, message_len, "Segmentgröße außerhalb 0..255 LEDs");
      return false;
    }
    scene.segment_percent = (uint8_t)value;
  }
  if (!g_server.hasArg("start_segment_percent")) scene.start_segment_percent = scene.segment_percent;
  if (!g_server.hasArg("main_segment_percent")) scene.main_segment_percent = scene.segment_percent;
  if (!g_server.hasArg("end_segment_percent")) scene.end_segment_percent = scene.segment_percent;
  if (g_server.hasArg("start_segment_percent")) {
    const long value = klcWebServerArgToLongStrict("start_segment_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SEGMENT_PIXELS, value)) {
      snprintf(message, message_len, "Start-Segment außerhalb 0..255 LEDs");
      return false;
    }
    scene.start_segment_percent = (uint8_t)value;
  }
  if (g_server.hasArg("main_segment_percent")) {
    const long value = klcWebServerArgToLongStrict("main_segment_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SEGMENT_PIXELS, value)) {
      snprintf(message, message_len, "Ein-Segment außerhalb 0..255 LEDs");
      return false;
    }
    scene.main_segment_percent = (uint8_t)value;
    scene.segment_percent = scene.main_segment_percent;
  }
  if (g_server.hasArg("end_segment_percent")) {
    const long value = klcWebServerArgToLongStrict("end_segment_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SEGMENT_PIXELS, value)) {
      snprintf(message, message_len, "Aus-Segment außerhalb 0..255 LEDs");
      return false;
    }
    scene.end_segment_percent = (uint8_t)value;
  }

  if (g_server.hasArg("segment_soft_edge_pixels")) {
    const long value = klcWebServerArgToLongStrict("segment_soft_edge_pixels");
    uint8_t max_segment = scene.start_segment_percent;
    if (scene.main_segment_percent > max_segment) max_segment = scene.main_segment_percent;
    if (scene.end_segment_percent > max_segment) max_segment = scene.end_segment_percent;
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SEGMENT_SOFT_EDGE_PIXELS, value)) {
      snprintf(message, message_len, "Weiche Segmentkante außerhalb 0..255 LEDs");
      return false;
    }
    if (value > max_segment) {
      snprintf(message, message_len, "Weiche Segmentkante darf nicht groesser als das groesste Segment sein");
      return false;
    }
    scene.segment_soft_edge_pixels = (uint8_t)value;
  }

  if (g_server.hasArg("tail_percent")) {
    const long value = klcWebServerArgToLongStrict("tail_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TAIL_PERCENT, value)) {
      snprintf(message, message_len, "Nachleuchten außerhalb 0..200 Prozent");
      return false;
    }
    scene.tail_percent = (uint8_t)value;
    if (!g_server.hasArg("start_tail_percent")) scene.start_tail_percent = scene.tail_percent;
    if (!g_server.hasArg("main_tail_percent")) scene.main_tail_percent = scene.tail_percent;
    if (!g_server.hasArg("end_tail_percent")) scene.end_tail_percent = scene.tail_percent;
  }

  if (g_server.hasArg("start_tail_percent")) {
    const long value = klcWebServerArgToLongStrict("start_tail_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TAIL_PERCENT, value)) {
      snprintf(message, message_len, "Start-Nachleuchten außerhalb 0..200 Prozent");
      return false;
    }
    scene.start_tail_percent = (uint8_t)value;
  }

  if (g_server.hasArg("main_tail_percent")) {
    const long value = klcWebServerArgToLongStrict("main_tail_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TAIL_PERCENT, value)) {
      snprintf(message, message_len, "Haupt-Nachleuchten außerhalb 0..200 Prozent");
      return false;
    }
    scene.main_tail_percent = (uint8_t)value;
    scene.tail_percent = scene.main_tail_percent;
  }

  if (g_server.hasArg("end_tail_percent")) {
    const long value = klcWebServerArgToLongStrict("end_tail_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TAIL_PERCENT, value)) {
      snprintf(message, message_len, "Aus-Nachleuchten außerhalb 0..200 Prozent");
      return false;
    }
    scene.end_tail_percent = (uint8_t)value;
  }

  // Checkboxen werden bei HTML-Formularen nur gesendet, wenn sie aktiv sind.
  // flow_ui_v2 zeigt an, dass die komplette Abläufe-Karte gesendet wurde;
  // dann darf ein fehlender Haken gezielt als false gespeichert werden.
  if (g_server.hasArg("wave_bounce") || g_server.hasArg("flow_ui_v2")) {
    scene.wave_bounce = klcWebServerArgChecked("wave_bounce");
    scene.main_wave_bounce = scene.wave_bounce;
    if (!g_server.hasArg("start_wave_bounce")) scene.start_wave_bounce = scene.wave_bounce;
    if (!g_server.hasArg("end_wave_bounce")) scene.end_wave_bounce = scene.wave_bounce;
  }
  if (g_server.hasArg("start_wave_bounce")) { scene.start_wave_bounce = klcWebServerArgChecked("start_wave_bounce"); }
  if (g_server.hasArg("main_wave_bounce")) { scene.main_wave_bounce = klcWebServerArgChecked("main_wave_bounce"); scene.wave_bounce = scene.main_wave_bounce; }
  if (g_server.hasArg("end_wave_bounce")) { scene.end_wave_bounce = klcWebServerArgChecked("end_wave_bounce"); }

  if (g_server.hasArg("tetris_group_min")) {
    const long value = klcWebServerArgToLongStrict("tetris_group_min");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MIN, value)) {
      snprintf(message, message_len, "Tetris-Pixelgruppe min. außerhalb 1..99 Pixel");
      return false;
    }
    scene.tetris_group_min = (uint8_t)value;
  }

  if (g_server.hasArg("tetris_group_max")) {
    const long value = klcWebServerArgToLongStrict("tetris_group_max");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MAX, value)) {
      snprintf(message, message_len, "Tetris-Pixelgruppe max. außerhalb 1..99 Pixel");
      return false;
    }
    scene.tetris_group_max = (uint8_t)value;
  }
  if (scene.tetris_group_max < scene.tetris_group_min) {
    scene.tetris_group_max = scene.tetris_group_min;
  }

  if (g_server.hasArg("tetris_random_colors") || g_server.hasArg("flow_ui_v2")) {
    scene.tetris_random_colors = klcWebServerArgChecked("tetris_random_colors");
  }

  if (g_server.hasArg("tetris_direction")) {
    const long value = klcWebServerArgToLongStrict("tetris_direction");
    if (value < KLC_SCENE_TETRIS_1_TO_X || value > KLC_SCENE_TETRIS_FROM_CENTER) {
      snprintf(message, message_len, "Tetris-Richtung außerhalb des gültigen Bereichs");
      return false;
    }
    scene.tetris_direction = (uint8_t)value;
    klcSceneTetrisFlagsFromDirection(scene.tetris_direction, scene.tetris_reverse_direction, scene.tetris_mirror_center, scene.tetris_random_direction);
  }

  if (g_server.hasArg("tetris_reverse_direction") || g_server.hasArg("tetris_mirror_center") || g_server.hasArg("tetris_random_direction") || g_server.hasArg("flow_ui_v2")) {
    scene.tetris_reverse_direction = klcWebServerArgChecked("tetris_reverse_direction");
    scene.tetris_mirror_center = klcWebServerArgChecked("tetris_mirror_center");
    scene.tetris_random_direction = klcWebServerArgChecked("tetris_random_direction");
    scene.tetris_direction = scene.tetris_random_direction ? static_cast<uint8_t>(KLC_SCENE_TETRIS_RANDOM) : klcSceneTetrisDirectionFromFlags(scene.tetris_reverse_direction, scene.tetris_mirror_center);
  }

  if (g_server.hasArg("tetris_gap")) {
    const long value = klcWebServerArgToLongStrict("tetris_gap");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GAP, value)) {
      snprintf(message, message_len, "Tetris-Abstand außerhalb 0..5 Pixel");
      return false;
    }
    scene.tetris_gap = (uint8_t)value;
  }

  if (g_server.hasArg("tetris_teardown_mode")) {
    const long value = klcWebServerArgToLongStrict("tetris_teardown_mode");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_TEARDOWN_MODE, value)) {
      snprintf(message, message_len, "Tetris-Übergang außerhalb des gültigen Bereichs");
      return false;
    }
    scene.tetris_teardown_mode = (uint8_t)value;
  }

  if (g_server.hasArg("tetris_random_timing") || g_server.hasArg("flow_ui_v2")) {
    scene.tetris_random_timing = klcWebServerArgChecked("tetris_random_timing");
  }
  if (g_server.hasArg("tetris_pause_min_ms")) {
    const long value = klcWebServerArgToLongStrict("tetris_pause_min_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_PAUSE_MIN_MS, value)) { snprintf(message, message_len, "Tetris-Pause min. außerhalb 0..10000 ms"); return false; }
    scene.tetris_pause_min_ms = (uint16_t)value;
  }
  if (g_server.hasArg("tetris_pause_max_ms")) {
    const long value = klcWebServerArgToLongStrict("tetris_pause_max_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_PAUSE_MAX_MS, value)) { snprintf(message, message_len, "Tetris-Pause max. außerhalb 0..10000 ms"); return false; }
    scene.tetris_pause_max_ms = (uint16_t)value;
  }
  if (scene.tetris_pause_max_ms < scene.tetris_pause_min_ms) scene.tetris_pause_max_ms = scene.tetris_pause_min_ms;
  if (g_server.hasArg("tetris_step_min_ms")) {
    const long value = klcWebServerArgToLongStrict("tetris_step_min_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_STEP_MIN_MS, value)) { snprintf(message, message_len, "Tetris-Fallzeit min. außerhalb 0..10000 ms"); return false; }
    scene.tetris_step_min_ms = (uint16_t)value;
  }
  if (g_server.hasArg("tetris_step_max_ms")) {
    const long value = klcWebServerArgToLongStrict("tetris_step_max_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_STEP_MAX_MS, value)) { snprintf(message, message_len, "Tetris-Fallzeit max. außerhalb 0..10000 ms"); return false; }
    scene.tetris_step_max_ms = (uint16_t)value;
  }
  if (scene.tetris_step_max_ms < scene.tetris_step_min_ms) scene.tetris_step_max_ms = scene.tetris_step_min_ms;

  if (g_server.hasArg("start_tetris_group_min")) {
    const long value = klcWebServerArgToLongStrict("start_tetris_group_min");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MIN, value)) { snprintf(message, message_len, "Start-Tetris-Pixelgruppe min. außerhalb 1..99 Pixel"); return false; }
    scene.start_tetris_group_min = (uint8_t)value;
  }
  if (g_server.hasArg("main_tetris_group_min")) {
    const long value = klcWebServerArgToLongStrict("main_tetris_group_min");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MIN, value)) { snprintf(message, message_len, "Ein-Tetris-Pixelgruppe min. außerhalb 1..99 Pixel"); return false; }
    scene.main_tetris_group_min = (uint8_t)value;
    scene.tetris_group_min = scene.main_tetris_group_min;
  }
  if (g_server.hasArg("end_tetris_group_min")) {
    const long value = klcWebServerArgToLongStrict("end_tetris_group_min");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MIN, value)) { snprintf(message, message_len, "Aus-Tetris-Pixelgruppe min. außerhalb 1..99 Pixel"); return false; }
    scene.end_tetris_group_min = (uint8_t)value;
  }
  if (g_server.hasArg("start_tetris_group_max")) {
    const long value = klcWebServerArgToLongStrict("start_tetris_group_max");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MAX, value)) { snprintf(message, message_len, "Start-Tetris-Pixelgruppe max. außerhalb 1..99 Pixel"); return false; }
    scene.start_tetris_group_max = (uint8_t)value;
  }
  if (g_server.hasArg("main_tetris_group_max")) {
    const long value = klcWebServerArgToLongStrict("main_tetris_group_max");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MAX, value)) { snprintf(message, message_len, "Ein-Tetris-Pixelgruppe max. außerhalb 1..99 Pixel"); return false; }
    scene.main_tetris_group_max = (uint8_t)value;
    scene.tetris_group_max = scene.main_tetris_group_max;
  }
  if (g_server.hasArg("end_tetris_group_max")) {
    const long value = klcWebServerArgToLongStrict("end_tetris_group_max");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GROUP_MAX, value)) { snprintf(message, message_len, "Aus-Tetris-Pixelgruppe max. außerhalb 1..99 Pixel"); return false; }
    scene.end_tetris_group_max = (uint8_t)value;
  }
  if (scene.start_tetris_group_max < scene.start_tetris_group_min) scene.start_tetris_group_max = scene.start_tetris_group_min;
  if (scene.main_tetris_group_max < scene.main_tetris_group_min) { scene.main_tetris_group_max = scene.main_tetris_group_min; scene.tetris_group_max = scene.main_tetris_group_max; }
  if (scene.end_tetris_group_max < scene.end_tetris_group_min) scene.end_tetris_group_max = scene.end_tetris_group_min;

  if (g_server.hasArg("start_tetris_gap")) {
    const long value = klcWebServerArgToLongStrict("start_tetris_gap");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GAP, value)) { snprintf(message, message_len, "Start-Tetris-Abstand außerhalb 0..5 Pixel"); return false; }
    scene.start_tetris_gap = (uint8_t)value;
  }
  if (g_server.hasArg("main_tetris_gap")) {
    const long value = klcWebServerArgToLongStrict("main_tetris_gap");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GAP, value)) { snprintf(message, message_len, "Ein-Tetris-Abstand außerhalb 0..5 Pixel"); return false; }
    scene.main_tetris_gap = (uint8_t)value;
    scene.tetris_gap = scene.main_tetris_gap;
  }
  if (g_server.hasArg("end_tetris_gap")) {
    const long value = klcWebServerArgToLongStrict("end_tetris_gap");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_GAP, value)) { snprintf(message, message_len, "Aus-Tetris-Abstand außerhalb 0..5 Pixel"); return false; }
    scene.end_tetris_gap = (uint8_t)value;
  }

  if (g_server.hasArg("start_tetris_teardown_mode")) {
    const long value = klcWebServerArgToLongStrict("start_tetris_teardown_mode");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_TEARDOWN_MODE, value)) { snprintf(message, message_len, "Start-Tetris-Übergang außerhalb des gültigen Bereichs"); return false; }
    scene.start_tetris_teardown_mode = (uint8_t)value;
  }
  if (g_server.hasArg("main_tetris_teardown_mode")) {
    const long value = klcWebServerArgToLongStrict("main_tetris_teardown_mode");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_TEARDOWN_MODE, value)) { snprintf(message, message_len, "Ein-Tetris-Übergang außerhalb des gültigen Bereichs"); return false; }
    scene.main_tetris_teardown_mode = (uint8_t)value;
    scene.tetris_teardown_mode = scene.main_tetris_teardown_mode;
  }
  if (g_server.hasArg("end_tetris_teardown_mode")) {
    const long value = klcWebServerArgToLongStrict("end_tetris_teardown_mode");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS_TEARDOWN_MODE, value)) { snprintf(message, message_len, "Aus-Tetris-Übergang außerhalb des gültigen Bereichs"); return false; }
    scene.end_tetris_teardown_mode = (uint8_t)value;
  }

  if (g_server.hasArg("start_tetris_random_colors") || g_server.hasArg("flow_ui_v2")) scene.start_tetris_random_colors = klcWebServerArgChecked("start_tetris_random_colors");
  if (g_server.hasArg("main_tetris_random_colors") || g_server.hasArg("flow_ui_v2")) { scene.main_tetris_random_colors = klcWebServerArgChecked("main_tetris_random_colors"); scene.tetris_random_colors = scene.main_tetris_random_colors; }
  if (g_server.hasArg("end_tetris_random_colors") || g_server.hasArg("flow_ui_v2")) scene.end_tetris_random_colors = klcWebServerArgChecked("end_tetris_random_colors");
  if (g_server.hasArg("start_tetris_reverse_direction") || g_server.hasArg("flow_ui_v2")) scene.start_tetris_reverse_direction = klcWebServerArgChecked("start_tetris_reverse_direction");
  if (g_server.hasArg("main_tetris_reverse_direction") || g_server.hasArg("flow_ui_v2")) { scene.main_tetris_reverse_direction = klcWebServerArgChecked("main_tetris_reverse_direction"); scene.tetris_reverse_direction = scene.main_tetris_reverse_direction; }
  if (g_server.hasArg("end_tetris_reverse_direction") || g_server.hasArg("flow_ui_v2")) scene.end_tetris_reverse_direction = klcWebServerArgChecked("end_tetris_reverse_direction");
  if (g_server.hasArg("start_tetris_mirror_center") || g_server.hasArg("flow_ui_v2")) scene.start_tetris_mirror_center = klcWebServerArgChecked("start_tetris_mirror_center");
  if (g_server.hasArg("main_tetris_mirror_center") || g_server.hasArg("flow_ui_v2")) { scene.main_tetris_mirror_center = klcWebServerArgChecked("main_tetris_mirror_center"); scene.tetris_mirror_center = scene.main_tetris_mirror_center; }
  if (g_server.hasArg("end_tetris_mirror_center") || g_server.hasArg("flow_ui_v2")) scene.end_tetris_mirror_center = klcWebServerArgChecked("end_tetris_mirror_center");
  if (g_server.hasArg("start_tetris_random_direction") || g_server.hasArg("flow_ui_v2")) scene.start_tetris_random_direction = klcWebServerArgChecked("start_tetris_random_direction");
  if (g_server.hasArg("main_tetris_random_direction") || g_server.hasArg("flow_ui_v2")) { scene.main_tetris_random_direction = klcWebServerArgChecked("main_tetris_random_direction"); scene.tetris_random_direction = scene.main_tetris_random_direction; }
  if (g_server.hasArg("end_tetris_random_direction") || g_server.hasArg("flow_ui_v2")) scene.end_tetris_random_direction = klcWebServerArgChecked("end_tetris_random_direction");
  scene.tetris_direction = scene.tetris_random_direction ? static_cast<uint8_t>(KLC_SCENE_TETRIS_RANDOM) : klcSceneTetrisDirectionFromFlags(scene.tetris_reverse_direction, scene.tetris_mirror_center);

  if (g_server.hasArg("tetris2_next_preview") || g_server.hasArg("flow_ui_v2")) scene.tetris2_next_preview = klcWebServerArgChecked("tetris2_next_preview");
  if (g_server.hasArg("tetris2_sync_segments") || g_server.hasArg("flow_ui_v2")) scene.tetris2_sync_segments = klcWebServerArgChecked("tetris2_sync_segments");
  if (g_server.hasArg("tetris2_direction_alternate") || g_server.hasArg("flow_ui_v2")) scene.tetris2_direction_alternate = klcWebServerArgChecked("tetris2_direction_alternate");
  if (g_server.hasArg("tetris2_random_colors") || g_server.hasArg("flow_ui_v2")) scene.tetris2_random_colors = klcWebServerArgChecked("tetris2_random_colors");
  if (g_server.hasArg("tetris2_reverse_direction") || g_server.hasArg("flow_ui_v2")) scene.tetris2_reverse_direction = klcWebServerArgChecked("tetris2_reverse_direction");
  if (g_server.hasArg("tetris2_block_min")) {
    const long value = klcWebServerArgToLongStrict("tetris2_block_min");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_BLOCK_MIN, value)) { snprintf(message, message_len, "Tetris_2 Blockgröße min. außerhalb 1..99"); return false; }
    scene.tetris2_block_min = (uint8_t)value;
  }
  if (g_server.hasArg("tetris2_block_max")) {
    const long value = klcWebServerArgToLongStrict("tetris2_block_max");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_BLOCK_MAX, value)) { snprintf(message, message_len, "Tetris_2 Blockgröße max. außerhalb 1..99"); return false; }
    scene.tetris2_block_max = (uint8_t)value;
  }
  if (scene.tetris2_block_max < scene.tetris2_block_min) scene.tetris2_block_max = scene.tetris2_block_min;
  if (g_server.hasArg("tetris2_teardown_mode")) {
    const long value = klcWebServerArgToLongStrict("tetris2_teardown_mode");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_TEARDOWN_MODE, value)) { snprintf(message, message_len, "Tetris_2 Abbauart ungültig"); return false; }
    scene.tetris2_teardown_mode = (uint8_t)value;
  }
  if (g_server.hasArg("tetris2_pause_min_ms")) {
    const long value = klcWebServerArgToLongStrict("tetris2_pause_min_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_PAUSE_MIN_MS, value)) { snprintf(message, message_len, "Tetris_2 Pause min. außerhalb 0..10000 ms"); return false; }
    scene.tetris2_pause_min_ms = (uint16_t)value;
  }
  if (g_server.hasArg("tetris2_pause_max_ms")) {
    const long value = klcWebServerArgToLongStrict("tetris2_pause_max_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_PAUSE_MAX_MS, value)) { snprintf(message, message_len, "Tetris_2 Pause max. außerhalb 0..10000 ms"); return false; }
    scene.tetris2_pause_max_ms = (uint16_t)value;
  }
  if (scene.tetris2_pause_max_ms < scene.tetris2_pause_min_ms) scene.tetris2_pause_max_ms = scene.tetris2_pause_min_ms;
  if (g_server.hasArg("tetris2_pixels_per_meter")) {
    const long value = klcWebServerArgToLongStrict("tetris2_pixels_per_meter");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_PIXELS_PER_METER, value)) { snprintf(message, message_len, "Tetris_2 Pixel/Meter außerhalb 1..2000"); return false; }
    scene.tetris2_pixels_per_meter = (uint16_t)value;
  }
  if (g_server.hasArg("tetris2_speed_min_mm_s")) {
    const long value = klcWebServerArgToLongStrict("tetris2_speed_min_mm_s");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_SPEED_MIN_MM_S, value)) { snprintf(message, message_len, "Tetris_2 Geschwindigkeit min. außerhalb 1..20000 mm/s"); return false; }
    scene.tetris2_speed_min_mm_s = (uint16_t)value;
  }
  if (g_server.hasArg("tetris2_speed_max_mm_s")) {
    const long value = klcWebServerArgToLongStrict("tetris2_speed_max_mm_s");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_SPEED_MAX_MM_S, value)) { snprintf(message, message_len, "Tetris_2 Geschwindigkeit max. außerhalb 1..20000 mm/s"); return false; }
    scene.tetris2_speed_max_mm_s = (uint16_t)value;
  }
  if (scene.tetris2_speed_max_mm_s < scene.tetris2_speed_min_mm_s) scene.tetris2_speed_max_mm_s = scene.tetris2_speed_min_mm_s;
  if (g_server.hasArg("tetris2_early_start_chance_pct")) {
    const long value = klcWebServerArgToLongStrict("tetris2_early_start_chance_pct");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_EARLY_START_CHANCE_PCT, value)) { snprintf(message, message_len, "Tetris_2 Frühstart-Wahrscheinlichkeit außerhalb 0..100 %%"); return false; }
    scene.tetris2_early_start_chance_pct = (uint8_t)value;
  }
  if (g_server.hasArg("tetris2_early_start_min_pct")) {
    const long value = klcWebServerArgToLongStrict("tetris2_early_start_min_pct");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_EARLY_START_MIN_PCT, value)) { snprintf(message, message_len, "Tetris_2 Frühstart min. außerhalb 0..95 %%"); return false; }
    scene.tetris2_early_start_min_pct = (uint8_t)value;
  }
  if (g_server.hasArg("tetris2_early_start_max_pct")) {
    const long value = klcWebServerArgToLongStrict("tetris2_early_start_max_pct");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_EARLY_START_MAX_PCT, value)) { snprintf(message, message_len, "Tetris_2 Frühstart max. außerhalb 0..95 %%"); return false; }
    scene.tetris2_early_start_max_pct = (uint8_t)value;
  }
  if (scene.tetris2_early_start_max_pct < scene.tetris2_early_start_min_pct) scene.tetris2_early_start_max_pct = scene.tetris2_early_start_min_pct;
  if (g_server.hasArg("tetris2_hsv_min_distance")) {
    const long value = klcWebServerArgToLongStrict("tetris2_hsv_min_distance");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_TETRIS2_HSV_MIN_DISTANCE, value)) { snprintf(message, message_len, "Tetris_2 HSV-Mindestabstand außerhalb 0..180"); return false; }
    scene.tetris2_hsv_min_distance = (uint8_t)value;
  }

  if (g_server.hasArg("sparkle_speed_ms")) {
    const long value = klcWebServerArgToLongStrict("sparkle_speed_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_SPEED_MS, value)) {
      snprintf(message, message_len, "Funkel-Geschwindigkeit außerhalb 0..60000 ms");
      return false;
    }
    scene.sparkle_speed_ms = (uint16_t)value;
  }
  if (g_server.hasArg("start_sparkle_speed_ms")) {
    const long value = klcWebServerArgToLongStrict("start_sparkle_speed_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_SPEED_MS, value)) {
      snprintf(message, message_len, "Start-Funkel-Geschwindigkeit außerhalb 0..60000 ms");
      return false;
    }
    scene.start_sparkle_speed_ms = (uint16_t)value;
  }
  if (g_server.hasArg("main_sparkle_speed_ms")) {
    const long value = klcWebServerArgToLongStrict("main_sparkle_speed_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_SPEED_MS, value)) {
      snprintf(message, message_len, "Ein-Funkel-Geschwindigkeit außerhalb 0..60000 ms");
      return false;
    }
    scene.main_sparkle_speed_ms = (uint16_t)value;
  }
  if (g_server.hasArg("end_sparkle_speed_ms")) {
    const long value = klcWebServerArgToLongStrict("end_sparkle_speed_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_SPEED_MS, value)) {
      snprintf(message, message_len, "Aus-Funkel-Geschwindigkeit außerhalb 0..60000 ms");
      return false;
    }
    scene.end_sparkle_speed_ms = (uint16_t)value;
  }
  if (g_server.hasArg("flow_ui_v2")) {
    scene.sparkle_speed_ms = scene.main_sparkle_speed_ms;
  }

  if (g_server.hasArg("sparkle_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("sparkle_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Funkel-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.sparkle_fill_percent = (uint8_t)value;
  }
  if (g_server.hasArg("start_sparkle_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("start_sparkle_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Start-Funkel-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.start_sparkle_fill_percent = (uint8_t)value;
  }
  if (g_server.hasArg("main_sparkle_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("main_sparkle_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Ein-Funkel-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.main_sparkle_fill_percent = (uint8_t)value;
  }
  if (g_server.hasArg("end_sparkle_fill_percent")) {
    const long value = klcWebServerArgToLongStrict("end_sparkle_fill_percent");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_FILL_PERCENT, value)) {
      snprintf(message, message_len, "Aus-Funkel-Füllgrad außerhalb 0..100 Prozent");
      return false;
    }
    scene.end_sparkle_fill_percent = (uint8_t)value;
  }
  if (g_server.hasArg("flow_ui_v2")) {
    scene.sparkle_fill_percent = scene.main_sparkle_fill_percent;
  }

  if (g_server.hasArg("sparkle_lifetime_ms")) {
    const long value = klcWebServerArgToLongStrict("sparkle_lifetime_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_LIFETIME_MS, value)) {
      snprintf(message, message_len, "Funkel-Lebenszeit außerhalb 0..60000 ms");
      return false;
    }
    scene.sparkle_lifetime_ms = (uint16_t)value;
  }
  if (g_server.hasArg("start_sparkle_lifetime_ms")) {
    const long value = klcWebServerArgToLongStrict("start_sparkle_lifetime_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_LIFETIME_MS, value)) {
      snprintf(message, message_len, "Start-Funkel-Lebenszeit außerhalb 0..60000 ms");
      return false;
    }
    scene.start_sparkle_lifetime_ms = (uint16_t)value;
  }
  if (g_server.hasArg("main_sparkle_lifetime_ms")) {
    const long value = klcWebServerArgToLongStrict("main_sparkle_lifetime_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_LIFETIME_MS, value)) {
      snprintf(message, message_len, "Ein-Funkel-Lebenszeit außerhalb 0..60000 ms");
      return false;
    }
    scene.main_sparkle_lifetime_ms = (uint16_t)value;
  }
  if (g_server.hasArg("end_sparkle_lifetime_ms")) {
    const long value = klcWebServerArgToLongStrict("end_sparkle_lifetime_ms");
    if (!klcWebServerEffectParamInRange(KLC_EFFECT_PARAM_SPARKLE_LIFETIME_MS, value)) {
      snprintf(message, message_len, "Aus-Funkel-Lebenszeit außerhalb 0..60000 ms");
      return false;
    }
    scene.end_sparkle_lifetime_ms = (uint16_t)value;
  }
  if (g_server.hasArg("flow_ui_v2")) {
    scene.sparkle_lifetime_ms = scene.main_sparkle_lifetime_ms;
  }

  if (!klcWebServerReadByteArg(
        "fireworks_speed", scene.fireworks_speed, scene.fireworks_speed, message, message_len) ||
      !klcWebServerReadByteArg(
        "fireworks_intensity", scene.fireworks_intensity, scene.fireworks_intensity, message, message_len) ||
      !klcWebServerReadByteArg(
        "tetrix_speed", scene.tetrix_speed, scene.tetrix_speed, message, message_len) ||
      !klcWebServerReadByteArg(
        "tetrix_width", scene.tetrix_width, scene.tetrix_width, message, message_len)) {
    return false;
  }

  if (!klcConfigValidateSceneDetailed(scene, (uint8_t)id_long,
                                      message, message_len)) {
    return false;
  }

  uint32_t expected_revision = klcSceneStoreRevision((uint8_t)id_long);
  if (g_server.hasArg("revision")) {
    const long revision_arg = klcWebServerArgToLongStrict("revision");
    if (revision_arg < 0) {
      snprintf(message, message_len, "Szenenrevision ungueltig");
      return false;
    }
    expected_revision = (uint32_t)revision_arg;
  }
  if (expected_revision != klcSceneStoreRevision((uint8_t)id_long)) {
    snprintf(message, message_len, "Revisionskonflikt: Browser %lu, aktuell %lu",
             (unsigned long)expected_revision,
             (unsigned long)klcSceneStoreRevision((uint8_t)id_long));
    return false;
  }

  // Der HTTP-Callback reiht nur den vollstaendig validierten Kandidaten ein.
  // Runtime-Uebernahme und anschliessender Flashauftrag laufen im Hauptloop.
  if (!klcSceneStoreEnqueue((uint8_t)id_long, scene, expected_revision,
                            result.operation_id, result.revision,
                            message, message_len)) {
    return false;
  }
  result.applied_revision=expected_revision;
  result.queued = result.operation_id != 0U;
  result.saved = !result.queued;
  result.applied = !result.queued;
  snprintf(message, message_len, result.queued
             ? "Szene %ld zur Runtime-Uebernahme eingereiht; Auftrag %llu laeuft"
             : "Szene %ld unveraendert und bereits dauerhaft gespeichert",
             id_long, (unsigned long long)result.operation_id);
  return true;
}

static void klcWebServerSendSceneConfigResult(
  int status, bool ok, const KlcSceneConfigCommitResult& result,
  const char* message)
{
  g_server.sendHeader("Cache-Control", "no-store");
  KlcWebResponseStreamGuard stream(
    status, "application/json; charset=utf-8", false);
  if (!stream.started()) return;
  (void)klcWebServerWriteResponseText(ok ? "{\"ok\":true,\"message\":"
                                                : "{\"ok\":false,\"error\":");
  klcWebServerSendJsonStringValue(message != nullptr ? message : "");
  klcWebServerSendContentFmt(
    ",\"saved\":%s,\"queued\":%s,\"applied\":%s,\"restart_required\":%s,\"operation_type\":\"scene\",\"operation_id\":\"%llu\",\"revision\":%lu,\"operation_revision\":%lu,\"accepted_revision\":%lu,\"applied_revision\":%lu,\"boot_generation\":%lu}",
    result.saved ? "true" : "false",
    result.queued ? "true" : "false",
    result.applied ? "true" : "false",
    result.restart_required ? "true" : "false",
    (unsigned long long)result.operation_id,
    (unsigned long)result.revision,(unsigned long)result.revision,
    (unsigned long)result.revision,
    (unsigned long)result.applied_revision,
    (unsigned long)klcSceneStoreBootGeneration());
  (void)stream.finish();
}

void klcWebServerHandleSceneConfigApiPost()
{
  char message[128];
  KlcSceneConfigCommitResult result = {};
  const bool ok = klcWebServerApplySceneConfigRequest(
    message, sizeof(message), result);
  klcWebServerSendSceneConfigResult(
    ok ? (result.queued ? 202 : 200) : (result.queued || result.saved ? 500 : 409),
    ok, result, message);
}

void klcWebServerHandleSceneConfigFormPost()
{
  char message[128];
  KlcSceneConfigCommitResult result = {};
  const bool ok=klcWebServerApplySceneConfigRequest(
    message, sizeof(message), result);
  long scene_arg=g_server.hasArg("id")?klcWebServerArgToLongStrict("id"):1;
  if(scene_arg<1||scene_arg>KLC_SCENE_MAX_PUBLIC)scene_arg=1;
  if(!ok){g_server.send(400,"text/plain; charset=utf-8",message);return;}
  char location[88];snprintf(location,sizeof(location),result.queued?
    "/flow/save-status?scene=%ld&operation_id=%llu":"/flows?scene=%ld",
    scene_arg,(unsigned long long)result.operation_id);
  g_server.sendHeader("Location", location);
  g_server.send(303, "text/plain; charset=utf-8", message);
}



void klcWebServerHandleSceneFlowConfigFormPost()
{
  char message[128];
  KlcSceneConfigCommitResult result = {};
  const bool ok = klcWebServerApplySceneConfigRequest(
    message, sizeof(message), result);
  long scene_arg = 1;
  if (g_server.hasArg("id")) {
    scene_arg = klcWebServerArgToLongStrict("id");
  }
  if (scene_arg < 0 || !klcScenePublicIsConfigurable((uint32_t)scene_arg)) {
    scene_arg = 1;
  }
  if (!ok) {
    g_server.send(result.saved || result.queued ? 500 : 400,
                  "text/plain; charset=utf-8", message);
    return;
  }
  char location[88];
  snprintf(location, sizeof(location), result.queued
    ? "/flow/save-status?scene=%ld&operation_id=%llu" : "/flow/edit?scene=%ld&saved=1",
    scene_arg,(unsigned long long)result.operation_id);
  g_server.sendHeader("Location", location);
  g_server.send(303, "text/plain; charset=utf-8", message);
}

void klcWebServerHandleSceneStorageStatusGet()
{
  uint64_t operation_id = 0U;
  if (g_server.hasArg("operation_id")) {
    const String text=g_server.arg("operation_id");char* end=nullptr;
    operation_id=strtoull(text.c_str(),&end,10);
    if (operation_id==0U||end==nullptr||*end!='\0') {
      g_server.send(400, "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"ungueltige Vorgangs-ID\"}");
      return;
    }
  }
  KlcSceneStoreStatus status{};
  if (!klcSceneStoreGetStatus(operation_id, status)) {
    g_server.send(404, "application/json; charset=utf-8",
                  "{\"ok\":false,\"error\":\"Vorgang nicht mehr im Statusfenster\"}");
    return;
  }
  g_server.sendHeader("Cache-Control", "no-store");
  KlcWebResponseStreamGuard stream(200,
    "application/json; charset=utf-8", false);
  if (!stream.started()) return;
  klcWebServerSendContentFmt(
    "{\"ok\":true,\"initialized\":%s,\"busy\":%s,\"ram_only\":%s,"
    "\"scene_id\":%u,\"operation_type\":\"scene\",\"operation_id\":\"%llu\",\"state\":",
    status.initialized ? "true" : "false", status.busy ? "true" : "false",
    status.ram_only ? "true" : "false", status.scene_id,
    (unsigned long long)status.operation_id);
  klcWebServerSendJsonStringValue(klcSceneStoreStateText(status.state));
  klcWebServerSendContentFmt(
    ",\"slot\":%u,\"queue_depth\":%u,\"revision\":%lu,"
    "\"operation_revision\":%lu,\"accepted_revision\":%lu,\"applied_revision\":%lu,\"current_scene_revision\":%lu,"
    "\"persisted_revision\":%lu,\"storage_sequence\":%lu,\"bytes_written\":%lu,"
    "\"bytes_read\":%lu,\"started_ms\":%lu,\"completed_ms\":%lu,"
    "\"max_step_us\":%lu,\"free_heap_start\":%lu,"
    "\"free_heap_end\":%lu,\"littlefs_total_bytes\":%lu,"
    "\"littlefs_used_bytes\":%lu,\"recovery_count\":%lu,\"runtime_applied\":%s,\"rollback_failed\":%s,\"failure_phase\":",
    status.slot, status.queue_depth, (unsigned long)status.operation_revision,
    (unsigned long)status.operation_revision,
    (unsigned long)status.accepted_revision,
    (unsigned long)status.applied_revision,
    (unsigned long)status.current_scene_revision,
    (unsigned long)status.persisted_revision,
    (unsigned long)status.storage_sequence,
    (unsigned long)status.bytes_written, (unsigned long)status.bytes_read,
    (unsigned long)status.started_ms, (unsigned long)status.completed_ms,
    (unsigned long)status.max_step_us,
    (unsigned long)status.free_heap_start,
    (unsigned long)status.free_heap_end,
    (unsigned long)status.littlefs_total_bytes,
    (unsigned long)status.littlefs_used_bytes,
    (unsigned long)status.recovery_count,status.runtime_applied?"true":"false",
    status.rollback_failed?"true":"false");
  klcWebServerSendJsonStringValue(klcSceneStoreFailurePhaseText(status.failure_phase));
  (void)klcWebServerWriteResponseText(",\"boot_generation\":");
  klcWebServerSendContentFmt("%lu,\"error\":",(unsigned long)klcSceneStoreBootGeneration());
  klcWebServerSendJsonStringValue(status.error);
  (void)klcWebServerWriteResponseText("}");
  (void)stream.finish();
}

void klcWebServerHandleScenePersistenceStatusGet()
{
  uint8_t first=1U,last=KLC_SCENE_MAX_PUBLIC;
  if(g_server.hasArg("scene_id")){
    const long id=klcWebServerArgToLongStrict("scene_id");
    if(id<1||id>KLC_SCENE_MAX_PUBLIC){g_server.send(400,
      "application/json; charset=utf-8","{\"ok\":false,\"error\":\"ungueltige Szenen-ID\"}");return;}
    first=last=(uint8_t)id;
  }
  g_server.sendHeader("Cache-Control","no-store");
  KlcWebResponseStreamGuard stream(200,"application/json; charset=utf-8",false);
  if(!stream.started())return;
  (void)klcWebServerWriteResponseText("{\"ok\":true,\"scenes\":[");
  for(uint8_t id=first;id<=last;++id){
    KlcScenePersistenceStatus s{};if(!klcSceneStoreGetSceneStatus(id,s))continue;
    if(id!=first)(void)klcWebServerWriteResponseText(",");
    klcWebServerSendContentFmt(
      "{\"scene_id\":%u,\"ram_revision\":%lu,\"accepted_revision\":%lu,\"applied_revision\":%lu,\"persisted_revision\":%lu,"
      "\"dirty\":%s,\"ram_only\":%s,\"active_operation_id\":\"%llu\","
      "\"queued\":%s,\"last_success_operation_id\":\"%llu\","
      "\"last_success_timestamp\":%lu,\"last_error_code\":%lu,\"last_error_text\":",
      s.scene_id,(unsigned long)s.ram_revision,(unsigned long)s.accepted_revision,
      (unsigned long)s.applied_revision,(unsigned long)s.persisted_revision,
      s.dirty?"true":"false",s.ram_only?"true":"false",
      (unsigned long long)s.active_operation_id,s.queued?"true":"false",
      (unsigned long long)s.last_success_operation_id,(unsigned long)s.last_success_timestamp,
      (unsigned long)s.last_error_code);
    klcWebServerSendJsonStringValue(s.last_error_text);
    (void)klcWebServerWriteResponseText(",\"last_recovery_reason\":");
    klcWebServerSendJsonStringValue(s.last_recovery_reason);
    klcWebServerSendContentFmt(
      ",\"active_slot\":%u,\"active_sequence\":%lu,\"slot_a_valid\":%s,\"slot_b_valid\":%s}",
      s.active_slot,(unsigned long)s.active_sequence,s.slot_a_valid?"true":"false",
      s.slot_b_valid?"true":"false");
  }
  (void)klcWebServerWriteResponseText("]}");(void)stream.finish();
}

void klcWebServerHandleSceneReplacementStatusGet()
{
  uint64_t operation_id=0U;
  if(g_server.hasArg("operation_id")){
    const String text=g_server.arg("operation_id");char* end=nullptr;
    operation_id=strtoull(text.c_str(),&end,10);
    if(operation_id==0U||end==nullptr||*end!='\0'){g_server.send(400,"application/json; charset=utf-8",
      "{\"ok\":false,\"error\":\"ungueltige Vorgangs-ID\"}");return;}
  }
  KlcSceneReplacementStatus s{};
  if(!klcSceneStoreGetReplacementStatus(operation_id,s)){g_server.send(404,
    "application/json; charset=utf-8","{\"ok\":false,\"error\":\"Importvorgang nicht gefunden\"}");return;}
  const char* state=s.state==1U?"prepared":s.state==2U?"migrating":
    s.state==3U?"complete":s.state==4U?"cancelled":s.state==5U?"failed":
    s.state==6U?"await_activation":"unknown";
  char json[420];snprintf(json,sizeof(json),
    "{\"ok\":true,\"replacement_operation_id\":\"%llu\",\"operation_id\":\"%llu\",\"config_generation\":%lu,\"state\":\"%s\",\"progress_scene\":%u,\"last_confirmed_scene\":%u,\"failed_scene\":%u,\"error_code\":%lu,\"main_config_activated\":%s,\"kls_generation_committed\":%s,\"fallback_source\":\"%s\",\"completed\":%s}",
    (unsigned long long)s.operation_id,(unsigned long long)s.operation_id,
    (unsigned long)s.config_generation,state,s.last_confirmed_scene,
    s.last_confirmed_scene,s.failed_scene,(unsigned long)s.error_code,
    s.main_config_activated?"true":"false",s.kls_generation_committed?"true":"false",
    s.fallback_source,s.completed?"true":"false");
  g_server.sendHeader("Cache-Control","no-store");
  g_server.send(200,"application/json; charset=utf-8",json);
}
