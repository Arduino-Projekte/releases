#include "klc_reset.h"

#include "klc_auth.h"
#include "klc_diag.h"
#include "klc_output_pending.h"
#include "klc_status_led.h"
#include "klc_storage.h"
#include "klc_scene_store.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static void klcResetSetMessage(char* message, size_t message_len,
                               const char* text)
{
  if (message == nullptr || message_len == 0) return;
  snprintf(message, message_len, "%s", text != nullptr ? text : "");
  message[message_len - 1] = '\0';
}

const char* klcResetKindText(KlcResetKind kind)
{
  return kind == KLC_RESET_FACTORY_DEFAULTS
    ? "Werkseinstellungen"
    : "Netzwerkeinstellungen";
}

static void klcResetPrepareCandidate(KlcResetKind kind,
                                     const KlcDeviceConfig& current,
                                     KlcDeviceConfig& candidate)
{
  if (kind == KLC_RESET_FACTORY_DEFAULTS) {
    klcConfigLoadDefaults(candidate);
    // Werksreset ist der dokumentierte Recovery-Weg bei vergessenem
    // Adminpasswort: Standardzugaenge (Benutzer 123456, Admin "Admin",
    // Benutzer-Login deaktiviert) werden hier fest initialisiert.
    (void)klcAuthEnsureConfigInitialized(candidate);
    return;
  }

  candidate = current;
  // Ein Netzwerkreset verwirft keine Ausgangswerte: auch eine bereits
  // gespeicherte, noch nicht aktivierte Ausgangs-Strukturaenderung bleibt in
  // der Datei erhalten. Nach jedem erfolgreichen Reset folgt unmittelbar ein
  // kontrollierter Neustart, der diesen gespeicherten Stand aktiviert.
  klcOutputPendingApplyToConfig(candidate);
  klcConfigLoadNetworkDefaults(candidate);
}

bool klcResetCommitDefaults(KlcResetKind kind,
                            KlcDeviceConfig& runtime_cfg,
                            char* message,
                            size_t message_len)
{
  if (kind != KLC_RESET_NETWORK_DEFAULTS &&
      kind != KLC_RESET_FACTORY_DEFAULTS) {
    klcResetSetMessage(message, message_len, "Unbekannte Reset-Art");
    klcDiagSetError(KLC_DIAG_ERROR_RESET_CANDIDATE_INVALID);
    klcDiagLogError(KLC_DIAG_ERROR_RESET_CANDIDATE_INVALID, "Reset abgelehnt: unbekannte Reset-Art");
    klcStatusLedShowResetResult(false);
    return false;
  }

  KlcConfigWorkspaceLease candidate_workspace("Reset vorbereiten");
  if (!candidate_workspace) {
    klcResetSetMessage(
      message, message_len,
      "Konfigurations-Arbeitsbereich ist belegt; Reset ohne Aenderung abgebrochen");
    klcStatusLedShowResetResult(false);
    return false;
  }
  KlcDeviceConfig& candidate = *candidate_workspace;
  klcResetPrepareCandidate(kind, runtime_cfg, candidate);

  char validation_error[192];
  if (!klcConfigValidateDetailed(candidate, validation_error,
                                 sizeof(validation_error))) {
    char detail[240];
    snprintf(detail, sizeof(detail), "%s ungueltig: %s",
             klcResetKindText(kind), validation_error);
    klcResetSetMessage(message, message_len, detail);
    klcDiagSetError(KLC_DIAG_ERROR_RESET_CANDIDATE_INVALID);
    klcDiagLogError(KLC_DIAG_ERROR_RESET_CANDIDATE_INVALID, detail);
    klcStatusLedShowResetResult(false);
    return false;
  }

  // Netzwerk- und Werksreset sind explizite Recovery-Aktionen. Sie duerfen
  // deshalb auch eine vorhandene, nicht lesbare Konfigurationsdatei nach
  // vollstaendiger Validierung atomar ersetzen.
  if (kind == KLC_RESET_FACTORY_DEFAULTS &&
      !klcSceneStorePrepareConfigReplacement(candidate,message,message_len)) {
    klcResetSetMessage(message, message_len,
      "Werkseinstellungen abgebrochen: Szenenspeicher konnte nicht vorbereitet werden");
    klcStatusLedShowResetResult(false);
    return false;
  }
  if (!klcStorageSaveConfigAsPending(candidate, true)) {
    if (kind == KLC_RESET_FACTORY_DEFAULTS) {
      klcSceneStoreCancelConfigReplacement();
    }
    char detail[240];
    snprintf(detail, sizeof(detail),
             "%s konnten nicht sicher gespeichert werden: %s",
             klcResetKindText(kind), klcStorageGetLastError());
    klcResetSetMessage(message, message_len, detail);
    klcDiagSetError(KLC_DIAG_ERROR_RESET_STORAGE_FAILED);
    klcDiagLogError(KLC_DIAG_ERROR_RESET_STORAGE_FAILED, detail);
    klcStatusLedShowResetResult(false);
    return false;
  }
  if(kind==KLC_RESET_FACTORY_DEFAULTS&&!klcSceneStoreConfirmConfigReplacement()){
    klcResetSetMessage(message,message_len,
      "Werkseinstellungen gespeichert, aber Szenen-Transaktion fehlt; Neustart abgebrochen");
    klcStatusLedShowResetResult(false);return false;
  }

  // Der Resetpfad plant unmittelbar den kontrollierten Neustart. Bis dahin
  // bleibt die aktive Laufzeitkonfiguration einschliesslich LED-Topologie
  // unangetastet; die atomar gespeicherte Datei ist ein Pending-Kandidat.
  klcOutputPendingCaptureSaved(candidate);

  // Nach einem Werksreset gelten wieder die Standardzugaenge: alle noch
  // laufenden Sitzungen sofort beenden und die Standardpasswort-Warnflags
  // aktualisieren (der anschliessende Neustart wuerde RAM-Sitzungen ohnehin
  // invalidieren, aber der Zustand soll schon vor dem Reboot konsistent sein).
  if (kind == KLC_RESET_FACTORY_DEFAULTS) {
    klcAuthSessionInvalidateAll();
  }
  klcAuthRefreshDefaultPasswordFlags(candidate);

  const uint16_t success_code = kind == KLC_RESET_FACTORY_DEFAULTS ? KLC_DIAG_EVENT_FACTORY_RESET_STORED : KLC_DIAG_EVENT_NETWORK_RESET_STORED;
  char detail[176];
  if(kind==KLC_RESET_FACTORY_DEFAULTS)snprintf(detail,sizeof(detail),
    "%s sicher gespeichert; Neustart zur Aktivierung erforderlich; Szenen-Vorgang %llu",
    klcResetKindText(kind),
    (unsigned long long)klcSceneStoreReplacementOperationId());
  else snprintf(detail,sizeof(detail),
    "%s sicher gespeichert; Neustart zur Aktivierung erforderlich",
    klcResetKindText(kind));
  klcDiagLogInfo(success_code, detail);
  klcStatusLedShowResetResult(true);
  klcResetSetMessage(message, message_len, detail);
  return true;
}
