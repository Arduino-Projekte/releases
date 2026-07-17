#include "klc_version.h"
#include "klc_auth.h"
#include "klc_chain.h"
#include "klc_config.h"
#include "klc_storage.h"
#include "klc_scene_store.h"
#include "klc_led_engine.h"
#include "klc_led_backend.h"
#include "klc_led_pool.h"
#include "klc_pixel_limits.h"
#include "klc_led_power.h"
#include "klc_output_pending.h"
#include "klc_scenes.h"
#include "klc_ethernet_w5500.h"
#include "klc_wifi.h"
#include "klc_net_control.h"
#include "klc_web_server.h"
#include "klc_knx_baos.h"
#include "klc_knx_objects.h"
#include "klc_ota.h"
#include "klc_language.h"
#include "klc_diag.h"
#include "klc_pins.h"
#include "klc_status_led.h"
#include "klc_profile_selfcheck.h"
#include "klc_reset.h"

#include <string.h>
#include <pico/bootrom.h>

// Globale Gerätekonfiguration.
// Die Konfiguration wird später aus dem Flash geladen.
// Nicht static, damit WebUI und Diagnose den aktuellen Stand anzeigen koennen.
KlcDeviceConfig g_config;
static bool g_config_recovery_locked = false;

// Lokaler Recovery-Weg ohne Netzwerk und ohne laufende Firmware:
// Service-Taster bereits beim Einschalten drei Sekunden halten. Die Abfrage
// liegt bewusst vor Storage, LED, Ethernet, WLAN und KNX. Nur wenn der Taster
// beim Boot erkannt wurde, wartet der Start auf die eindeutige Haltegeste.
static void klcBootCheckServiceButtonForBootsel()
{
  pinMode(KLC_PIN_SERVICE_BUTTON, INPUT_PULLUP);
  if (digitalRead(KLC_PIN_SERVICE_BUTTON) != LOW) return;

  const uint32_t started_ms = millis();
  Serial.println("[SERVICE] Boot-Taster erkannt: 3 s halten fuer BOOTSEL/UF2.");

  while (digitalRead(KLC_PIN_SERVICE_BUTTON) == LOW) {
    const uint32_t held_ms = (uint32_t)(millis() - started_ms);
    // Erste Stufe: Gruen sehr schnell. Zweite Stufe: Gruen dauerhaft und
    // Rot sehr schnell. Diese Folge ist nur beim Booten BOOTSEL zugeordnet.
    klcStatusLedSetServiceWindow(held_ms < 1500UL ? 1U : 3U);
    klcStatusLedTick(false, false);

    if (held_ms >= 3000UL) {
      klcStatusLedSetServiceWindow(4U); // beide LEDs aus: Uebergabe an USB-ROM
      klcStatusLedTick(false, false);
      Serial.println("[SERVICE] BOOTSEL/UF2-Modus wird jetzt gestartet.");
      Serial.flush();
      delay(50);
      reset_usb_boot(0, 0);
      while (true) delay(1000); // nur Sicherheitsnetz, reset_usb_boot kehrt nicht zurueck
    }
    delay(10);
  }

  klcStatusLedSetServiceWindow(0U);
  klcStatusLedTick(false, false);
  Serial.println("[SERVICE] Boot-Taster vor 3 s losgelassen, normaler Start.");
}

// Merkt die erste erfolgreiche WLAN-Client-Verbindung genau einmal dauerhaft.
// Das Ereignis wird erst nach dem bereits atomaren/verifizierenden Config-
// Commit bestaetigt. Bei vollem/defektem LittleFS folgt fruehestens nach einer
// Minute ein neuer Versuch; eine vorhandene unlesbare Altdatei wird nie ersetzt.
static void klcPersistFirstWifiConnectionIfNeeded()
{
  if (!klcWifiIsSupported() || g_config.wifi.sta_ever_connected ||
      g_config_recovery_locked ||
      klcWebServerSetupWifiOwnsFirstConnectionPersistence() ||
      !klcWifiHasUnpersistedSuccessfulConnection()) {
    if (g_config.wifi.sta_ever_connected) {
      klcWifiAcknowledgeSuccessfulConnectionPersisted();
    }
    return;
  }

  static uint32_t retry_at_ms = 0;
  const uint32_t now = millis();
  if (retry_at_ms != 0 && (int32_t)(now - retry_at_ms) < 0) return;

  KlcConfigWorkspaceLease candidate_workspace(
    "WLAN-Erstverbindungsstatus speichern", g_config);
  if (!candidate_workspace) {
    retry_at_ms = now + 60000UL;
    Serial.println(
      "[WLAN] Erstverbindungsstatus nicht gespeichert: Konfigurations-Arbeitsbereich belegt.");
    return;
  }
  KlcDeviceConfig& candidate = *candidate_workspace;
  candidate.wifi.sta_ever_connected = true;
  candidate.wifi.ap_enabled = false;
  if (klcStorageSaveConfig(candidate)) {
    g_config = candidate;
    klcWifiAcknowledgeSuccessfulConnectionPersisted();
    retry_at_ms = 0;
    Serial.println(
      "[WLAN] Erste erfolgreiche Client-Verbindung und AP-Sollzustand AUS in einem Commit gespeichert.");
    if (klcWifiStopApPreservingSta()) {
      Serial.println(
        "[WLAN][AP-POLICY] Service-AP ist nach dem ersten STA-Erfolg tatsaechlich aus.");
    } else {
      Serial.println(
        "[WLAN][AP-POLICY] AP-Aus ist gespeichert; Hardwareabschaltung wird vom Soll-/Ist-Waechter wiederholt.");
    }
  } else {
    retry_at_ms = now + 60000UL;
    Serial.print("[WLAN] Erstverbindungsstatus konnte nicht gespeichert werden: ");
    Serial.println(klcStorageGetLastError());
  }
}

// Zentrale Service-AP-Policy fuer einen normalen STA-Erfolg ausserhalb des
// Setup-Tests. Der AP wird nur abgeschaltet, wenn AP-Aus zuvor atomar
// gespeichert und in g_config uebernommen wurde. Bei Recovery oder
// Speicherfehler bleibt der Zugang erhalten.
static void klcApplyServiceApPolicyAfterStaSuccess()
{
  static uint32_t retry_at_ms = 0;
  if (!klcWifiIsSupported() || !klcWifiStaIsConnected() ||
      !g_config.wifi.ap_enabled || g_config_recovery_locked ||
      klcWebServerSetupWifiOwnsFirstConnectionPersistence()) {
    return;
  }

  const uint32_t now = millis();
  if (retry_at_ms != 0 && (int32_t)(now - retry_at_ms) < 0) return;

  KlcConfigWorkspaceLease candidate(
    "Service-AP nach STA-Erfolg dauerhaft deaktivieren", g_config);
  if (!candidate) {
    retry_at_ms = now + 60000UL;
    Serial.println(
      "[WLAN][AP-POLICY] AP bleibt aktiv: Konfigurations-Arbeitsbereich ist belegt.");
    return;
  }
  candidate->wifi.sta_ever_connected = true;
  candidate->wifi.ap_enabled = false;
  if (!klcStorageSaveConfig(*candidate)) {
    retry_at_ms = now + 60000UL;
    Serial.print(
      "[WLAN][AP-POLICY] AP bleibt als Recoveryzugang aktiv; AP-Aus konnte nicht gespeichert werden: ");
    Serial.println(klcStorageGetLastError());
    return;
  }

  g_config = *candidate;
  klcWifiAcknowledgeSuccessfulConnectionPersisted();
  retry_at_ms = 0;
  Serial.println(
    "[WLAN][AP-POLICY] AP-Konfiguration atomar als AUS gespeichert; Hardwareabschaltung angefordert.");
  if (klcWifiStopApPreservingSta()) {
    Serial.println(
      "[WLAN][AP-POLICY] Service-AP ist tatsaechlich hardwareseitig aus.");
  } else {
    Serial.println(
      "[WLAN][AP-POLICY] FEHLER: AP-Aus ist gespeichert, Hardwareabschaltung noch nicht bestaetigt; Soll-/Ist-Waechter wiederholt den Vorgang.");
  }
}

// Synchronisiert nach jeder Verbindungskante "nicht verbunden -> verbunden"
// DP 53 genau einmal. In aktiven Statusmodi wird zusaetzlich der normale
// Status-Snapshot erneuert; im passiven Modus bleibt die Ausnahme auf DP 53 begrenzt.
static void klcPublishStartupStatusAfterBaosConnected()
{
  static bool was_connected = false;
  const bool connected = g_config.knx.enabled && klcKnxBaosIsConnected();

  if (!connected) {
    was_connected = false;
    return;
  }
  if (was_connected) {
    return;
  }

  was_connected = true;
  Serial.println("[KNX] BAOS neu verbunden: aktueller Startup-/Runtime-Status wird veroeffentlicht.");
  klcKnxObjectsSendStartupStatus();
}

// Service-Taster im laufenden Betrieb (LOW = gedrueckt, INPUT_PULLUP):
//   unter 2 realen s = keine Aktion; Gruen blinkt sehr schnell
//   2 bis unter 5 s  = Netzwerkreset; Gruen leuchtet dauerhaft
//   5 bis unter 10 s = Werksreset; Gruen leuchtet, Rot blinkt sehr schnell
//   ab 10 s          = Abbruch/Klemmschutz; beide LEDs sind aus
// Die Aktion wird ausschliesslich beim LOSLASSEN ausgefuehrt. Dadurch kann
// eine versehentlich gewaehlte Resetstufe durch Weiterhalten bis 10 s sicher
// verworfen werden. Bei zu fruehem Loslassen oder Abbruch kehren die LEDs
// sofort zur normalen Systemanzeige zurueck.
static void klcServiceRebootAfterSuccessfulReset(const char* reason)
{
  Serial.print("[SERVICE] Sicherer Reset abgeschlossen: ");
  Serial.println(reason != nullptr ? reason : "Reset");
  Serial.println("[SERVICE] Gewaehltes LED-Muster bleibt bis zum kontrollierten Neustart aktiv.");

  // Die zuvor gewaehlte Resetstufe bleibt noch kurz sichtbar. Die Schleife
  // ist fest begrenzt und bedient nur die Status-LED; danach wird unmittelbar
  // neu gestartet. Netzwerkreset bleibt gruen, Werksreset bleibt gruen mit
  // schnell blinkender roter LED.
  const uint32_t started_ms = millis();
  while ((uint32_t)(millis() - started_ms) < 900UL) {
    klcStatusLedTick(false, false);
    delay(10);
  }

  Serial.flush();
  delay(50);
  rp2040.reboot();
}

static uint8_t klcServiceButtonZoneForHeldMs(uint32_t held_ms)
{
  if (held_ms >= 10000UL) return 4U;
  if (held_ms >= 5000UL) return 3U;
  if (held_ms >= 2000UL) return 2U;
  return 1U;
}

static bool klcServiceButtonFirmwareActivity()
{
  return klcOtaIsUpdateRunning() || klcOtaIsDownloadRunning() ||
         klcOtaIsRebootRequested();
}

static void klcServiceButtonReturnToNormalDisplay()
{
  klcStatusLedSetServiceWindow(0);
  klcStatusLedTick(klcServiceButtonFirmwareActivity(), false);
}

static void klcServiceButtonTick()
{
  static bool pressed = false;
  static uint32_t press_start_ms = 0;
  static uint32_t last_logged_s = 0;

  const bool now_pressed = digitalRead(KLC_PIN_SERVICE_BUTTON) == LOW;
  const uint32_t now = millis();

  // Waehrend Firmwaredownload, -schreiben oder geplantem OTA-Neustart ist
  // die Reset-Auswahl gesperrt. Andernfalls koennte ein unsichtbarer
  // Service-Zustand unter der hoeher priorisierten Update-Anzeige trotzdem
  // beim Loslassen einen Reset ausloesen.
  if (klcServiceButtonFirmwareActivity()) {
    if (pressed) {
      pressed = false;
      klcStatusLedSetServiceWindow(0);
      Serial.println("[SERVICE] Reset-Auswahl wegen laufender Firmwareaktivitaet verworfen.");
    }
    return;
  }

  if (now_pressed) {
    if (!pressed) {
      pressed = true;
      press_start_ms = now;
      last_logged_s = 0;
      Serial.println("[SERVICE] Taster gedrueckt: <2 s keine Aktion, 2..<5 s Netzwerkreset, 5..<10 s Werksreset, ab 10 s Abbruch.");
    } else {
      const uint32_t held_s = (now - press_start_ms) / 1000UL;
      if (held_s > 0UL && held_s != last_logged_s) {
        last_logged_s = held_s;
        Serial.print("[SERVICE] gehalten: ");
        Serial.print(held_s);
        Serial.println(" s");
      }
    }

    const uint32_t held_ms = now - press_start_ms;
    klcStatusLedSetServiceWindow(klcServiceButtonZoneForHeldMs(held_ms));
    return;
  }

  if (!pressed) {
    return;
  }
  pressed = false;

  const uint32_t held_ms = now - press_start_ms;
  const uint8_t released_zone = klcServiceButtonZoneForHeldMs(held_ms);
  if (released_zone == 1U) {
    klcServiceButtonReturnToNormalDisplay();
    if (held_ms >= 50UL) {
      Serial.println("[SERVICE] Taster unter 2 s gehalten, keine Aktion.");
    }
    return;
  }

  if (released_zone == 2U) {
    // Die Auswahl vor dem blockierenden Commit nochmals eindeutig setzen.
    // So bleibt Gruen waehrend des Speicherns sichtbar und bis zum Neustart an.
    klcStatusLedSetServiceWindow(2U);
    klcStatusLedTick(false, false);
    Serial.println("[SERVICE] Sicherer Netzwerk-Reset wird vorbereitet.");
    char reset_message[256];
    if (!klcResetCommitDefaults(KLC_RESET_NETWORK_DEFAULTS, g_config,
                                reset_message, sizeof(reset_message))) {
      klcServiceButtonReturnToNormalDisplay();
      Serial.print("[SERVICE] Netzwerk-Reset abgebrochen, kein Neustart: ");
      Serial.println(reset_message);
      return;
    }
    klcServiceRebootAfterSuccessfulReset(reset_message);
    return;
  }

  if (released_zone == 3U) {
    // Gruen bleibt an; Rot startet vor dem blockierenden Commit nochmals mit
    // einem vollstaendigen schnellen Blinkzyklus und blinkt bis zum Neustart.
    klcStatusLedSetServiceWindow(3U);
    klcStatusLedTick(false, false);
    Serial.println("[SERVICE] Sicherer Werksreset wird vorbereitet.");
    char reset_message[256];
    if (!klcResetCommitDefaults(KLC_RESET_FACTORY_DEFAULTS, g_config,
                                reset_message, sizeof(reset_message))) {
      klcServiceButtonReturnToNormalDisplay();
      Serial.print("[SERVICE] Werksreset abgebrochen, kein Neustart: ");
      Serial.println(reset_message);
      return;
    }
    klcServiceRebootAfterSuccessfulReset(reset_message);
    return;
  }

  klcServiceButtonReturnToNormalDisplay();
  Serial.println("[SERVICE] 10 s oder laenger gehalten: Reset-Auswahl verworfen, keine Aktion.");
}

static bool klcBootValidateLedCandidate(KlcDeviceConfig& cfg,
                                        KlcOutputActivationStep& failed_step,
                                        char* error,
                                        size_t error_len)
{
  failed_step = KLC_OUTPUT_ACTIVATION_NONE;
  if (error == nullptr || error_len == 0U) return false;
  error[0] = '\0';

  klcChainNormalizeConfig(cfg);

  const KlcPixelValidationResult pixel_validation = klcPixelValidateConfig(cfg);
  if (pixel_validation.range == KLC_PIXEL_RANGE_REJECTED) {
    failed_step = KLC_OUTPUT_ACTIVATION_VALIDATE_PIXELS;
    klcPixelFormatValidationMessage(pixel_validation, error, error_len);
    return false;
  }

  if (!klcChainValidate(cfg, error, error_len)) {
    failed_step = KLC_OUTPUT_ACTIVATION_VALIDATE_CHAIN;
    return false;
  }

  if (!klcConfigValidateDetailed(cfg, error, error_len)) {
    failed_step = KLC_OUTPUT_ACTIVATION_VALIDATE_CONFIG;
    return false;
  }

  KlcLedPoolPlan plan;
  if (!klcLedPoolComputePlan(cfg, plan)) {
    failed_step = KLC_OUTPUT_ACTIVATION_MEMORY_PLAN;
    snprintf(error, error_len, "%s", "Ueberlauf im LED-Speicherplan");
    return false;
  }
  if (plan.total_bytes > klcPixelLimitProfile().dynamic_heap_budget_bytes) {
    failed_step = KLC_OUTPUT_ACTIVATION_MEMORY_PLAN;
    snprintf(error, error_len,
             "LED-Speicherplan %lu Byte ueberschreitet Budget %lu Byte",
             (unsigned long)plan.total_bytes,
             (unsigned long)klcPixelLimitProfile().dynamic_heap_budget_bytes);
    return false;
  }
  return true;
}

static KlcOutputActivationStep klcBootBackendFailureStep(const char* step)
{
  if (step == nullptr) return KLC_OUTPUT_ACTIVATION_BACKEND_INIT;
  if (strcmp(step, "pixel_limits") == 0) return KLC_OUTPUT_ACTIVATION_VALIDATE_PIXELS;
  if (strcmp(step, "heap_preflight") == 0 ||
      strcmp(step, "heap_reserve") == 0) return KLC_OUTPUT_ACTIVATION_HEAP_RESERVE;
  if (strcmp(step, "pool_reserve") == 0) return KLC_OUTPUT_ACTIVATION_POOL_RESERVE;
  if (strcmp(step, "pool_carve") == 0) return KLC_OUTPUT_ACTIVATION_POOL_CARVE;
  if (strcmp(step, "pio") == 0) return KLC_OUTPUT_ACTIVATION_PIO_INIT;
  if (strcmp(step, "dma") == 0) return KLC_OUTPUT_ACTIVATION_DMA_INIT;
  return KLC_OUTPUT_ACTIVATION_BACKEND_INIT;
}

static void klcBootMakeSafeRecoveryConfig(KlcDeviceConfig& cfg)
{
  klcConfigLoadDefaults(cfg);
  const uint8_t count = cfg.output_count > KLC_MAX_OUTPUTS
    ? KLC_MAX_OUTPUTS : cfg.output_count;
  for (uint8_t i = 0; i < count; ++i) {
    cfg.outputs[i].enabled = false;
    cfg.outputs[i].follows_output = 0U;
    cfg.outputs[i].chain_reverse = false;
  }
  klcChainNormalizeConfig(cfg);
  (void)klcAuthEnsureConfigInitialized(cfg);
}

// Klartext fuer den Chip-Neustartgrund. WDT steht auf diesem Core auch fuer
// Panics/Hard-Faults, weil deren Handler ueber den Watchdog neu starten.
static const char* klcBootResetReasonText(RP2040::resetReason_t reason)
{
  switch (reason) {
    case RP2040::PWRON_RESET: return "Einschalten (Power-on)";
    case RP2040::RUN_PIN_RESET: return "RUN-Pin/Hardware-Reset";
    case RP2040::SOFT_RESET: return "kontrollierter Software-Neustart";
    case RP2040::WDT_RESET: return "Watchdog (Absturz/Panic)";
    case RP2040::DEBUG_RESET: return "Debug-Port";
    case RP2040::GLITCH_RESET: return "Spannungs-Glitch";
    case RP2040::BROWNOUT_RESET: return "Brownout (Versorgungseinbruch)";
    default: return "unbekannt";
  }
}

void setup()
{
  const uint32_t setup_started_us = micros();
  klcStatusLedBegin();

  Serial.begin(115200);
  delay(300);

  klcBootCheckServiceButtonForBootsel();

  Serial.println();
  Serial.println("========================================");
  Serial.println(KLC_PROJECT_FULL_NAME);
  Serial.print("Version: ");
  Serial.println(KLC_VERSION);
  Serial.print("Build: ");
  Serial.print(KLC_BUILD_TIMESTAMP);
  Serial.print(" / Label: ");
  Serial.println(KLC_BUILD_LABEL);
  Serial.print("Boardprofil: ");
  Serial.print(KLC_BOARD_NAME);
  Serial.print(" / Build-Target: ");
  Serial.print(KLC_BUILD_TARGET);
  Serial.print(" / Release-Produkt: ");
  Serial.print(KLC_RELEASE_PRODUCT);
  Serial.print(" / Releasepfad: ");
  Serial.print(KLC_RELEASE_VARIANT_PATH);
  Serial.print(" / Pinprofil: ");
  Serial.println(KLC_BOARD_PINMAP_ID);
  Serial.print("Netzwerkprofil: ");
  Serial.print(KLC_NETWORK_BACKEND_NAME);
  Serial.print(" / WLAN-Hardware: ");
  Serial.print(KLC_BOARD_HAS_WIFI_HARDWARE ? "ja" : "nein");
  Serial.print(" / WLAN-Firmware aktiv: ");
  Serial.println(KLC_WIFI_BACKEND_ACTIVE ? "ja" : "nein");
  const RP2040::resetReason_t boot_reset_reason = rp2040.getResetReason();
  Serial.print("Neustartgrund: ");
  Serial.println(klcBootResetReasonText(boot_reset_reason));
  Serial.println("Arduino-Startstruktur");
  Serial.println("========================================");

  klcConfigLoadDefaults(g_config);

  klcDiagBegin();
  klcProfileSelfcheckBegin();
  klcDiagLogInfo(KLC_DIAG_EVENT_SETUP_STARTED, "Setup gestartet");
  // Ein Watchdog-/Glitch-/Brownout-Start deutet auf Absturz oder
  // Versorgungsproblem hin und muss auch ohne serielle Konsole in der
  // Diagnose sichtbar sein.
  if (boot_reset_reason == RP2040::WDT_RESET ||
      boot_reset_reason == RP2040::GLITCH_RESET ||
      boot_reset_reason == RP2040::BROWNOUT_RESET) {
    char reset_note[96];
    snprintf(reset_note, sizeof(reset_note),
             "Unerwarteter Neustart: %s",
             klcBootResetReasonText(boot_reset_reason));
    klcDiagLogWarning(KLC_DIAG_WARNING_UNEXPECTED_RESET, reset_note);
  } else {
    char reset_note[96];
    snprintf(reset_note, sizeof(reset_note), "Neustartgrund: %s",
             klcBootResetReasonText(boot_reset_reason));
    klcDiagLogInfo(KLC_DIAG_EVENT_SETUP_STARTED, reset_note);
  }
  klcProfileSelfcheckRun();
  const uint32_t storage_mount_started_us = micros();
  klcStorageBegin();
  klcDiagRecordBlockingOperation("Boot: LittleFS mounten",
    micros() - storage_mount_started_us, 100000UL);

  const bool lkg_existed_at_start = klcStorageLastKnownGoodExists();
  uint32_t main_size = 0U, main_hash = 0U;
  bool main_identity_valid =
    klcStorageGetConfigIdentity(main_size, main_hash);
  (void)klcOutputActivationBeginBoot(main_size, main_hash,
                                     main_identity_valid,
                                     lkg_existed_at_start);

  bool active_from_main = false;
  bool active_from_previous = false;
  bool active_from_lkg = false;
  bool recovery_mode = false;
  bool tracked_candidate = false;
  const uint32_t config_load_started_us = micros();
  const bool config_loaded = klcStorageLoadConfigWithRecovery(g_config);
  klcDiagRecordBlockingOperation("Boot: Konfiguration und Recovery laden",
    micros() - config_load_started_us, 250000UL);
  const KlcStorageRecoveryStatus& config_recovery =
    klcStorageGetRecoveryStatus();
  const bool main_loaded = config_loaded &&
    config_recovery.source == KLC_STORAGE_CONFIG_SOURCE_ACTIVE;
  active_from_previous = config_loaded &&
    config_recovery.source == KLC_STORAGE_CONFIG_SOURCE_PREVIOUS;

  if (main_loaded) {
    // Migration/Auth-Ergaenzung kann die Datei beim Laden atomar erneuert
    // haben; fuer Versuch/Fehlerjournal gilt deshalb die Identitaet danach.
    main_identity_valid = klcStorageGetConfigIdentity(main_size, main_hash);
    klcOutputPendingCaptureSaved(g_config);
    active_from_main = true;

    const KlcOutputActivationStatus& activation =
      klcOutputActivationGetStatus();
    if (activation.pending_present) {
      if (!main_identity_valid ||
          !klcOutputActivationShouldAttempt(main_size, main_hash)) {
        Serial.println("[LED-ACT] Unveraenderter fehlgeschlagener Pending-Kandidat wird nicht erneut gestartet.");
        active_from_main = false;
        if (klcStorageLoadLastKnownGood(g_config)) {
          active_from_lkg = true;
        } else {
          recovery_mode = true;
        }
      } else if (klcOutputActivationMarkAttemptStarted(main_size, main_hash)) {
        tracked_candidate = true;
      } else {
        // Ohne persistierbaren In-Progress-Status duerfen wir einen neuen
        // Pending-Stand nicht als sicher aktivierbar behandeln.
        active_from_main = false;
        if (klcStorageLoadLastKnownGood(g_config)) active_from_lkg = true;
        else recovery_mode = true;
      }
    } else if (!lkg_existed_at_start) {
      // Erstaktivierung ebenfalls gegen Stromausfall/Reset absichern.
      tracked_candidate = klcOutputActivationMarkAttemptStarted(
        main_size, main_hash);
      if (!tracked_candidate) {
        active_from_main = false;
        recovery_mode = true;
      }
    }
  } else if (active_from_previous) {
    // Die Rettungskopie ist semantisch vollstaendig gueltig, bleibt aber nur
    // Laufzeitquelle. Die defekte/fehlende Hauptdatei und /klc_config_prev.json
    // werden bis zu einer bewussten Adminaktion nicht veraendert.
    klcOutputPendingCaptureSaved(g_config);
    g_config_recovery_locked = true;
    klcStorageSetConfigWriteLocked(true);
    Serial.println("[STORAGE][RECOVERY] Controller startet mit validierter vorheriger Konfiguration; dauerhafte Uebernahme ausstehend.");

    char load_error[160];
    snprintf(load_error, sizeof(load_error),
             "Aktive Konfiguration unbrauchbar: %.104s",
             config_recovery.active_error);
    (void)klcOutputActivationMarkFailure(
      main_size, main_hash, KLC_OUTPUT_ACTIVATION_LOAD_PENDING, 0U,
      load_error);
  } else if (config_recovery.recovery_active) {
    // Beide Dateien wurden bereits ueber denselben Import-/Validierungspfad
    // geprueft. Sichere Defaults bleiben nur im RAM; kein automatisches
    // Ueberschreiben und damit auch keine Neustartschleife.
    recovery_mode = true;
    g_config_recovery_locked = true;
    klcStorageSetConfigWriteLocked(true);
  } else {
    // Echte Ersteinrichtung: Es existieren keinerlei Konfigurationsartefakte.
    // Nur in diesem Fall duerfen Defaults wie bisher initial gespeichert werden.
    Serial.println("[STORAGE] Keine gespeicherte Konfiguration gefunden, nutze Defaultwerte.");
    klcDiagSetWarning(KLC_DIAG_WARNING_CONFIG_DEFAULTED);
    (void)klcAuthEnsureConfigInitialized(g_config);
    if (klcStorageSaveConfig(g_config)) {
      main_identity_valid = klcStorageGetConfigIdentity(main_size, main_hash);
      klcOutputPendingCaptureSaved(g_config);
      active_from_main = true;
      tracked_candidate = klcOutputActivationMarkAttemptStarted(
        main_size, main_hash);
      klcDiagLogWarning(KLC_DIAG_WARNING_CONFIG_DEFAULTED,
                        "Keine gespeicherte Konfiguration, Defaults gespeichert");
    } else {
      recovery_mode = true;
      Serial.print("[STORAGE] Defaults konnten nicht gespeichert werden: ");
      Serial.println(klcStorageGetLastError());
    }
  }

  // Nach dem validierten Legacy-/Backup-Ladevorgang werden Szenen aus dem
  // eigenen A/B-Repository ueberlagert. Beim ersten Start migriert Begin die
  // vorhandenen JSON-Szenen und markiert das Repository erst nach kompletter
  // Ruecklese-/CRC-Pruefung als massgeblich.
  if (!recovery_mode && !klcSceneStoreBegin(
        g_config, active_from_previous || active_from_lkg)) {
    Serial.println("[SCENE-STORE] Repository nicht bereit; Szenenaenderungen bleiben gesperrt.");
    klcDiagSetWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED);
    klcDiagLogWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED,
      "KLS1-Start oder Migration fehlgeschlagen; JSON-Szenen bleiben aktiv");
  }

  char candidate_error[192];
  KlcOutputActivationStep candidate_step = KLC_OUTPUT_ACTIVATION_NONE;
  if (!recovery_mode &&
      !klcBootValidateLedCandidate(g_config, candidate_step,
                                   candidate_error,
                                   sizeof(candidate_error))) {
    Serial.print("[LED-ACT] Kandidat vor Hardwarestart abgelehnt: ");
    Serial.println(candidate_error);
    if (active_from_main) {
      (void)klcOutputActivationMarkFailure(
        main_size, main_hash, candidate_step, 0U, candidate_error);
      klcDiagSetError(KLC_DIAG_ERROR_LED_ACTIVATION_FAILED);
      klcDiagLogError(KLC_DIAG_ERROR_LED_ACTIVATION_FAILED, candidate_error);
      active_from_main = false;
      if (klcStorageLoadLastKnownGood(g_config) &&
          klcBootValidateLedCandidate(g_config, candidate_step,
                                      candidate_error,
                                      sizeof(candidate_error))) {
        active_from_lkg = true;
      } else {
        active_from_lkg = false;
        recovery_mode = true;
      }
    } else {
      active_from_previous = false;
      active_from_lkg = false;
      recovery_mode = true;
    }
  }

  if (recovery_mode) {
    klcBootMakeSafeRecoveryConfig(g_config);
  }

  (void)klcAuthEnsureConfigInitialized(g_config);
  klcAuthRefreshDefaultPasswordFlags(g_config);

  // Service-Taster wird jetzt im laufenden Betrieb ausgewertet, siehe
  // klcServiceButtonTick(). Ein beim Boot klemmender Taster loest damit
  // keinen Werksreset mehr aus.
  pinMode(KLC_PIN_SERVICE_BUTTON, INPUT_PULLUP);

  // LEDs werden hier bewusst noch NICHT eingeschaltet. klcLedEngineBegin
  // initialisiert die Strips nur dunkel und reserviert dabei einmalig den
  // zusammenhaengenden LED-Masterpool (aktive Puffer + fester Testpool);
  // grosse temporaere JSON-/Parser-Puffer des Konfigurationsladens sind zu
  // diesem Zeitpunkt bereits wieder freigegeben. Erst spaeter, wenn Netzwerk,
  // Webserver und OTA laufen (das Geraet also erreichbar ist), startet die
  // Szenenlogik ueber klcScenesBegin den Power-On-Zustand und schaltet die
  // LEDs ein. Sonst liefen nach einem (Online-)Flash bereits LED-Animationen,
  // bevor die Weboberflaeche ueberhaupt erreichbar war.
  bool led_engine_ok = klcLedEngineBegin(g_config);
  if (!led_engine_ok && !recovery_mode) {
    const bool failed_candidate_was_lkg = active_from_lkg;
    const KlcLedBackendRuntime* backend = klcLedBackendGetRuntime();
    char activation_error[160];
    snprintf(activation_error, sizeof(activation_error), "%s",
             backend != nullptr && backend->activation_error[0] != '\0'
               ? backend->activation_error
               : "LED-Backendinitialisierung fehlgeschlagen");
    const KlcOutputActivationStep failure_step = klcBootBackendFailureStep(
      backend != nullptr ? backend->activation_step : nullptr);
    const uint8_t failure_output = backend != nullptr
      ? backend->activation_failed_output_id : 0U;

    if (active_from_main) {
      (void)klcOutputActivationMarkFailure(
        main_size, main_hash, failure_step, failure_output,
        activation_error);
      klcDiagSetError(KLC_DIAG_ERROR_LED_ACTIVATION_FAILED);
      klcDiagLogError(KLC_DIAG_ERROR_LED_ACTIVATION_FAILED,
                      activation_error);
    }
    klcLedEngineRollbackActivation();
    klcLedPoolRollbackBootReservation();

    // Genau ein Recoveryversuch mit der persistenten LKG. Der Pending-Stand
    // bleibt unveraendert und als fehlgeschlagen sichtbar.
    active_from_main = false;
    active_from_previous = false;
    active_from_lkg = false;
    if (!failed_candidate_was_lkg &&
        klcStorageLoadLastKnownGood(g_config) &&
        klcBootValidateLedCandidate(g_config, candidate_step,
                                    candidate_error,
                                    sizeof(candidate_error))) {
      active_from_lkg = true;
      led_engine_ok = klcLedEngineBegin(g_config);
    }

    if (!active_from_lkg || !led_engine_ok) {
      if (active_from_lkg) {
        klcLedEngineRollbackActivation();
        klcLedPoolRollbackBootReservation();
      }
      recovery_mode = true;
      active_from_lkg = false;
      klcBootMakeSafeRecoveryConfig(g_config);
      led_engine_ok = klcLedEngineBegin(g_config);
      (void)klcOutputActivationMarkRecovery(
        KLC_OUTPUT_ACTIVATION_LKG_LOAD,
        "Pending und Last-Known-Good konnten nicht aktiviert werden; sichere LED-Ausgabe ist deaktiviert");
      klcDiagSetError(KLC_DIAG_ERROR_LED_RECOVERY_MODE);
    }
  }

  if (recovery_mode) {
    if (!led_engine_ok) {
      klcLedEngineRollbackActivation();
      klcLedPoolRollbackBootReservation();
    } else {
      klcLedPoolCommitBootReservation();
    }
    (void)klcOutputActivationMarkRecovery(
      KLC_OUTPUT_ACTIVATION_LKG_LOAD,
      "Sicherer Recoverymodus aktiv; LED-Ausgaenge bleiben deaktiviert");
    klcDiagSetError(KLC_DIAG_ERROR_LED_RECOVERY_MODE);
    klcDiagLogError(KLC_DIAG_ERROR_LED_RECOVERY_MODE,
                    "Sicherer Recoverymodus aktiv; LED-Ausgaenge bleiben deaktiviert");
  } else if (active_from_previous && led_engine_ok) {
    // Die validierte Rettungskopie darf den Laufzeit-/LED-Start vollstaendig
    // versorgen, wird aber weder als Hauptdatei noch als persistente LKG
    // geschrieben. Erst der explizite Recovery-Button fuehrt einen Commit aus.
    klcLedPoolCommitBootReservation();
    klcOutputLastKnownGoodCapture(g_config);
    klcOutputPendingResetFromActive(g_config);
    Serial.println("[STORAGE][RECOVERY] Rettungskopie ist vollstaendig aktiv; dauerhafte Uebernahme bleibt ausstehend.");
  } else if (active_from_lkg && led_engine_ok) {
    klcLedPoolCommitBootReservation();
    klcOutputLastKnownGoodCapture(g_config);
    (void)klcOutputActivationMarkFallback(false);
    klcDiagSetWarning(KLC_DIAG_WARNING_LED_LKG_FALLBACK);
    klcDiagLogWarning(KLC_DIAG_WARNING_LED_LKG_FALLBACK,
                      "Pending fehlgeschlagen oder uebersprungen; Last-Known-Good ist aktiv");
    Serial.println("[LED-ACT] Pending fehlgeschlagen/uebersprungen; Last-Known-Good ist aktiv.");
  } else if (active_from_main && led_engine_ok) {
    // Erst jetzt ist die Kandidatenaktivierung vollstaendig. Vor diesem Punkt
    // wird weder LKG aktualisiert noch Pending geloescht.
    const bool needs_lkg_commit =
      tracked_candidate || !klcStorageLastKnownGoodExists() ||
      klcOutputActivationGetStatus().pending_present;
    bool lkg_commit_ok = true;
    if (needs_lkg_commit) {
      lkg_commit_ok = klcStoragePromoteCurrentToLastKnownGood();
      if (lkg_commit_ok) {
        (void)klcOutputActivationMarkSuccess(main_size, main_hash);
      } else {
        (void)klcOutputActivationMarkFailure(
          main_size, main_hash, KLC_OUTPUT_ACTIVATION_LKG_PERSIST, 0U,
          klcStorageGetLastError());
      }
    } else {
      klcOutputActivationMarkKnownGoodBootSuccess(true);
    }
    klcLedPoolCommitBootReservation();
    if (lkg_commit_ok) {
      klcOutputLastKnownGoodCapture(g_config);
      klcOutputPendingResetFromActive(g_config);
    }
  }

  // Erst nach vollstaendig erfolgreicher Pool-/PIO-/DMA-/Backend-Aktivierung
  // wird die vorbereitete KLS-Generation gemeinsam mit dieser Configquelle
  // autoritativ. Ein spaeter Fallback bindet seine Szenen zuvor neu.
  const char* activated_config_source=recovery_mode?"recovery":
    (active_from_lkg?"lkg":(active_from_previous?"previous":"main"));
  if(led_engine_ok&&!klcSceneStoreFinalizeActivatedConfig(
       g_config,activated_config_source)){
    klcDiagSetWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED);
    klcDiagLogWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED,
      "Aktive Hauptkonfiguration konnte nicht gemeinsam mit KLS finalisiert werden");
  }

  // Ethernet und WLAN teilen sich denselben lwIP-Stack. Beide Schnittstellen
  // koennen per Konfiguration, KNX (DP 20/21) oder WebUI dauerhaft
  // abgeschaltet sein. Rueckweg ohne Netzwerk: Service-Taster 2 bis unter 5 Sekunden halten und loslassen.
  const uint32_t network_start_started_us = micros();
  if (g_config.wifi.eth_enabled) {
    klcEthernetBegin(g_config.network, g_config.hostname);
  } else {
    Serial.println("[ETH] Ethernet ist per Konfiguration/KNX deaktiviert.");
    // W5500 (falls bestueckt) im Reset halten, damit er keinen Strom zieht.
    klcEthernetEnd();
  }
  klcWifiBegin(g_config.wifi, g_config.hostname);
  klcNetControlBegin(klcEthernetIsStarted(), klcWifiIsSupported() && g_config.wifi.wlan_enabled);

  klcWebServerSetConfigRecoveryLocked(g_config_recovery_locked);

  if (klcNetControlNetworkWanted()) {
    klcWebServerBegin();
  } else {
    Serial.println("[WEB] Alle Netzwerkschnittstellen sind deaktiviert, Webserver bleibt aus.");
    Serial.println("[WEB] Die freie Loopzeit kommt den LED-Animationen zugute.");
  }
  klcOtaBegin();
  klcOtaSetUpdateSettings(g_config.update.beta_program, g_config.update.auto_check, g_config.update.auto_install, g_config.update.auto_check_interval_minutes, g_config.update.auto_install_delay_minutes);
  klcDiagRecordBlockingOperation("Boot: Netzwerk und Webserver starten",
    micros() - network_start_started_us, 300000UL);

  // Jetzt ist das Geraet erreichbar (Webserver/OTA aktiv): erst jetzt die LEDs
  // ueber die Szenenlogik in ihren Startzustand bringen. Bewusst vor dem KNX-
  // Start, damit der anschliessende KNX-Startstatus den korrekten Szenenzustand
  // meldet.
  if (!klcScenesBegin(g_config)) {
    Serial.print("[CHAIN] Szenenstart abgelehnt: ");
    Serial.println(klcChainGetLastRuntimeError());
  }

  klcKnxBaosBegin();
  klcKnxObjectsBegin();
  // Reconnect-Kantenerkennung im Loop synchronisiert DP 53 bzw. den Vollstatus genau einmal.

  // Die Paketinstallation gehoert nicht in den Bootpfad. Der bestehende
  // Sprach-Tick fuehrt Stage, Validierung, Aktivierung und Commit nach dem
  // Start in getrennten Phasen aus; bis dahin bleibt Englisch der Fallback.
  const char* selected_language = klcLanguageNormalize(g_config.ui.language);
  if (klcLanguageNeedsPack(selected_language) &&
      !klcLanguageActiveIsValid(selected_language)) {
    klcLanguageRequestDownload(selected_language);
  }

  klcDiagLogInfo(KLC_DIAG_EVENT_SETUP_COMPLETED, "Setup abgeschlossen");
  klcStatusLedBootComplete();
  klcDiagRecordBlockingOperation("Boot: gesamtes Setup",
    micros() - setup_started_us, 1500000UL);
  Serial.println("Setup abgeschlossen.");
}

void loop()
{
  klcEthernetTick();
  klcWifiTick();
  klcPersistFirstWifiConnectionIfNeeded();
  klcApplyServiceApPolicyAfterStaSuccess();

  // Webserver- und OTA-Tick nur, solange Netzwerk gewollt ist. Sind Ethernet
  // und WLAN per KNX/WebUI abgeschaltet, entfallen diese Aufrufe komplett und
  // die Loopzeit kommt den LED-Animationen zugute. Ausnahme: ein bereits
  // angeforderter Neustart/BOOTSEL-Wechsel wird immer noch ausgefuehrt.
  if (klcNetControlNetworkWanted() || klcOtaIsRebootRequested() || klcOtaIsBootselRequested()) {
    klcWebServerTick();
    klcOtaTick();
  }
  klcStorageUiStateTick();
  klcStatusLedTick(klcOtaIsUpdateRunning() || klcOtaIsDownloadRunning() || klcOtaIsRebootRequested(), klcWebServerIsAdminSessionActive());

  // Heap-Gesamtwerte, Tiefstwert und fester Verlaufs-Ringpuffer (5-s-Takt,
  // ca. 10 Minuten) fuer Diagnoseseite und Support-Snapshot.
  klcLedPoolHeapTick();

  // Waehrend eines Firmware-Uploads lassen wir alle nicht notwendigen
  // Dummy-/Laufzeitmodule ruhen. Das entlastet Netzwerk, Heap und Loopzeit,
  // bis der Upload abgeschlossen oder abgebrochen ist.
  if (klcOtaIsUpdateRunning()) {
    return;
  }

  // Genau ein kleiner Dateisystemschritt pro Loop. Dadurch laufen Webserver,
  // KNX, Szenenberechnung und LED-Ausgabe zwischen allen Commitphasen weiter.
  klcSceneStoreTick();

  klcServiceButtonTick();
  klcNetControlTick();
  klcKnxBaosTick();
  klcPublishStartupStatusAfterBaosConnected();
  klcKnxObjectsTick();
  klcScenesTick();

  // Manuelle LED-Tests mit eigener Laufzeitlogik, z. B. Kanten-Blinktest
  // fuer Anfang/Ende eines Ausgangs. Szenen ueberschreiben aktive Tests nicht.
  klcLedEngineTickManualTests();
  klcWebServerOutputTestTick();

  klcLedEngineShow();

  // Nichtkritische Hintergrundarbeit zuletzt. Eingebettete Sprachpakete
  // funktionieren offline; Onlinepakete starten erst mit vorhandenem Uplink.
  if (!klcDiagBackgroundIsActive()) {
    klcLanguageTick();
  }
  klcDiagTick();
}
