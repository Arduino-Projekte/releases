#include "klc_web_server_internal.h"
#include "klc_scene_store.h"
#include <stdlib.h>

static void klcWebServerRedirectToFlows(uint8_t scene_id)
{
  if (scene_id < 1U || scene_id > KLC_SCENE_MAX_PUBLIC) {
    scene_id = 1U;
  }
  char location[40];
  snprintf(location, sizeof(location), "/flows?scene=%u", (unsigned)scene_id);
  g_server.sendHeader("Location", location);
  g_server.send(303, "text/plain; charset=utf-8", "Ablauf geändert");
}

static void klcWebServerRedirectToFlowStatus(uint8_t scene_id,
                                             uint64_t operation_id)
{
  if(operation_id==0U){klcWebServerRedirectToFlows(scene_id);return;}
  char location[80];snprintf(location,sizeof(location),
    "/flow/save-status?scene=%u&operation_id=%llu",scene_id,
    (unsigned long long)operation_id);
  g_server.sendHeader("Location",location);
  g_server.send(303,"text/plain; charset=utf-8","Speicherstatus");
}

static bool klcWebServerSaveFlowToolsScene(const KlcSceneConfig& scene,
                                           uint8_t scene_id,
                                           char* message,
                                           size_t message_len,
                                           const char* ok_text,
                                           uint64_t& operation_id)
{
  if (scene_id < 1U || scene_id > KLC_SCENE_MAX_PUBLIC ||
      !klcConfigValidateSceneDetailed(scene, scene_id,
                                      message, message_len)) {
    return false;
  }
  const uint32_t expected_revision=klcSceneStoreRevision(scene_id);
  operation_id=0U;uint32_t revision = 0U;
  if (!klcSceneStoreEnqueue(scene_id, scene,
                            expected_revision,
                            operation_id, revision, message, message_len)) {
    return false;
  }
  snprintf(message, message_len, operation_id != 0U
             ? "%s; Speicherauftrag %llu laeuft" : "%s; unveraendert",
             ok_text, (unsigned long long)operation_id);
  return true;
}

void klcWebServerHandleFlowCopyPost()
{
  char message[128];
  const long source_long = g_server.hasArg("source") ? klcWebServerArgToLongStrict("source") : -1;
  const long target_long = g_server.hasArg("target") ? klcWebServerArgToLongStrict("target") : -1;
  if (source_long < 1 || source_long > KLC_SCENE_MAX_PUBLIC || target_long < 1 || target_long > KLC_SCENE_MAX_PUBLIC) {
    klcWebServerSendLocalizedFormError(400, "Quelle oder Ziel ungültig", "/flows");
    return;
  }
  if (source_long == target_long) {
    klcWebServerRedirectToFlows((uint8_t)source_long);
    return;
  }

  KlcSceneConfig scene = g_config.scenes[(uint8_t)source_long];
  scene.id = (uint8_t)target_long;
  uint64_t operation_id=0U;
  if (!klcWebServerSaveFlowToolsScene(scene, (uint8_t)target_long,
                                      message, sizeof(message),
                                      "Ablauf kopiert",operation_id)) {
    klcWebServerSendLocalizedFormError(500, message, "/flows");
    return;
  }
  klcWebServerRedirectToFlowStatus((uint8_t)target_long,operation_id);
}

void klcWebServerHandleFlowResetPost()
{
  char message[128];
  const long id_long = g_server.hasArg("id") ? klcWebServerArgToLongStrict("id") : -1;
  if (id_long < 1 || id_long > KLC_SCENE_MAX_PUBLIC) {
    klcWebServerSendLocalizedFormError(400, "Ablauf-ID ungültig", "/flows");
    return;
  }

  KlcSceneConfig scene{};
  klcConfigLoadSceneDefault(scene, (uint8_t)id_long);
  scene.id = (uint8_t)id_long;
  uint64_t operation_id=0U;
  if (!klcWebServerSaveFlowToolsScene(scene, (uint8_t)id_long,
                                      message, sizeof(message),
                                      "Ablauf zurückgesetzt",operation_id)) {
    klcWebServerSendLocalizedFormError(500, message, "/flows");
    return;
  }
  klcWebServerRedirectToFlowStatus((uint8_t)id_long,operation_id);
}

void klcWebServerHandleFlowNamePost()
{
  char message[128];
  const long id_long = g_server.hasArg("id") ? klcWebServerArgToLongStrict("id") : -1;
  if (id_long < 1 || id_long > KLC_SCENE_MAX_PUBLIC) {
    klcWebServerSendLocalizedFormError(400, "Ablauf-ID ungültig", "/flows");
    return;
  }
  if (!g_server.hasArg("name")) {
    klcWebServerSendLocalizedFormError(400, "Name fehlt", "/flows");
    return;
  }
  String name = g_server.arg("name");
  name.trim();
  if (name.length() == 0) {
    klcWebServerSendLocalizedFormError(400, "Name darf nicht leer sein", "/flows");
    return;
  }

  KlcSceneConfig scene = g_config.scenes[(uint8_t)id_long];
  name.toCharArray(scene.name, KLC_MAX_NAME_LEN);
  uint64_t operation_id=0U;
  if (!klcWebServerSaveFlowToolsScene(scene, (uint8_t)id_long,
                                      message, sizeof(message),
                                      "Ablaufname gespeichert",operation_id)) {
    klcWebServerSendLocalizedFormError(500, message, "/flows");
    return;
  }
  klcWebServerRedirectToFlowStatus((uint8_t)id_long,operation_id);
}

void klcWebServerHandleFlowSaveStatusGet()
{
  const long scene=g_server.hasArg("scene")?klcWebServerArgToLongStrict("scene"):-1;
  const String operation_text=g_server.hasArg("operation_id")?
    g_server.arg("operation_id"):String("");
  char* end=nullptr;const uint64_t operation=strtoull(operation_text.c_str(),&end,10);
  if(scene<1||scene>KLC_SCENE_MAX_PUBLIC||operation==0U||end==nullptr||*end!='\0'){
    g_server.send(400,"text/plain; charset=utf-8","Speicherstatus ungueltig");return;
  }
  KlcSceneStoreStatus status{};
  if(!klcSceneStoreGetStatus(operation,status)){
    g_server.send(404,"text/plain; charset=utf-8",
      "Vorgang nicht mehr im Statusfenster. Szene neu laden.");return;
  }
  char html[1200];snprintf(html,sizeof(html),
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>%s<title>Speicherstatus</title></head><body><main><h1>Ablauf speichern</h1><p>Szene %ld, Vorgang %llu</p><p>Status: %s</p><p>%s</p><p><a href='/flows?scene=%ld'>Zurueck zum Ablauf</a></p></main></body></html>",
    status.busy?"<meta http-equiv='refresh' content='1'>":"",scene,
    (unsigned long long)operation,
    klcSceneStoreStateText(status.state),
    status.state==KLC_SCENE_STORE_DONE?"Dauerhaft gespeichert.":
    status.state==KLC_SCENE_STORE_FAILED?"Nur im RAM aktiv; Speichern erneut versuchen.":
    status.state==KLC_SCENE_STORE_SUPERSEDED?"Durch neueren Auftrag ersetzt; Szene neu laden.":
    "Speicherung laeuft; diese Seite aktualisiert sich automatisch.",scene);
  g_server.sendHeader("Cache-Control","no-store");
  g_server.send(200,"text/html; charset=utf-8",html);
}
