#include "klc_web_ui_internal.h"
#include "klc_scene_store.h"

const char* klcWebUiSceneFlowHtml(uint8_t selected_scene_id)
{
  size_t used = 0;
  if (selected_scene_id < 1U || selected_scene_id > KLC_SCENE_MAX_PUBLIC) {
    selected_scene_id = 1U;
  }
  const uint8_t prev_scene = selected_scene_id <= 1U ? KLC_SCENE_MAX_PUBLIC : (uint8_t)(selected_scene_id - 1U);
  const uint8_t next_scene = selected_scene_id >= KLC_SCENE_MAX_PUBLIC ? 1U : (uint8_t)(selected_scene_id + 1U);
  const uint8_t selected_public_scene = selected_scene_id;
  const uint8_t prev_public_scene = prev_scene;
  const uint8_t next_public_scene = next_scene;
  if (!klcWebUiEnsureBuffer(g_flow_html, KLC_WEBUI_FLOW_HTML_LEN)) {
    return KLC_WEBUI_OOM_HTML;
  }

  klcWebUiAppendText(g_flow_html, KLC_WEBUI_FLOW_HTML_LEN, used,
    "<!doctype html><html lang='de'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>KLC Abläufe</title><link rel='stylesheet' href='/ui.css'>"
    "<style>.flow-editor-frame{width:100%%;min-height:900px;border:0;background:transparent;overflow:hidden}.flow-shell .grid{margin-top:12px}.flow-tools{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-top:14px}.flow-tool{border:1px solid var(--border);border-radius:14px;padding:12px;background:rgba(255,255,255,.025)}.flow-tool .row{margin-top:8px}.flow-load-note{margin:10px 0 0}</style>"
    "</head><body><main class='wrap'>");
  klcWebUiAppendNav(g_flow_html, KLC_WEBUI_FLOW_HTML_LEN, used, "flows");

  klcWebUiAppendText(g_flow_html, KLC_WEBUI_FLOW_HTML_LEN, used,
    "<section class='card flow-shell'><h1>Abläufe</h1>"
    "<form class='preview-head' method='GET' action='/flows'><label title='Wählt den Ablauf, der unten im Editor bearbeitet wird. Der Zusatz - Pool markiert Abläufe im KNX-Szenen-Pool.'>Ablauf auswählen<a class='help-dot' href='/help?q=flows.scene_pool' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select id='flowSceneSelect' class='preview-select' name='scene'><option value='%u'>Ablauf %u wird geladen ...</option></select></label>"
    "<button class='btn' type='submit'>anzeigen</button>"
    "<a id='flowPrev' class='btn secondary' href='/flows?scene=%u'>◀</a>"
    "<a id='flowNext' class='btn secondary' href='/flows?scene=%u'>▶</a>"
    "<a id='flowPreview' class='btn secondary' href='/preview?scene=%u&t=0'>Vorschau</a></form>"
    "<div class='flow-tools'>"
    "<form id='flowNameForm' class='flow-tool' method='POST' action='/flow/name'><input type='hidden' name='csrf' value='%s'><input id='flowToolNameId' type='hidden' name='id' value='%u'><label title='Benennt den oben ausgewählten Ablauf um, ohne den Editor zu öffnen.'>Name schnell ändern<br><input id='flowToolName' name='name' maxlength='31' value=''></label><div class='row'><button class='btn small' type='submit'>Name speichern</button></div></form>"
    "<form id='flowCopyForm' class='flow-tool' method='POST' action='/flow/copy' onsubmit='return confirm(window.klcT?window.klcT(\"Ziel-Ablauf wird überschrieben. Fortfahren?\"):\"Ziel-Ablauf wird überschrieben. Fortfahren?\");'><input type='hidden' name='csrf' value='%s'><input id='flowToolCopySource' type='hidden' name='source' value='%u'><label title='Kopiert alle Einstellungen des ausgewählten Ablaufs auf den Ziel-Speicherplatz. Der Ziel-Ablauf wird dabei überschrieben.'>Ausgewählten Ablauf kopieren nach<br><select id='flowCopyTarget' name='target'></select></label><div class='row'><button class='btn small' type='submit'>kopieren / duplizieren</button></div></form>"
    "<form id='flowResetForm' class='flow-tool' method='POST' action='/flow/reset' onsubmit='return confirm(window.klcT?window.klcT(\"Diesen Ablauf auf Werkstandard zurücksetzen?\"):\"Diesen Ablauf auf Werkstandard zurücksetzen?\");'><input type='hidden' name='csrf' value='%s'><input id='flowToolResetId' type='hidden' name='id' value='%u'><p class='muted'>Setzt nur den ausgewählten Ablauf zurück.</p><div class='row'><button class='btn small secondary' type='submit'>Ablauf zurücksetzen</button></div></form>"
    "</div><p id='flowStatus' class='muted flow-load-note'>Editor wird geladen ...</p>"
    "<p id='flowPoolInfo' class='hint'>Szenen-Pool wird geladen ...</p></section>"
    "<div class='flow-editor-host'>"
    "<iframe id='flowEditorFrame' class='flow-editor-frame' title='Ablauf-Editor' data-scene='%u' scrolling='no'></iframe>"
    "<noscript><p><a class='btn' href='/flow/edit?scene=%u'>Editor öffnen</a></p></noscript></div>"
    "<script>window.KLC_FLOW_INITIAL_SCENE=%u;window.KLC_FLOW_COUNT=%u;</script><script src='/flows.js?v=" KLC_VERSION "'></script>"
    "</main></body></html>",
    selected_public_scene,
    selected_public_scene,
    prev_public_scene,
    next_public_scene,
    selected_public_scene,
    g_webui_csrf,
    selected_scene_id,
    g_webui_csrf,
    selected_scene_id,
    g_webui_csrf,
    selected_scene_id,
    selected_scene_id,
    selected_public_scene,
    selected_scene_id,
    KLC_SCENE_MAX_PUBLIC);

  return g_flow_html;
}

const char* klcWebUiSceneFlowEditorHtml(uint8_t selected_scene_id)
{
  size_t used = 0;
  if (selected_scene_id < 1U || selected_scene_id > KLC_SCENE_MAX_PUBLIC) {
    selected_scene_id = 1U;
  }
  if (!klcWebUiEnsureBuffer(g_flow_editor_html, KLC_WEBUI_STREAM_ONLY_LEN)) {
    return KLC_WEBUI_OOM_HTML;
  }

  klcWebUiAppendText(g_flow_editor_html, KLC_WEBUI_STREAM_ONLY_LEN, used,
    "<!doctype html><html lang='de'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>KLC Ablauf-Editor</title><link rel='stylesheet' href='/ui.css'>"
    "<style>html,body{background:transparent;overflow:hidden}body{padding:24px 0}.wrap{max-width:none;padding:0}.wrap:after{content:none}.cards{margin-top:0}input.klc-range-num{max-width:120px}</style>"
    "</head><body><main class='wrap'>");

  // Der Editor wird als eigenstaendiger iframe geladen und nutzt keine Nav,
  // daher die Hide-Regel fuer Experten-Abschnitte hier direkt mitsenden.
  klcWebUiAppendAdvancedHideStyle(g_flow_editor_html, KLC_WEBUI_STREAM_ONLY_LEN, used);

  klcWebUiAppendText(g_flow_editor_html, KLC_WEBUI_STREAM_ONLY_LEN, used,
    "<div class='cards'>");

  for (uint8_t s = selected_scene_id; s <= selected_scene_id; ++s) {
    const KlcSceneConfig& scene = g_config.scenes[s];
    char safe_name[KLC_MAX_NAME_LEN * 6];
    klcWebUiEscapeAttr(scene.name, safe_name, sizeof(safe_name));

    klcWebUiAppendText(g_flow_editor_html, KLC_WEBUI_STREAM_ONLY_LEN, used,
      "<section class='flow-scene-card%s'>"
      "<div class='flow-editor-title'><div><h2>Szene %u</h2><span class='flow-scene-meta'>%s</span></div><span class='flow-scene-state'>%s</span></div>"
      "<form class='flow-scene-body flow-scene-form' method='POST' action='/flow/config'>"
      "<input type='hidden' name='csrf' value='%s'>"
      "<input type='hidden' name='id' value='%u'><input type='hidden' name='revision' value='%lu'><input type='hidden' name='flow_ui_v2' value='1'><input type='hidden' name='start_reverse_direction' value='%u'><input type='hidden' name='start_mirror_center' value='%u'><input type='hidden' name='main_reverse_direction' value='%u'><input type='hidden' name='main_mirror_center' value='%u'><input type='hidden' name='end_reverse_direction' value='%u'><input type='hidden' name='end_mirror_center' value='%u'>"
      ""
      "<h4 class='flow-section-title'>Grunddaten</h4><div class='flow-section'><div class='grid'>"
      "<label title='Schaltet diesen Ablauf ein oder aus. Deaktiviert bleiben alle Werte gespeichert; der Ablauf wird aber weder per KNX noch in der Steuerung angeboten.'>Aktiv<br><input type='checkbox' name='enabled' value='1'%s onchange='klcFlowActiveChanged(this)'></label>"
      "<label title='Freier Anzeigename des Ablaufs. Erscheint in Steuerung, Auswahlfeldern und der Ablauf-Übersicht.'>Name / Label<br><input name='name' maxlength='31' value='%s'></label>"
      "<label title='In den KNX-Szenen-Pool aufnehmen. Nur Pool-Szenen schaltet das KNX-Objekt Nächste/Vorherige Szene durch (Telegramm 1 = weiter, 0 = zurück), aufsteigend mit Umlauf.'>Szenen-Pool<br><input type='checkbox' name='in_pool' value='1'%s> per KNX weiter/zurück schaltbar</label>"
      "</div></div><p class='muted flow-disabled-note'>Diese Szene ist deaktiviert. Die Werte bleiben gespeichert und werden beim erneuten Aktivieren weiterverwendet.</p><div class='flow-advanced'>"
      "<h4 class='flow-section-title klc-adv'>Start-Effekt</h4><div class='flow-section klc-adv'><div class='grid'>"
      "<label title='Animation beim Einschalten des Ablaufs, bevor die Hauptphase beginnt. Kein Start-Effekt heißt: direkt mit der Hauptphase starten.'>Start-Effekt<a class='help-dot' href='/help?q=flows.phase_effect' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select class='js-flow-start-effect' name='start_effect' onchange='klcFlowStartEffectChanged(this)'>"
      "<option value='%u' selected>%s</option>"
      "</select></label>"
      "<label class='flow-start-duration' title='Dauer der Start-Animation in Millisekunden. 0 = sofort; bei Rampe die Aufblendzeit. Leer bzw. 0 nutzt bei Lauf-Effekten die automatische Dauer aus Pixelzahl mal Schrittzeit.'>Start-Dauer / Rampe ms<a class='help-dot' href='/help?q=flows.phase_duration' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='86400000' name='start_duration_ms' value='%lu'><span class='muted'>0 = sofort, z.B. 1000 = 1 s Rampe</span></label>"
      "<span class='flow-row-break'></span><label class='flow-tail-option flow-start-tail' title='Laufende Effekte ziehen in der Startphase einen weich abfallenden Schweif hinter sich her. Die Schweiflänge steht unter Nachleuchten Prozent.'><input type='checkbox' name='start_tail' value='1'%s onchange='klcFlowTailChanged(this)'> Nachleuchten</label>"
      "</div></div><h4 class='flow-section-title'>Ein-Effekt / Hauptphase</h4><div class='flow-section'><div class='grid'>"
      "<label title='Effekt während der Hauptphase, also solange der Ablauf eingeschaltet ist. Aus bedeutet: nach dem Start-Effekt bleibt alles dunkel.'>Während EIN<a class='help-dot' href='/help?q=flows.phase_effect' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select class='js-flow-main-effect' name='main_effect' onchange='klcFlowMainEffectChanged(this)'>"
      "<option value='%u' selected>%s</option>"
      "</select></label>"
      "<label class='klc-adv' title='Dauer der Hauptphase in Millisekunden. 0 = dauerhaft an, bis der Ablauf per KNX oder Steuerung beendet wird. Ein Wert größer 0 startet danach automatisch den Aus-Effekt.'>Hauptdauer ms<a class='help-dot' href='/help?q=flows.phase_duration' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='86400000' name='main_duration_ms' value='%lu'><span class='muted'>0 = dauerhaft</span></label>"
      "<div class='flow-tetris-param flow-wide-option klc-adv'><span class='flow-segment-label'>Tetris je Phase</span><div class='grid'>"
      "<label title='Größe der fallenden Tetris-Bausteine in der Startphase: Zufallsbereich von minimal bis maximal, in Pixeln. Gleiche Werte = feste Blockgröße.'>Start Gruppe min./max<a class='help-dot' href='/help?q=flows.tetris_group' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='1' max='99' name='start_tetris_group_min' value='%u'> <input type='number' min='1' max='99' name='start_tetris_group_max' value='%u'></label>"
      "<label title='Größe der fallenden Tetris-Bausteine in der Hauptphase: Zufallsbereich von minimal bis maximal, in Pixeln.'>Ein Gruppe min./max<br><input type='number' min='1' max='99' name='main_tetris_group_min' value='%u'> <input type='number' min='1' max='99' name='main_tetris_group_max' value='%u'></label>"
      "<label title='Größe der fallenden Tetris-Bausteine in der Ausschaltphase: Zufallsbereich von minimal bis maximal, in Pixeln.'>Aus Gruppe min./max<br><input type='number' min='1' max='99' name='end_tetris_group_min' value='%u'> <input type='number' min='1' max='99' name='end_tetris_group_max' value='%u'></label>"
      "<input type='hidden' name='start_tetris_gap' value='%u'><input type='hidden' name='main_tetris_gap' value='%u'><input type='hidden' name='end_tetris_gap' value='%u'>"
      "<label title='Fallrichtung der Bausteine in der Startphase umdrehen: statt von Pixel 1 Richtung Ende fällt alles vom Ende Richtung Pixel 1.'>Start Richtung<br><span class='flow-motion-checks'><label><input type='checkbox' name='start_tetris_reverse_direction' value='1'%s> umdrehen</label><input type='hidden' name='start_tetris_mirror_center' value='0' data-old='%s'><input type='hidden' name='start_tetris_random_direction' value='0' data-old='%s'><span class='muted'>Mitte/Zufall entfernt</span></span></label>"
      "<label title='Fallrichtung der Bausteine in der Hauptphase umdrehen.'>Ein Richtung<br><span class='flow-motion-checks'><label><input type='checkbox' name='main_tetris_reverse_direction' value='1'%s> umdrehen</label><input type='hidden' name='main_tetris_mirror_center' value='0' data-old='%s'><input type='hidden' name='main_tetris_random_direction' value='0' data-old='%s'><span class='muted'>Mitte/Zufall entfernt</span></span></label>"
      "<label title='Fallrichtung der Bausteine in der Ausschaltphase umdrehen.'>Aus Richtung<br><span class='flow-motion-checks'><label><input type='checkbox' name='end_tetris_reverse_direction' value='1'%s> umdrehen</label><input type='hidden' name='end_tetris_mirror_center' value='0' data-old='%s'><input type='hidden' name='end_tetris_random_direction' value='0' data-old='%s'><span class='muted'>Mitte/Zufall entfernt</span></span></label>"
      "<label title='Jeder Baustein der Startphase bekommt eine zufällige Farbe statt der Szenenfarbe.'><input type='checkbox' name='start_tetris_random_colors' value='1'%s> Start zufällige Farben</label><label title='Jeder Baustein der Hauptphase bekommt eine zufällige Farbe statt der Szenenfarbe.'><input type='checkbox' name='main_tetris_random_colors' value='1'%s> Ein zufällige Farben</label><label title='Jeder Baustein der Ausschaltphase bekommt eine zufällige Farbe statt der Szenenfarbe.'><input type='checkbox' name='end_tetris_random_colors' value='1'%s> Aus zufällige Farben</label>"
      "<label class='flow-tetris-cycle-param' title='Wie der fertige Tetris-Aufbau der Startphase wieder verschwindet, bevor die Hauptphase übernimmt.'>Start Abbauart<a class='help-dot' href='/help?q=flows.tetris_teardown' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select name='start_tetris_teardown_mode'><option value='0'%s>Original-Abbau</option><option value='1'%s>zufällig rausfallen</option><option value='2'%s>weiter schieben</option><option value='3'%s>ausperlen</option><option value='4'%s>Fade Out</option><option value='5'%s>abwechselnd</option><option value='6'%s>zufällig</option></select></label>"
      "<label class='flow-tetris-cycle-param' title='Wie ein fertiger Tetris-Aufbau in der Hauptphase abgebaut wird, bevor der nächste Aufbau-Zyklus beginnt.'>Ein Übergang<br><select name='main_tetris_teardown_mode'><option value='0'%s>Original-Abbau</option><option value='1'%s>zufällig rausfallen</option><option value='2'%s>weiter schieben</option><option value='3'%s>ausperlen</option><option value='4'%s>Fade Out</option><option value='5'%s>abwechselnd</option><option value='6'%s>zufällig</option></select></label>"
      "<label class='flow-tetris-cycle-param' title='Wie der Tetris-Abbau in der Ausschaltphase abläuft, bis alle Pixel dunkel sind.'>Aus Abbauart<br><select name='end_tetris_teardown_mode'><option value='0'%s>Original-Abbau</option><option value='1'%s>zufällig rausfallen</option><option value='2'%s>weiter schieben</option><option value='3'%s>ausperlen</option><option value='4'%s>Fade Out</option><option value='5'%s>abwechselnd</option><option value='6'%s>zufällig</option></select></label>"
      "<label class='wide' title='Statt fester Schrittzeit werden Pause und Fallzeit jedes Bausteins zufällig aus den Bereichen darunter gewürfelt. Wirkt organischer, aber weniger vorhersehbar.'><input type='checkbox' name='tetris_random_timing' value='1'%s> Tetris Zufalls-Timing <span class='muted'>Pause zwischen Bausteinen und Fallzeit pro Baustein werden zufällig bestimmt.</span><a class='help-dot' href='/help?q=flows.tetris_random_timing' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a></label>"
      "<label title='Zufallsbereich der Wartezeit zwischen zwei Bausteinen in Millisekunden. Gilt nur bei aktivem Zufalls-Timing.'>Pause min./max ms<br><input type='number' min='0' max='10000' name='tetris_pause_min_ms' value='%u'> <input type='number' min='0' max='10000' name='tetris_pause_max_ms' value='%u'></label>"
      "<label title='Zufallsbereich der Fallzeit eines einzelnen Bausteins in Millisekunden. Gilt nur bei aktivem Zufalls-Timing.'>Zufalls-Fallzeit pro Baustein min./max ms<br><input type='number' min='0' max='10000' name='tetris_step_min_ms' value='%u'> <input type='number' min='0' max='10000' name='tetris_step_max_ms' value='%u'></label>"
      "<label title='Zeigt den nächsten fallenden Block schon gedimmt am Startpunkt an, wie die Vorschau beim Tetris-Spiel.'><input type='checkbox' name='tetris2_next_preview' value='1'%s> Tetris_2: Nächsten einblenden</label><label title='Wechselt die Fallrichtung nach jedem fertigen Aufbau: einmal von vorn, einmal von hinten.'><input type='checkbox' name='tetris2_direction_alternate' value='1'%s> Tetris_2: Richtung wechseln</label><label title='Alle Kanäle und String-Segmente bauen im Gleichtakt auf statt unabhängig voneinander.'><input type='checkbox' name='tetris2_sync_segments' value='1'%s> Tetris_2: Alle Kanäle/Segmente synchron</label><label title='Jeder Block bekommt eine zufällige Farbe mit dem eingestellten HSV-Mindestabstand zur vorherigen.'><input type='checkbox' name='tetris2_random_colors' value='1'%s> Tetris_2: Zufällige Farben</label><label title='Dreht die Grundrichtung des Fallens um: Blöcke stapeln sich am Anfang des Strangs statt am Ende.'><input type='checkbox' name='tetris2_reverse_direction' value='1'%s> Tetris_2: Richtung umdrehen</label>"
      "<label title='Zufallsbereich der Blockgröße in Pixeln. Gleiche Werte = feste Größe.'>Tetris_2 Blockgröße min./max<a class='help-dot' href='/help?q=flows.tetris2_block_size' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='1' max='99' name='tetris2_block_min' value='%u'> <input type='number' min='1' max='99' name='tetris2_block_max' value='%u'></label><label title='Wie ein fertiger Tetris_2-Aufbau wieder verschwindet.'>Tetris_2 Abbauart<br><select name='tetris2_teardown_mode'><option value='0'%s>Original-Abbau</option><option value='1'%s>zufällig rausfallen</option><option value='2'%s>weiter schieben</option><option value='3'%s>ausperlen</option><option value='4'%s>Fade Out</option><option value='5'%s>abwechselnd</option><option value='6'%s>zufällig</option></select></label>"
      "<label title='Zufallsbereich der Wartezeit zwischen zwei Blöcken in Millisekunden.'>Tetris_2 Pause min./max ms<br><input type='number' min='0' max='10000' name='tetris2_pause_min_ms' value='%u'> <input type='number' min='0' max='10000' name='tetris2_pause_max_ms' value='%u'></label><label title='LED-Dichte des Strangs. Rechnet die Geschwindigkeit in mm/s in Pixel pro Sekunde um; bei 60er-Strips 60 eintragen.'>Tetris_2 Pixel/Meter<a class='help-dot' href='/help?q=flows.tetris2_pixels_per_meter' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='1' max='2000' name='tetris2_pixels_per_meter' value='%u'></label><label title='Zufallsbereich der Fallgeschwindigkeit in Millimetern pro Sekunde, unabhängig von der LED-Dichte.'>Tetris_2 Geschwindigkeit min./max mm/s<a class='help-dot' href='/help?q=flows.tetris2_speed' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='1' max='20000' name='tetris2_speed_min_mm_s' value='%u'> <input type='number' min='1' max='20000' name='tetris2_speed_max_mm_s' value='%u'></label>"
      "<label title='Wahrscheinlichkeit in Prozent, dass der nächste Block schon startet, während der aktuelle noch fällt.'>Tetris_2 Frühstart Chance %%<a class='help-dot' href='/help?q=flows.tetris2_early_start' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='100' name='tetris2_early_start_chance_pct' value='%u'></label><label title='Wie früh der nächste Block startet: Zufallsbereich in Prozent der Reststrecke des aktuellen Blocks.'>Tetris_2 Frühstart min./max %%<br><input type='number' min='0' max='95' name='tetris2_early_start_min_pct' value='%u'> <input type='number' min='0' max='95' name='tetris2_early_start_max_pct' value='%u'></label><label title='Mindestabstand aufeinanderfolgender Zufallsfarben auf dem HSV-Farbkreis (0-180 Grad). Höher = deutlicher unterscheidbare Farben.'>Tetris_2 HSV-Mindestabstand<a class='help-dot' href='/help?q=flows.tetris2_hsv_distance' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='180' name='tetris2_hsv_min_distance' value='%u'></label>"
      "</div><input type='hidden' name='tetris_group_min' value='%u'><input type='hidden' name='tetris_group_max' value='%u'><input type='hidden' name='tetris_gap' value='%u'><input type='hidden' name='tetris_teardown_mode' value='%u'><span class='muted'>Tetris_2 ist komplett neu und nutzt Seed-Planung, fallende Blöcke ohne Abstand, keine Mitte-/Zufallsrichtung und Geschwindigkeit in mm/s über Pixel/Meter. Die alten Tetris-Werte bleiben als Legacy/Feinwerte gespeichert.</span></div>"
      "<div class='flow-sparkle-param scene-lit-percent klc-adv'><span class='flow-segment-label'>Funkeln</span><div class='grid'><label title='Abstand zwischen dem Aufleuchten neuer Funken in der Startphase, in Millisekunden. Kleiner = dichteres Geflimmer.'>Start-Funkel Geschwindigkeit ms<a class='help-dot' href='/help?q=flows.sparkle_speed' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='60000' name='start_sparkle_speed_ms' value='%u'></label><label title='Abstand zwischen dem Aufleuchten neuer Funken in der Hauptphase, in Millisekunden.'>Ein-Funkel Geschwindigkeit ms<br><input type='number' min='0' max='60000' name='main_sparkle_speed_ms' value='%u'></label><label title='Abstand zwischen dem Aufleuchten neuer Funken in der Ausschaltphase, in Millisekunden.'>Aus-Funkel Geschwindigkeit ms<br><input type='number' min='0' max='60000' name='end_sparkle_speed_ms' value='%u'></label>"
      "<label title='Wie viel Prozent der LEDs in der Startphase gleichzeitig funkeln sollen.'>Start-Funkel Füllgrad %%<a class='help-dot' href='/help?q=flows.sparkle_fill' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='100' name='start_sparkle_fill_percent' value='%u'></label><label title='Wie viel Prozent der LEDs in der Hauptphase gleichzeitig funkeln sollen.'>Ein-Funkel Füllgrad %%<br><input type='number' min='0' max='100' name='main_sparkle_fill_percent' value='%u'></label><label title='Wie viel Prozent der LEDs in der Ausschaltphase gleichzeitig funkeln sollen.'>Aus-Funkel Füllgrad %%<br><input type='number' min='0' max='100' name='end_sparkle_fill_percent' value='%u'></label>"
      "<label title='Wie lange ein einzelner Funke der Startphase leuchtet und ausglimmt, in Millisekunden.'>Start-Funkel Lebenszeit ms<a class='help-dot' href='/help?q=flows.sparkle_lifetime' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='60000' name='start_sparkle_lifetime_ms' value='%u'></label><label title='Wie lange ein einzelner Funke der Hauptphase leuchtet und ausglimmt, in Millisekunden.'>Ein-Funkel Lebenszeit ms<br><input type='number' min='0' max='60000' name='main_sparkle_lifetime_ms' value='%u'></label><label title='Wie lange ein einzelner Funke der Ausschaltphase leuchtet und ausglimmt, in Millisekunden.'>Aus-Funkel Lebenszeit ms<br><input type='number' min='0' max='60000' name='end_sparkle_lifetime_ms' value='%u'></label></div><input type='hidden' name='sparkle_speed_ms' value='%u'><input type='hidden' name='sparkle_fill_percent' value='%u'><input type='hidden' name='sparkle_lifetime_ms' value='%u'><span class='muted'>Funkeln kann je Phase anders schnell, dicht und lang ausglimmend laufen. Legacy-Werte folgen der Ein-Phase.</span></div>"
      "<div class='flow-pulse-period scene-lit-percent klc-adv'><label title='Dauer eines kompletten Puls- oder Blinkzyklus der Startphase in Millisekunden, z.B. 2000 = alle 2 Sekunden ein Zyklus.'>Start-Puls-/Blinkperiode ms<a class='help-dot' href='/help?q=flows.pulse_period' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='86400000' name='start_pulse_period_ms' value='%lu'></label><label title='Dauer eines kompletten Puls- oder Blinkzyklus der Hauptphase in Millisekunden.'>Ein-Puls-/Blinkperiode ms<br><input type='number' min='0' max='86400000' name='main_pulse_period_ms' value='%lu'></label><label title='Dauer eines kompletten Puls- oder Blinkzyklus der Ausschaltphase in Millisekunden.'>Aus-Puls-/Blinkperiode ms<br><input type='number' min='0' max='86400000' name='end_pulse_period_ms' value='%lu'></label><input type='hidden' name='pulse_period_ms' value='%lu'><span class='muted'>Steuert Pulsation und Warnblinken getrennt je Phase.</span></div>"
      "<div class='flow-segment-param klc-adv'><span class='flow-segment-label'>Segmentleuchten</span><div class='grid'><label title='Blockgröße des Segmentleuchtens in der Startphase, in LEDs.'>Start Segment LEDs<a class='help-dot' href='/help?q=flows.segment_size' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input class='js-segment-size' type='number' min='0' max='255' name='start_segment_percent' value='%u'></label><label title='Blockgröße des Segmentleuchtens in der Hauptphase, in LEDs.'>Ein Segment LEDs<br><input class='js-segment-size' type='number' min='0' max='255' name='main_segment_percent' value='%u'></label><label title='Blockgröße des Segmentleuchtens in der Ausschaltphase, in LEDs.'>Aus Segment LEDs<br><input class='js-segment-size' type='number' min='0' max='255' name='end_segment_percent' value='%u'></label><label title='Anzahl LEDs außerhalb des Segments, über die linear ausgeblendet wird. 0 = harte Kante.'>Weiche Kante LEDs<a class='help-dot' href='/help?q=flows.segment_soft_edge' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input class='js-segment-soft-edge' type='number' min='0' max='255' name='segment_soft_edge_pixels' value='%u'></label></div><input type='hidden' name='segment_percent' value='%u'><span class='muted flow-segment-help'>Segmentleuchten: Blockgröße in LEDs je Phase. Weiche Kante ist die Anzahl LEDs außerhalb des Segments, über die linear ausgeblendet wird. Beispiel 5: 80%%, 60%%, 40%%, 20%%, 0%%. Maximal jeweilige Segmentgröße, technisch 0..255. 0 = harte Kante. Legacy-Wert folgt dem Ein-Segment.</span></div>"
      "<span class='flow-row-break'></span><label class='flow-tail-option flow-main-tail klc-adv' title='Laufende Effekte ziehen in der Hauptphase einen weich abfallenden Schweif hinter sich her.'><input type='checkbox' name='main_tail' value='1'%s onchange='klcFlowTailChanged(this)'> Nachleuchten</label>"
      "<div class='flow-tail-percent-param scene-lit-percent klc-adv'><label title='Schweiflänge der Startphase in Prozent: Gesamtlänge aus hellem Kopf plus weich abfallendem Schweif.'>Start-Nachleuchten %%<a class='help-dot' href='/help?q=flows.tail_percent' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='200' name='start_tail_percent' value='%u'></label><label title='Schweiflänge der Hauptphase in Prozent.'>Ein-Nachleuchten %%<br><input type='number' min='0' max='200' name='main_tail_percent' value='%u'></label><label title='Schweiflänge der Ausschaltphase in Prozent.'>Aus-Nachleuchten %%<br><input type='number' min='0' max='200' name='end_tail_percent' value='%u'></label><input type='hidden' name='tail_percent' value='%u'><span class='muted flow-tail-help'>Gesamtlänge aus hellem Kopf plus Schweif getrennt pro Phase. Beispiel: 5%% Füllgrad und 50%% Nachleuchten = 5%% voll hell, danach weich bis 50%% auf dunkel.</span></div>"
      "<span class='flow-row-break'></span><label class='flow-wave-bounce-param flow-wide-option klc-adv' title='Ping-Pong-Modus für Wellen: Am Strangende kehrt die Welle um, statt am Anfang neu zu starten.'><input type='checkbox' name='wave_bounce' value='1'%s> Welle am Rand zurückwerfen <span class='muted'>Ping-Pong: laufende Wellen schwappen hin und her, statt am anderen Rand neu zu starten.</span><a class='help-dot' href='/help?q=flows.wave_bounce' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a></label>"
      "</div></div><h4 class='flow-section-title klc-adv'>Aus-Effekt</h4><div class='flow-section klc-adv'><div class='grid'>"
      "<label title='Animation beim Beenden des Ablaufs, nachdem die Hauptphase vorbei ist oder ein Aus-Befehl kam. Sofort aus schaltet ohne Animation ab.'>Aus-Effekt<a class='help-dot' href='/help?q=flows.phase_effect' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select class='js-flow-end-effect' name='end_effect' onchange='klcFlowEndEffectChanged(this)'>"
      "<option value='%u' selected>%s</option>"
      "</select></label>"
      "<label class='flow-end-duration' title='Dauer der Aus-Animation in Millisekunden. 0 = sofort; bei Rampe die Abblendzeit.'>Aus-Dauer / Rampe ms<br><input type='number' min='0' max='86400000' name='end_duration_ms' value='%lu'><span class='muted'>0 = sofort</span></label>"
      "<span class='flow-row-break'></span><label class='flow-tail-option flow-end-tail' title='Laufende Effekte ziehen in der Ausschaltphase einen weich abfallenden Schweif hinter sich her.'><input type='checkbox' name='end_tail' value='1'%s onchange='klcFlowTailChanged(this)'> Nachleuchten</label>"
      "</div></div><h4 class='flow-section-title'>Globale Effekt-Einstellungen</h4><div class='flow-section'><div class='grid'>"
      "<label class='klc-adv' title='Weiches Überblenden an den Phasenwechseln Start zu EIN und EIN zu Aus, in Millisekunden. 0 = harter Wechsel.'>Übergangs-Dauer ms<a class='help-dot' href='/help?q=flows.transition_duration' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='60000' name='transition_duration_ms' value='%lu'><span class='muted'>weiches Überblenden Start → EIN und EIN → Aus, z.B. 300 ms</span></label>"
      "<label title='Rotanteil der Szenenfarbe, 0 bis 255.'>R<br><input type='number' min='0' max='255' name='r' value='%u'></label>"
      "<label title='Grünanteil der Szenenfarbe, 0 bis 255.'>G<br><input type='number' min='0' max='255' name='g' value='%u'></label>"
      "<label title='Blauanteil der Szenenfarbe, 0 bis 255.'>B<br><input type='number' min='0' max='255' name='b' value='%u'></label>"
      "<label title='Weißanteil der Szenenfarbe, 0 bis 255. Wirkt nur bei RGBW-LEDs; bei RGB wird er den Farbkanälen zugemischt.'>W<br><input type='number' min='0' max='255' name='w' value='%u'></label>"
      "<label title='Grundhelligkeit des Ablaufs, 0 bis 255. Wird durch die Max. Helligkeit des Ausgangs begrenzt.'>Helligkeit<br><input type='number' min='0' max='255' name='brightness' value='%u'></label>"
      "<label class='klc-adv' title='Legt fest, ob der Füllgrad in Prozent der Stranglänge oder als feste Pixelanzahl gilt.'>Betroffene LEDs<a class='help-dot' href='/help?q=flows.pixel_mode' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select class='js-scene-pixel-mode' name='pixel_mode' onchange='klcScenePixelModeChanged(this)'>"
      "<option value='0'%s>Relativ in Prozent</option>"
      "<option value='1'%s>Absolut als Pixelanzahl</option>"
      "</select></label>"
      "<div class='scene-lit-percent' style='%s'><label class='klc-adv' title='Wie viel Prozent des Strangs die Startphase nutzt.'>Start-Füllgrad %%<a class='help-dot' href='/help?q=flows.phase_fill' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='100' name='start_fill_percent' value='%u'></label><label title='Wie viel Prozent des Strangs die Hauptphase nutzt. 100 = ganze Leiste.'>Ein-Füllgrad %%<br><input type='number' min='0' max='100' name='main_fill_percent' value='%u'></label><label class='klc-adv' title='Wie viel Prozent des Strangs die Ausschaltphase nutzt.'>Aus-Füllgrad %%<br><input type='number' min='0' max='100' name='end_fill_percent' value='%u'></label><input type='hidden' name='lit_percent' value='%u'><span class='muted klc-adv'>Damit lassen sich Start-, Haupt- und Ausschaltanimation mit unterschiedlichem Füllgrad kombinieren.</span></div>"
      "<label class='scene-lit-pixels klc-adv' style='%s' title='Feste Anzahl betroffener LEDs, wenn Betroffene LEDs auf Absolut steht. Gilt für alle Phasen.'># Pixel<a class='help-dot' href='/help?q=flows.lit_percent' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='5000' name='lit_pixels' value='%u'></label>"
      "<div class='flow-step-ms scene-lit-percent'><label class='klc-adv' title='Millisekunden pro Pixel-Schritt für Lauflicht und Wellen in der Startphase. Kleiner = schneller.'>Start-Schrittzeit ms<a class='help-dot' href='/help?q=flows.step_ms' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='60000' name='start_step_ms' value='%u'></label><label title='Millisekunden pro Pixel-Schritt für Lauflicht und Wellen in der Hauptphase.'>Ein-Schrittzeit ms<br><input type='number' min='0' max='60000' name='main_step_ms' value='%u'></label><label class='klc-adv' title='Millisekunden pro Pixel-Schritt für Lauflicht und Wellen in der Ausschaltphase.'>Aus-Schrittzeit ms<br><input type='number' min='0' max='60000' name='end_step_ms' value='%u'></label><input type='hidden' name='speed_ms' value='%u'><span class='muted'>Steuert Lauflicht und Wellen je Schritt; Tetris_2 nutzt eigene Geschwindigkeit in mm/s über Pixel/Meter.</span></div>"
      "<label class='klc-adv' title='Verschiebt die komplette Zeitleiste jedes weiteren Kanals um diesen Wert nach hinten. Kanal 1 startet sofort, Kanal 2 nach einfachem, Kanal 3 nach doppeltem Versatz.'>Start-Versatz je Kanal ms<a class='help-dot' href='/help?q=flows.channel_delay' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' min='0' max='60000' name='global_delay_ms' value='%u'><span class='muted'>Staffelt die Ausgänge/Kanäle beim Einschalten. 0 = alle gleichzeitig.</span></label>"
      "<label class='klc-adv' title='Verzögert bei Ausgängen mit String-Segmenten jedes folgende Segment um diesen Wert beim Einschalten.'>String-Segmente Startversatz je Segment ms<br><input type='number' min='0' max='60000' name='string_segment_start_delay_ms' value='%u'><span class='muted'>0 = alle Segmente synchron; 3000 = jedes folgende Segment 3 s später.</span></label>"
      "<label class='klc-adv' title='Gestaffeltes Ausschalten: bei String-Segmenten je Segment, sonst je Kanal.'>Stopp-Versatz je Segment/Kanal ms<br><input type='number' min='0' max='60000' name='string_segment_stop_delay_ms' value='%u'><span class='muted'>Hat der Ausgang String-Segmente: je Segment. Sonst: staffelt die Ausgänge/Kanäle beim Ausschalten. 0 = alles gleichzeitig aus.</span></label>"
      "<label class='klc-adv' title='Start/Stop zeitsynchron: alle Stränge beginnen und enden gemeinsam, kurze Stränge laufen langsamer. Laufzeitsynchron: gleiche Schrittzeit überall, kurze Stränge sind früher fertig.'>Globale Lauflicht-/Wellen-Synchronisation<a class='help-dot' href='/help?q=flows.sync_mode' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><select name='sync_mode'>"
      "<option value='0'%s>Start/Stop zeitsynchron</option>"
      "<option value='1'%s>Laufzeitsynchron</option>"
      "</select></label>"
      "<div class='flow-fw-param flow-wide-option' style='%s'><span class='flow-segment-label'>Feuerwerk</span><span class='muted'>Die optimale Einstellung ist fest hinterlegt (ruhiger Takt, maximale Funkenzahl) – hier gibt es nichts mehr einzustellen. Funken sind bunt, Helligkeit folgt der Szenenfarbe.</span></div>"
      "<div class='flow-tx-param flow-wide-option' style='%s'><span class='flow-segment-label'>Tetrix</span><div class='grid'>"
      "<label title='Fallgeschwindigkeit der Tetrix-Blöcke, 0 bis 255. Höher = schnellerer Fall.'>Geschwindigkeit<a class='help-dot' href='/help?q=flows.tetrix' title='Hilfe zu diesem Feld' onclick='event.stopPropagation()'>?</a><br><input type='number' class='js-slider' min='0' max='255' name='tetrix_speed' value='%u'></label>"
      "<label title='Größe der fallenden Blöcke in Pixeln. 0 = zufällige Größe je Block.'>Blockbreite<br><input type='number' class='js-slider' min='0' max='255' name='tetrix_width' value='%u'></label>"
      "</div><span class='muted'>Geschwindigkeit: höher = schnellerer Fall. Blockbreite: Größe der fallenden Blöcke (0 = zufällige Größe je Block). Farben werden zufällig je Block gewählt.</span></div>"
      "</div></div><p class='muted klc-adv'>Ablauf: %s → %s → %s</p></div><div class='row'><button class='btn small' type='submit'>Ablauf speichern</button><a class='btn small secondary' href='/preview?scene=%u&t=0'>Vorschau öffnen</a></div></form></section>",
      scene.enabled ? "" : " flow-scene-disabled",
      (unsigned)scene.id,
      safe_name,
      scene.enabled ? "aktiv" : "inaktiv",
      g_webui_csrf,
      scene.id,
      (unsigned long)klcSceneStoreRevision(scene.id),
      scene.start_reverse_direction ? 1 : 0,
      scene.start_mirror_center ? 1 : 0,
      scene.main_reverse_direction ? 1 : 0,
      scene.main_mirror_center ? 1 : 0,
      scene.end_reverse_direction ? 1 : 0,
      scene.end_mirror_center ? 1 : 0,
      scene.enabled ? " checked" : "",
      safe_name,
      scene.in_pool ? " checked" : "",
      (unsigned)scene.start_effect,
      klcWebUiScenePhaseEffectText(scene.start_effect),
      (unsigned long)scene.start_duration_ms,
      klcWebUiChecked(klcEffectHasFlag(KLC_EFFECT_DOMAIN_PHASE, scene.start_effect, KLC_EFFECT_FLAG_TAIL)),
      (unsigned)scene.main_effect,
      klcWebUiSceneMainEffectText(scene.main_effect),
      (unsigned long)scene.main_duration_ms,
      scene.start_tetris_group_min,
      scene.start_tetris_group_max,
      scene.main_tetris_group_min,
      scene.main_tetris_group_max,
      scene.end_tetris_group_min,
      scene.end_tetris_group_max,
      scene.start_tetris_gap,
      scene.main_tetris_gap,
      scene.end_tetris_gap,
      klcWebUiChecked(scene.start_tetris_reverse_direction),
      klcWebUiChecked(scene.start_tetris_mirror_center),
      klcWebUiChecked(scene.start_tetris_random_direction),
      klcWebUiChecked(scene.main_tetris_reverse_direction),
      klcWebUiChecked(scene.main_tetris_mirror_center),
      klcWebUiChecked(scene.main_tetris_random_direction),
      klcWebUiChecked(scene.end_tetris_reverse_direction),
      klcWebUiChecked(scene.end_tetris_mirror_center),
      klcWebUiChecked(scene.end_tetris_random_direction),
      klcWebUiChecked(scene.start_tetris_random_colors),
      klcWebUiChecked(scene.main_tetris_random_colors),
      klcWebUiChecked(scene.end_tetris_random_colors),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_REVERSE),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM_DROP),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_WIPE),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_SOFT_DISSOLVE),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_FADE_OUT),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_ALTERNATE),
      klcWebUiSelected(scene.start_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_REVERSE),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM_DROP),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_WIPE),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_SOFT_DISSOLVE),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_FADE_OUT),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_ALTERNATE),
      klcWebUiSelected(scene.main_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_REVERSE),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM_DROP),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_WIPE),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_SOFT_DISSOLVE),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_FADE_OUT),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_ALTERNATE),
      klcWebUiSelected(scene.end_tetris_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM),
      klcWebUiChecked(scene.tetris_random_timing),
      scene.tetris_pause_min_ms,
      scene.tetris_pause_max_ms,
      scene.tetris_step_min_ms,
      scene.tetris_step_max_ms,
      klcWebUiChecked(scene.tetris2_next_preview),
      klcWebUiChecked(scene.tetris2_direction_alternate),
      klcWebUiChecked(scene.tetris2_sync_segments),
      klcWebUiChecked(scene.tetris2_random_colors),
      klcWebUiChecked(scene.tetris2_reverse_direction),
      scene.tetris2_block_min,
      scene.tetris2_block_max,
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_REVERSE),
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM_DROP),
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_WIPE),
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_SOFT_DISSOLVE),
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_FADE_OUT),
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_ALTERNATE),
      klcWebUiSelected(scene.tetris2_teardown_mode == KLC_SCENE_TETRIS_TEARDOWN_RANDOM),
      scene.tetris2_pause_min_ms,
      scene.tetris2_pause_max_ms,
      scene.tetris2_pixels_per_meter,
      scene.tetris2_speed_min_mm_s,
      scene.tetris2_speed_max_mm_s,
      scene.tetris2_early_start_chance_pct,
      scene.tetris2_early_start_min_pct,
      scene.tetris2_early_start_max_pct,
      scene.tetris2_hsv_min_distance,
      scene.main_tetris_group_min,
      scene.main_tetris_group_max,
      scene.main_tetris_gap,
      scene.main_tetris_teardown_mode,
      scene.start_sparkle_speed_ms,
      scene.main_sparkle_speed_ms,
      scene.end_sparkle_speed_ms,
      scene.start_sparkle_fill_percent,
      scene.main_sparkle_fill_percent,
      scene.end_sparkle_fill_percent,
      scene.start_sparkle_lifetime_ms,
      scene.main_sparkle_lifetime_ms,
      scene.end_sparkle_lifetime_ms,
      scene.main_sparkle_speed_ms,
      scene.main_sparkle_fill_percent,
      scene.main_sparkle_lifetime_ms,
      (unsigned long)scene.start_pulse_period_ms,
      (unsigned long)scene.main_pulse_period_ms,
      (unsigned long)scene.end_pulse_period_ms,
      (unsigned long)scene.main_pulse_period_ms,
      scene.start_segment_percent,
      scene.main_segment_percent,
      scene.end_segment_percent,
      scene.segment_soft_edge_pixels,
      scene.main_segment_percent,
      klcWebUiChecked(klcEffectHasFlag(KLC_EFFECT_DOMAIN_MAIN, scene.main_effect, KLC_EFFECT_FLAG_TAIL)),
      scene.start_tail_percent,
      scene.main_tail_percent,
      scene.end_tail_percent,
      scene.main_tail_percent,
      klcWebUiChecked(scene.wave_bounce),
      (unsigned)scene.end_effect,
      klcWebUiScenePhaseEffectText(scene.end_effect),
      (unsigned long)scene.end_duration_ms,
      klcWebUiChecked(klcEffectHasFlag(KLC_EFFECT_DOMAIN_PHASE, scene.end_effect, KLC_EFFECT_FLAG_TAIL)),
      (unsigned long)scene.transition_duration_ms,
      scene.r,
      scene.g,
      scene.b,
      scene.w,
      scene.brightness,
      klcWebUiSelected(scene.pixel_mode == KLC_SCENE_PIXELS_PERCENT),
      klcWebUiSelected(scene.pixel_mode == KLC_SCENE_PIXELS_ABSOLUTE),
      scene.pixel_mode == KLC_SCENE_PIXELS_ABSOLUTE ? "display:none" : "",
      scene.start_fill_percent,
      scene.main_fill_percent,
      scene.end_fill_percent,
      scene.main_fill_percent,
      scene.pixel_mode == KLC_SCENE_PIXELS_ABSOLUTE ? "" : "display:none",
      scene.lit_pixels,
      scene.start_step_ms,
      scene.main_step_ms,
      scene.end_step_ms,
      scene.main_step_ms,
      scene.global_delay_ms,
      scene.string_segment_start_delay_ms,
      scene.string_segment_stop_delay_ms,
      klcWebUiSelected(scene.sync_mode == KLC_SCENE_SYNC_START_STOP),
      klcWebUiSelected(scene.sync_mode == KLC_SCENE_SYNC_RUNTIME),
      (klcEffectHasFlag(KLC_EFFECT_DOMAIN_MAIN, scene.main_effect, KLC_EFFECT_FLAG_FIREWORKS) || klcEffectHasFlag(KLC_EFFECT_DOMAIN_PHASE, scene.start_effect, KLC_EFFECT_FLAG_FIREWORKS) || klcEffectHasFlag(KLC_EFFECT_DOMAIN_PHASE, scene.end_effect, KLC_EFFECT_FLAG_FIREWORKS)) ? "" : "display:none",
      (klcEffectHasFlag(KLC_EFFECT_DOMAIN_MAIN, scene.main_effect, KLC_EFFECT_FLAG_TETRIX) || klcEffectHasFlag(KLC_EFFECT_DOMAIN_PHASE, scene.start_effect, KLC_EFFECT_FLAG_TETRIX) || klcEffectHasFlag(KLC_EFFECT_DOMAIN_PHASE, scene.end_effect, KLC_EFFECT_FLAG_TETRIX)) ? "" : "display:none",
      scene.tetrix_speed,
      scene.tetrix_width,
      scene.start_effect == KLC_SCENE_PHASE_NONE ? "kein Start-Effekt" : klcWebUiScenePhaseEffectText(scene.start_effect),
      klcWebUiSceneMainEffectText(scene.main_effect),
      scene.end_effect == KLC_SCENE_PHASE_NONE ? "kein Aus-Effekt" : klcWebUiScenePhaseEffectText(scene.end_effect),
      (unsigned)scene.id);
  }

  klcWebUiAppendText(g_flow_editor_html, KLC_WEBUI_STREAM_ONLY_LEN, used,
    "</div><script src='/i18n.js'></script><script src='/effects.js?v=" KLC_VERSION "'></script><script src='/flow/edit.js?v=" KLC_VERSION "'></script>"
    // Sliderize bewusst INLINE in der frischen (no-store) Editor-HTML, damit es
    // unabhaengig von einem evtl. veralteten /flow/edit.js-Cache zuverlaessig
    // greift. Ergaenzt ausgewaehlte Zahlenfelder um einen synchronen Schieberegler.
    "<script>(function(){var f=document.querySelector('form');if(!f)return;"
    "var names=['r','g','b','w','brightness','start_fill_percent','main_fill_percent','end_fill_percent','start_tail_percent','main_tail_percent','end_tail_percent','start_segment_percent','main_segment_percent','end_segment_percent','start_sparkle_fill_percent','main_sparkle_fill_percent','end_sparkle_fill_percent','start_tetris_group_min','main_tetris_group_min','end_tetris_group_min','start_tetris_group_max','main_tetris_group_max','end_tetris_group_max','start_tetris_gap','main_tetris_gap','end_tetris_gap','fireworks_speed','fireworks_intensity','tetrix_speed','tetrix_width','start_step_ms','main_step_ms','end_step_ms','start_sparkle_speed_ms','main_sparkle_speed_ms','end_sparkle_speed_ms','start_sparkle_lifetime_ms','main_sparkle_lifetime_ms','end_sparkle_lifetime_ms','tetris_pause_min_ms','tetris_pause_max_ms','tetris_step_min_ms','tetris_step_max_ms','tetris2_block_min','tetris2_block_max','tetris2_pause_min_ms','tetris2_pause_max_ms','tetris2_pixels_per_meter','tetris2_speed_min_mm_s','tetris2_speed_max_mm_s','tetris2_early_start_chance_pct','tetris2_early_start_min_pct','tetris2_early_start_max_pct','tetris2_hsv_min_distance'];"
    "names.forEach(function(nm){var inp=f.querySelector('input[name='+JSON.stringify(nm)+']');"
    "if(!inp||inp.getAttribute('type')!=='number'||inp.dataset.kslider)return;"
    "var mn=parseFloat(inp.getAttribute('min'));if(isNaN(mn))mn=0;var mx=parseFloat(inp.getAttribute('max'));if(isNaN(mx))return;"
    "inp.dataset.kslider='1';inp.classList.add('klc-range-num');"
    "var r=document.createElement('input');r.type='range';r.min=mn;r.max=mx;r.step=(mx-mn)>2000?Math.max(1,Math.round((mx-mn)/300)):1;r.value=(inp.value===''?mn:inp.value);r.className='klc-range';"
    "r.addEventListener('input',function(){inp.value=r.value;inp.dispatchEvent(new Event('input',{bubbles:true}));});"
    "inp.addEventListener('input',function(){if(document.activeElement!==r)r.value=inp.value;});"
    "inp.parentNode.insertBefore(r,inp);});})();</script>"
    "</main></body></html>");
  return g_flow_editor_html;

}


const char* klcWebUiSceneFlowJs()
{
  return
    "var klcFlowItems=[];var klcFlowActive=parseInt(window.KLC_FLOW_INITIAL_SCENE||'1',10)||1;var klcFlowEditorTimer=null;var klcFlowLoadSeq=0;"
    "function klcFlowPublicId(id){return parseInt(id||'0',10)||0;}"
    "function klcFlowSceneUrl(id){return '/flow/edit?scene='+encodeURIComponent(klcFlowPublicId(id));}"
    "function klcFlowClampScene(id){var max=parseInt(window.KLC_FLOW_COUNT||'50',10)||50;id=parseInt(id||'1',10)||1;if(id<1)id=1;if(id>max)id=max;return id;}"
    "function klcFlowSetText(id,txt){var e=document.getElementById(id);if(e)e.textContent=txt;}"
    "function klcFlowItemById(id){id=klcFlowClampScene(id);for(var i=0;i<klcFlowItems.length;i++){if(parseInt(klcFlowItems[i].id||0,10)===id)return klcFlowItems[i];}return null;}"
    "function klcFlowResizeEditor(){var f=document.getElementById('flowEditorFrame');if(!f)return;try{var d=f.contentWindow.document;var h=Math.max(700,d.documentElement.scrollHeight,d.body.scrollHeight)+8;f.style.height=h+'px';}catch(e){}}function klcFlowEditorReady(){klcFlowResizeEditor();klcFlowSetText('flowStatus','Ablauf auswählen, kopieren oder direkt unten bearbeiten.');}"
    "function klcFlowLoadEditorNow(id,seq){id=klcFlowClampScene(id);if(seq&&seq!==klcFlowLoadSeq)return;var f=document.getElementById('flowEditorFrame');if(f){f.onload=klcFlowEditorReady;if(f.getAttribute('src')!==klcFlowSceneUrl(id))f.setAttribute('src',klcFlowSceneUrl(id));else klcFlowEditorReady();}}"
    "function klcFlowLoadEditor(id){id=klcFlowClampScene(id);var seq=++klcFlowLoadSeq;var f=document.getElementById('flowEditorFrame');var first=!(f&&f.getAttribute('src'));if(first&&window.fetch){klcFlowSetText('flowStatus','Editor wird geladen ...');fetch('/api/webui/free',{cache:'no-store'}).catch(function(){}).then(function(){klcFlowLoadEditorNow(id,seq);});}else{klcFlowLoadEditorNow(id,seq);}}"
    "function klcFlowScheduleEditor(id){if(klcFlowEditorTimer)clearTimeout(klcFlowEditorTimer);klcFlowEditorTimer=setTimeout(function(){klcFlowLoadEditor(id);},180);}"
    "function klcFlowLabel(item){if(!item)return 'Szene '+klcFlowPublicId(klcFlowActive);var d=parseInt(item.display_id||klcFlowPublicId(item.id),10)||klcFlowPublicId(item.id);return d+' '+(item.name||('Szene '+d))+(item.enabled?'':' - inaktiv')+((item.in_pool&&item.enabled)?' - Pool':'');}"
    // Der Controller nimmt nur angehakte UND aktivierte Szenen in den Pool
    // (klcScenesBuildPool). Genau dieselbe Bedingung hier, damit die Anzeige
    // nicht etwas verspricht, was KNX dann nicht durchschaltet.
    "function klcFlowPoolInfo(items){var p=(items||[]).filter(function(it){return it&&it.in_pool&&it.enabled;});"
    "var e=document.getElementById('flowPoolInfo');if(!e)return;"
    "if(!p.length){e.textContent='Szenen-Pool: leer. Setze im Ablauf-Editor das Häkchen \"Szenen-Pool\", damit KNX diese Szene mit dem 1-Bit-Objekt weiter (1) bzw. zurück (0) durchschalten kann.';return;}"
    "var ids=p.map(function(it){return parseInt(it.display_id||klcFlowPublicId(it.id),10)||klcFlowPublicId(it.id);});"
    "var first=ids[0],last=ids[ids.length-1];"
    "e.textContent='Szenen-Pool (KNX weiter=1 / zurück=0): '+ids.join(' → ')+' → wieder '+first+'. '"
    "+'Ist der Kanal aus oder auf einer Szene außerhalb des Pools, springt weiter auf '+first+' und zurück auf '+last+'.';}"
    "function klcFlowFillTargetSelect(){var t=document.getElementById('flowCopyTarget');if(!t)return;t.textContent='';klcFlowItems.forEach(function(it){var d=parseInt(it.display_id||klcFlowPublicId(it.id),10)||klcFlowPublicId(it.id);var o=document.createElement('option');o.value=String(it.id);o.textContent=d+' '+(it.name||('Szene '+d));t.appendChild(o);});if(klcFlowItems.length>1)t.value=String(klcFlowActive>=klcFlowItems.length?1:klcFlowActive+1);}"
    "function klcFlowUpdateTools(){var it=klcFlowItemById(klcFlowActive);var name=document.getElementById('flowToolName');if(name)name.value=it?(it.name||''):'';['flowToolNameId','flowToolCopySource','flowToolResetId'].forEach(function(id){var e=document.getElementById(id);if(e)e.value=String(klcFlowActive);});var t=document.getElementById('flowCopyTarget');if(t&&t.options.length){var max=parseInt(window.KLC_FLOW_COUNT||'50',10)||50;var want=String(klcFlowActive>=max?1:klcFlowActive+1);t.value=want;if(t.value!==want)t.selectedIndex=0;}}"
    "function klcFlowSetActive(id,loadNow){var max=parseInt(window.KLC_FLOW_COUNT||'1',10)||1;id=klcFlowClampScene(id);klcFlowActive=id;var pub=klcFlowPublicId(id),prev=(id<=1?max:id-1),next=(id>=max?1:id+1);var sel=document.getElementById('flowSceneSelect');if(sel)sel.value=String(pub);var a=document.getElementById('flowPrev');if(a)a.href='/flows?scene='+klcFlowPublicId(prev);var b=document.getElementById('flowNext');if(b)b.href='/flows?scene='+klcFlowPublicId(next);['flowPreview','flowPreviewTop','flowPreviewMain'].forEach(function(x){var p=document.getElementById(x);if(p)p.href='/preview?scene='+pub+'&t=0';});klcFlowUpdateTools();try{history.replaceState(null,'','/flows?scene='+pub);}catch(e){}if(loadNow)klcFlowLoadEditor(id);}"
    "function klcFlowRender(items,keepEditor){klcFlowItems=Array.isArray(items)?items:[];var sel=document.getElementById('flowSceneSelect');if(sel){sel.textContent='';klcFlowItems.forEach(function(it){var o=document.createElement('option');o.value=String(klcFlowPublicId(it.id));o.textContent=klcFlowLabel(it);sel.appendChild(o);});}klcFlowFillTargetSelect();klcFlowPoolInfo(klcFlowItems);klcFlowSetActive(klcFlowActive,false);klcFlowSetText('flowStatus','Ablauf auswählen, kopieren oder direkt unten bearbeiten.');if(!keepEditor)klcFlowScheduleEditor(klcFlowActive);}"
    "function klcFlowLoadSummary(keepEditor){fetch('/api/flows/summary',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){window.KLC_FLOW_COUNT=parseInt(j.count||window.KLC_FLOW_COUNT||1,10)||1;klcFlowRender(j.flows||[],!!keepEditor);}).catch(function(){klcFlowSetText('flowStatus','Ablaufübersicht konnte nicht geladen werden.');if(!keepEditor)klcFlowScheduleEditor(klcFlowActive);});}"
    "window.addEventListener('message',function(ev){var d=ev&&ev.data;if(d&&d.type==='klc-flow-saved')klcFlowLoadSummary(true);});"
    "document.addEventListener('DOMContentLoaded',function(){klcFlowActive=klcFlowClampScene(klcFlowActive);var sel=document.getElementById('flowSceneSelect');if(sel)sel.addEventListener('change',function(){klcFlowSetActive(parseInt(sel.value||'1',10)||1,true);});klcFlowSetActive(klcFlowActive,false);klcFlowLoadSummary();});";
}

const char* klcWebUiSceneFlowEditorJs()
{
  return R"KLCJS(
function klcFlowNotifyParentResize(){try{if(parent&&parent!==window&&parent.klcFlowResizeEditor)setTimeout(parent.klcFlowResizeEditor,20);}catch(e){}}
function klcScenePixelModeChanged(sel){var f=sel&&sel.form;if(!f)return;var abs=sel.value==='1';var rel=f.querySelector('.scene-lit-percent');var pix=f.querySelector('.scene-lit-pixels');if(rel)rel.style.display=abs?'none':'grid';if(pix)pix.style.display=abs?'':'none';klcFlowNotifyParentResize();}
function klcFlowNum(v,d){var n=parseInt(v,10);return isNaN(n)?d:n;}
function klcFlowCatalog(){return window.KLC_EFFECT_CATALOG||{effects:[],params:[]};}
function klcFlowEffectDescriptor(phase,v){var domain=phase==='main'?'main':'phase';return (klcFlowCatalog().effects||[]).find(function(e){return e.domain===domain&&Number(e.id)===Number(v);})||null;}
function klcFlowEffectFlag(phase,v,key){var e=klcFlowEffectDescriptor(phase,v);return !!(e&&e[key]);}
function klcFlowPopulateEffectSelect(sel,phase){if(!sel)return;var initial=klcFlowNum(sel.value,0),domain=phase==='main'?'main':'phase',all=(klcFlowCatalog().effects||[]).filter(function(e){if(e.domain!==domain)return false;if(phase==='start'&&!e.start_allowed)return false;if(phase==='end'&&!e.end_allowed)return false;return !e.ui_hidden;});var current=klcFlowEffectDescriptor(phase,initial);if(current&&!all.some(function(e){return Number(e.id)===initial;}))all.push(current);sel.innerHTML='';all.forEach(function(e){var o=document.createElement('option');o.value=String(e.id);o.textContent=window.klcT?window.klcT(e.label_key):e.label_key;sel.appendChild(o);});sel.value=String(initial);}
function klcFlowApplyParamConstraints(f){(klcFlowCatalog().params||[]).forEach(function(p){var names=p.phase_scoped?['start_'+p.key,'main_'+p.key,'end_'+p.key]:[p.key];names.forEach(function(nm){f.querySelectorAll("input[name='"+nm+"']").forEach(function(inp){if((inp.type||'').toLowerCase()!=='number')return;inp.min=String(p.min);inp.max=String(p.max);inp.step=String(p.step||1);});});});}
function klcFlowSetPixelPreset(f,percent,onlyLarge){if(!f)return;var mode=f.querySelector("select[name='pixel_mode']");var rels=f.querySelectorAll("input[name='start_fill_percent'],input[name='main_fill_percent'],input[name='end_fill_percent']");var main=f.querySelector("input[name='main_fill_percent']");var legacy=f.querySelector("input[name='lit_percent']");var current=main?klcFlowNum(main.value,0):0;var isAbs=mode&&mode.value==='1';if(onlyLarge&&!isAbs&&current<=50)return;if(mode){mode.value='0';klcScenePixelModeChanged(mode);}rels.forEach(function(rel){rel.value=String(percent);});if(legacy)legacy.value=String(percent);}
function klcFlowSetPixelPresetIfBelow(f,percent,threshold){if(!f)return;var mode=f.querySelector("select[name='pixel_mode']");var main=f.querySelector("input[name='main_fill_percent']");var current=main?klcFlowNum(main.value,0):0;var isAbs=mode&&mode.value==='1';if(!isAbs&&current>=threshold)return;klcFlowSetPixelPreset(f,percent,false);}
function klcFlowIsMainRun(v){return klcFlowEffectFlag('main',v,'run');}function klcFlowIsPhaseRun(v){return klcFlowEffectFlag('start',v,'run');}
function klcFlowIsMainSegment(v){return klcFlowEffectFlag('main',v,'segment');}function klcFlowIsPhaseSegment(v){return klcFlowEffectFlag('start',v,'segment');}
function klcFlowIsMainWaveBase(v){return klcFlowEffectFlag('main',v,'wave')&&!klcFlowEffectFlag('main',v,'tail');}function klcFlowIsMainWaveTail(v){return klcFlowEffectFlag('main',v,'wave')&&klcFlowEffectFlag('main',v,'tail');}function klcFlowIsMainWaveAny(v){return klcFlowEffectFlag('main',v,'wave');}
function klcFlowIsPhaseWaveBase(v){return klcFlowEffectFlag('start',v,'wave')&&!klcFlowEffectFlag('start',v,'tail');}function klcFlowIsPhaseWaveTail(v){return klcFlowEffectFlag('start',v,'wave')&&klcFlowEffectFlag('start',v,'tail');}function klcFlowIsPhaseWaveAny(v){return klcFlowEffectFlag('start',v,'wave');}
function klcFlowIsMainTetris(v){return klcFlowEffectFlag('main',v,'tetris');}function klcFlowIsPhaseTetris(v){return klcFlowEffectFlag('start',v,'tetris');}function klcFlowIsMainRandomFill(v){return klcFlowEffectFlag('main',v,'random_fill');}function klcFlowIsPhaseRandomFill(v){return klcFlowEffectFlag('start',v,'random_fill');}function klcFlowIsMainSparkle(v){return klcFlowEffectFlag('main',v,'sparkle');}function klcFlowIsPhaseSparkle(v){return klcFlowEffectFlag('start',v,'sparkle');}
function klcFlowIsMainPulse(v){return klcFlowEffectFlag('main',v,'pulse');}function klcFlowIsPhasePulse(v){return klcFlowEffectFlag('start',v,'pulse');}
function klcFlowIsMainMotion(v){return klcFlowEffectFlag('main',v,'motion');}function klcFlowIsPhaseMotion(v){return klcFlowEffectFlag('start',v,'motion');}
function klcFlowIsMainFillReverse(v){return klcFlowEffectFlag('main',v,'fill_reverse');}function klcFlowIsPhaseFillReverse(v){return klcFlowEffectFlag('start',v,'fill_reverse');}
function klcFlowReverseAvailable(phase,v){return phase==='main'?(klcFlowIsMainMotion(v)||klcFlowIsMainFillReverse(v)):(klcFlowIsPhaseMotion(v)||klcFlowIsPhaseFillReverse(v));}
function klcFlowIsMotionKind(phase,v){return phase==='main'?klcFlowIsMainMotion(v):klcFlowIsPhaseMotion(v);}
function klcFlowReverseLabel(phase,v){if(klcFlowIsMotionKind(phase,v))return 'Laufrichtung umdrehen';return 'Leuchtbereich umkehren';}
function klcFlowEffectName(phase){return phase==='start'?'start_effect':(phase==='main'?'main_effect':'end_effect');}
function klcFlowEffectValue(f,nm){var e=f?f.querySelector("[name='"+nm+"']"):null;return klcFlowNum(e&&e.value,0);}
function klcFlowTailChecked(f,nm){var cb=f?f.querySelector("input[name='"+nm+"']"):null;return !!(cb&&cb.checked&&cb.offsetParent!==null);}
function klcFlowTailChanged(cb){if(cb&&cb.form){var p=cb.name==='start_tail'?'start':(cb.name==='main_tail'?'main':'end');klcFlowSyncMotionHidden(cb.form,p);klcFlowUpdateDynamicFields(cb.form);}}
function klcFlowUpdateActiveState(f){if(!f)return;var cb=f.querySelector("input[name='enabled']");var active=!!(cb&&cb.checked);var card=f.closest('.flow-scene-card');if(card){card.classList.toggle('flow-scene-disabled',!active);var state=card.querySelector('.flow-scene-state');if(state)state.textContent=active?'aktiv':'inaktiv';}klcFlowNotifyParentResize();}
function klcFlowActiveChanged(cb){klcFlowUpdateActiveState(cb?cb.form:null);}
function klcFlowToggleTailBox(f,cls,isWave){var box=f?f.querySelector(cls):null;var cb=box?box.querySelector('input[type=checkbox]'):null;if(box)box.style.display=isWave?'':'none';if(cb&&!isWave)cb.checked=false;}
function klcFlowMotionKind(phase,v){var main=phase==='main';if(main?klcFlowIsMainRun(v):klcFlowIsPhaseRun(v))return 'run';if(main?klcFlowIsMainWaveAny(v):klcFlowIsPhaseWaveAny(v))return 'wave';return String(v);}
function klcFlowMotionDir(phase,v){var e=klcFlowEffectDescriptor(phase,v);return e&&e.direction?e.direction:'forward';}
function klcFlowMotionValue(phase,kind,dir,tail){if(kind!=='run'&&kind!=='wave')return klcFlowNum(kind,0);var domain=phase==='main'?'main':'phase';var e=(klcFlowCatalog().effects||[]).find(function(x){return x.domain===domain&&x.family===kind&&x.direction===dir&&!!x.tail===!!tail;});if(!e&&kind==='run')e=(klcFlowCatalog().effects||[]).find(function(x){return x.domain===domain&&x.family===kind&&x.direction===dir;});return e?Number(e.id):0;}
function klcFlowDirFromChecks(f,phase){var w=f?f.querySelector('.flow-'+phase+'-motion-dir'):null;if(!w)return 'forward';var rev=w.querySelector('input[data-dir=reverse]');var mir=w.querySelector('input[data-dir=mirror]');var r=!!(rev&&rev.checked),m=!!(mir&&mir.checked&&mir.offsetParent!==null);if(m&&r)return 'out_center';if(m)return 'center_out';if(r)return 'reverse';return 'forward';}
function klcFlowEnsureHidden(f,nm,val){var h=f?f.querySelector("input[type='hidden'][name='"+nm+"']"):null;if(!h&&f){h=document.createElement('input');h.type='hidden';h.name=nm;h.value=val||'0';f.insertBefore(h,f.firstChild);}return h;}
function klcFlowSetHiddenBool(f,nm,on){var h=klcFlowEnsureHidden(f,nm,'0');if(h)h.value=on?'1':'0';}
function klcFlowSyncMotionHidden(f,phase){if(!f)return;var sel=f.querySelector(phase==='start'?'.js-flow-start-effect':(phase==='main'?'.js-flow-main-effect':'.js-flow-end-effect'));var hidden=klcFlowEnsureHidden(f,klcFlowEffectName(phase),'0');if(!sel||!hidden)return;var w=f.querySelector('.flow-'+phase+'-motion-dir');var v=klcFlowEffectValue(f,klcFlowEffectName(phase));var available=klcFlowReverseAvailable(phase,v);var motion=klcFlowIsMotionKind(phase,v);var rev=w?w.querySelector('input[data-dir=reverse]'):null;var mir=w?w.querySelector('input[data-dir=mirror]'):null;var reverse=!!(available&&rev&&rev.checked);var mirror=!!(available&&motion&&mir&&mir.checked&&mir.offsetParent!==null);klcFlowSetHiddenBool(f,phase+'_reverse_direction',reverse);klcFlowSetHiddenBool(f,phase+'_mirror_center',mirror);var tailName=phase==='start'?'start_tail':(phase==='main'?'main_tail':'end_tail');hidden.value=String(klcFlowMotionValue(phase,sel.value,klcFlowDirFromChecks(f,phase),klcFlowTailChecked(f,tailName)));}
function klcFlowApplyPresetForEffect(f,phase,v){var e=klcFlowEffectDescriptor(phase,v);if(!e)return;if(e.family==='static'||e.pulse){klcFlowSetPixelPresetIfBelow(f,100,50);}else if(e.segment||e.tetris||e.random_fill||e.sparkle||e.fireworks||e.tetrix||e.tetris2){klcFlowSetPixelPreset(f,100,false);}else if(e.run||e.wave){klcFlowSetPixelPreset(f,10,true);}}
function klcFlowEffectChangedByUi(sel,phase){var f=sel&&sel.form;if(!f)return;klcFlowSyncMotionHidden(f,phase);var raw=(sel&&sel.value)||'0';var v=klcFlowNum(raw,0);if(raw==='run'||raw==='wave')v=klcFlowMotionValue(phase,raw,klcFlowDirFromChecks(f,phase),klcFlowTailChecked(f,phase==='start'?'start_tail':(phase==='main'?'main_tail':'end_tail')));klcFlowApplyPresetForEffect(f,phase,v);klcFlowSyncMotionHidden(f,phase);klcFlowUpdateDynamicFields(f);}
function klcFlowMainEffectChanged(sel){klcFlowEffectChangedByUi(sel,'main');}function klcFlowStartEffectChanged(sel){klcFlowEffectChangedByUi(sel,'start');}function klcFlowEndEffectChanged(sel){klcFlowEffectChangedByUi(sel,'end');}
function klcFlowUpdateReverseUi(f,phase){var w=f?f.querySelector('.flow-'+phase+'-motion-dir'):null;if(!w)return;var v=klcFlowEffectValue(f,klcFlowEffectName(phase));var available=klcFlowReverseAvailable(phase,v);var motion=klcFlowIsMotionKind(phase,v);w.style.display=available?'':'none';var lab=w.querySelector('[data-dir-label]');if(lab)lab.textContent=klcFlowReverseLabel(phase,v);var mirWrap=w.querySelector('[data-mirror-wrap]');var mir=w.querySelector('input[data-dir=mirror]');if(mirWrap)mirWrap.style.display=motion?'':'none';if(mir&&!motion)mir.checked=false;klcFlowSyncMotionHidden(f,phase);}
function klcFlowEnhanceMotionSelect(sel,phase){if(!sel||sel.dataset.enhanced==='1')return;var f=sel.form,nm=sel.name,initial=klcFlowNum(sel.value,0);var hidden=document.createElement('input');hidden.type='hidden';hidden.name=nm;hidden.value=String(initial);sel.parentNode.insertBefore(hidden,sel);sel.removeAttribute('name');sel.dataset.enhanced='1';var revHidden=klcFlowEnsureHidden(f,phase+'_reverse_direction','0'),mirHidden=klcFlowEnsureHidden(f,phase+'_mirror_center','0');var opts=Array.from(sel.options).map(function(o){return {v:o.value,t:o.text,hidden:o.hidden};});var kind=klcFlowMotionKind(phase,initial),dir=klcFlowMotionDir(phase,initial);sel.innerHTML='';var seenRun=false,seenWave=false;opts.forEach(function(o){var v=klcFlowNum(o.v,0);var k=klcFlowMotionKind(phase,v);if(k==='run'){if(seenRun)return;seenRun=true;o={v:'run',t:'Lauflicht'};}else if(k==='wave'){if(seenWave)return;seenWave=true;o={v:'wave',t:'Welle'};}else if(o.hidden){return;}var op=document.createElement('option');op.value=o.v;op.textContent=o.t;sel.appendChild(op);});sel.value=kind;var wrap=document.createElement('div');wrap.className='flow-motion-dir flow-'+phase+'-motion-dir';wrap.innerHTML='<b>Richtung / Bereich</b><div class="flow-motion-checks"><label title="Effekt läuft rückwärts: von Pixel X Richtung Pixel 1 statt umgekehrt."><input type="checkbox" data-dir="reverse"> <span data-dir-label>Laufrichtung umdrehen</span></label><label data-mirror-wrap title="Effekt läuft symmetrisch von der Mitte nach aussen; zusammen mit Laufrichtung umdrehen von aussen zur Mitte."><input type="checkbox" data-dir="mirror"> Mitte spiegeln</label></div>';sel.parentNode.insertAdjacentElement('afterend',wrap);var rev=wrap.querySelector('input[data-dir=reverse]'),mir=wrap.querySelector('input[data-dir=mirror]');if(rev)rev.checked=(dir==='reverse'||dir==='out_center'||revHidden.value==='1');if(mir)mir.checked=(dir==='center_out'||dir==='out_center'||mirHidden.value==='1');sel.onchange=function(){klcFlowEffectChangedByUi(sel,phase);};[rev,mir].forEach(function(cb){if(cb)cb.onchange=function(){klcFlowEffectChangedByUi(sel,phase);};});klcFlowUpdateReverseUi(f,phase);klcFlowSyncMotionHidden(f,phase);}
function klcFlowFieldNode(e){if(!e)return null;var checks=e.closest('.flow-motion-checks');if(checks&&checks.parentElement&&checks.parentElement.tagName==='LABEL')return checks.parentElement;return e.closest('label')||e.closest('.flow-field')||e;}
function klcFlowSetInputVisible(f,nm,on){if(!f)return;f.querySelectorAll("[name='"+nm+"']").forEach(function(e){if((e.type||'').toLowerCase()==='hidden')return;var n=klcFlowFieldNode(e);if(n)n.style.display=on?'':'none';});}
function klcFlowSetPhaseInputs(f,phase,on,names){names.forEach(function(s){klcFlowSetInputVisible(f,phase+'_'+s,on);});}
function klcFlowAny(a){for(var i=0;i<a.length;i++){if(a[i])return true;}return false;}
// Ergaenzt ausgewaehlte Zahlenfelder der Animationen um einen synchronen
// Schieberegler (gleiche Optik wie Feuerwerk/Tetrix). Das Zahlenfeld bleibt fuer
// die genaue Eingabe und das Absenden erhalten. Rein clientseitig, damit die
// positionsbasierten Server-Templates unveraendert bleiben.
function klcFlowSliderize(f){if(!f)return;f.querySelectorAll("input[type='number']").forEach(function(inp){if(inp.dataset.kslider||inp.offsetParent===null)return;var mn=parseFloat(inp.getAttribute('min'));if(isNaN(mn))mn=0;var mx=parseFloat(inp.getAttribute('max'));if(isNaN(mx))return;inp.dataset.kslider='1';inp.classList.add('klc-range-num');var rng=document.createElement('input');rng.type='range';rng.min=mn;rng.max=mx;rng.step=(mx-mn)>2000?Math.max(1,Math.round((mx-mn)/300)):Math.max(1,parseFloat(inp.step)||1);rng.value=(inp.value===''?mn:inp.value);rng.className='klc-range';rng.addEventListener('input',function(){inp.value=rng.value;inp.dispatchEvent(new Event('input',{bubbles:true}));});inp.addEventListener('input',function(){if(document.activeElement!==rng)rng.value=inp.value;});inp.parentNode.insertBefore(rng,inp);});}
function klcFlowUpdateDynamicFields(f){if(!f)return;var sv=klcFlowEffectValue(f,'start_effect'),mv=klcFlowEffectValue(f,'main_effect'),ev=klcFlowEffectValue(f,'end_effect');function state(p,v){var e=klcFlowEffectDescriptor(p,v)||{};return {v:v,e:e,wave:!!e.wave,motion:!!e.motion,pulse:!!e.pulse,segment:!!e.segment,tetris:!!e.tetris,sparkle:!!e.sparkle,active:!!e.active};}var phase={start:state('start',sv),main:state('main',mv),end:state('end',ev)};klcFlowToggleTailBox(f,'.flow-start-tail',phase.start.wave);klcFlowToggleTailBox(f,'.flow-main-tail',phase.main.wave);klcFlowToggleTailBox(f,'.flow-end-tail',phase.end.wave);['start','main','end'].forEach(function(p){klcFlowUpdateReverseUi(f,p);});var startDur=f.querySelector('.flow-start-duration');if(startDur)startDur.style.display=phase.start.e.duration?'':'none';var endDur=f.querySelector('.flow-end-duration');if(endDur)endDur.style.display=phase.end.e.duration?'':'none';var pulse=f.querySelector('.flow-pulse-period');if(pulse)pulse.style.display=klcFlowAny([phase.start.pulse,phase.main.pulse,phase.end.pulse])?'':'none';['start','main','end'].forEach(function(p){klcFlowSetInputVisible(f,p+'_pulse_period_ms',phase[p].pulse);});var step=f.querySelector('.flow-step-ms');if(step)step.style.display=klcFlowAny([phase.start.motion,phase.main.motion,phase.end.motion])?'':'none';['start','main','end'].forEach(function(p){klcFlowSetInputVisible(f,p+'_step_ms',phase[p].motion);});var fillBox=f.querySelector('.scene-lit-percent');if(fillBox){klcFlowSetInputVisible(f,'start_fill_percent',phase.start.active);klcFlowSetInputVisible(f,'main_fill_percent',phase.main.active);klcFlowSetInputVisible(f,'end_fill_percent',phase.end.active);}var segment=klcFlowAny([phase.start.segment,phase.main.segment,phase.end.segment]);var seg=f.querySelector('.flow-segment-param');if(seg)seg.style.display=segment?'':'none';['start','main','end'].forEach(function(p){klcFlowSetInputVisible(f,p+'_segment_percent',phase[p].segment);});var tetris=klcFlowAny([phase.start.tetris,phase.main.tetris,phase.end.tetris]);f.querySelectorAll('.flow-tetris-param').forEach(function(x){x.style.display=tetris?'':'none';});['start','main','end'].forEach(function(p){klcFlowSetPhaseInputs(f,p,phase[p].tetris,['tetris_group_min','tetris_group_max','tetris_gap','tetris_reverse_direction','tetris_mirror_center','tetris_random_direction','tetris_random_colors','tetris_teardown_mode']);});var sparkle=klcFlowAny([phase.start.sparkle,phase.main.sparkle,phase.end.sparkle]);f.querySelectorAll('.flow-sparkle-param').forEach(function(x){x.style.display=sparkle?'':'none';});['start','main','end'].forEach(function(p){klcFlowSetPhaseInputs(f,p,phase[p].sparkle,['sparkle_speed_ms','sparkle_fill_percent','sparkle_lifetime_ms']);});var tailOn={start:klcFlowTailChecked(f,'start_tail'),main:klcFlowTailChecked(f,'main_tail'),end:klcFlowTailChecked(f,'end_tail')};var tail=klcFlowAny([tailOn.start,tailOn.main,tailOn.end]);var tailBox=f.querySelector('.flow-tail-percent-param');if(tailBox)tailBox.style.display=tail?'':'none';['start','main','end'].forEach(function(p){klcFlowSetInputVisible(f,p+'_tail_percent',tailOn[p]);});var bounce=f.querySelector('.flow-wave-bounce-param');if(bounce)bounce.style.display=phase.main.wave?'':'none';var fw=f.querySelector('.flow-fw-param');if(fw)fw.style.display=klcFlowAny([phase.start.e.fireworks,phase.main.e.fireworks,phase.end.e.fireworks])?'':'none';var tx=f.querySelector('.flow-tx-param');if(tx)tx.style.display=klcFlowAny([phase.start.e.tetrix,phase.main.e.tetrix,phase.end.e.tetrix])?'':'none';klcFlowNotifyParentResize();}
function klcFlowSyncLegacySegment(f){var main=f?f.querySelector("input[name='main_segment_percent']"):null;var legacy=f?f.querySelector("input[name='segment_percent']"):null;if(main&&legacy)legacy.value=main.value;}
function klcFlowUpdateSegmentSoftMax(f){if(!f)return;var max=0;["start_segment_percent","main_segment_percent","end_segment_percent"].forEach(function(nm){var e=f.querySelector("input[name='"+nm+"']");var v=e?klcFlowNum(e.value,0):0;if(v>max)max=v;});var soft=f.querySelector("input[name='segment_soft_edge_pixels']");if(soft){soft.max=String(max);var cur=klcFlowNum(soft.value,0);if(cur>max)soft.value=String(max);}}
function klcFlowSyncLegacyFill(f){var main=f?f.querySelector("input[name='main_fill_percent']"):null;var legacy=f?f.querySelector("input[name='lit_percent']"):null;if(main&&legacy)legacy.value=main.value;}
function klcFlowSyncLegacyTail(f){var main=f?f.querySelector("input[name='main_tail_percent']"):null;var legacy=f?f.querySelector("input[name='tail_percent']"):null;if(main&&legacy)legacy.value=main.value;}
function klcFlowSyncLegacyStep(f){var main=f?f.querySelector("input[name='main_step_ms']"):null;var legacy=f?f.querySelector("input[name='speed_ms']"):null;if(main&&legacy)legacy.value=main.value;}
function klcFlowSyncLegacyPulse(f){var main=f?f.querySelector("input[name='main_pulse_period_ms']"):null;var legacy=f?f.querySelector("input[name='pulse_period_ms']"):null;if(main&&legacy)legacy.value=main.value;}
function klcFlowSyncBeforeSave(f){['start','main','end'].forEach(function(p){klcFlowSyncMotionHidden(f,p);});klcFlowSyncLegacyFill(f);klcFlowSyncLegacySegment(f);klcFlowUpdateSegmentSoftMax(f);klcFlowSyncLegacyTail(f);klcFlowSyncLegacyStep(f);klcFlowSyncLegacyPulse(f);}
function klcFlowSaveText(text){return window.klcT?window.klcT(text):text;}
function klcFlowSaveStatus(f,text,isError){var s=f.querySelector('.flow-save-status');if(!s){s=document.createElement('span');s.className='muted flow-save-status';var row=f.querySelector('.row:last-child');(row||f).appendChild(s);}s.textContent=text||'';s.style.color=isError?'#ff9c9c':'';}
function klcFlowPendingKey(scene){return 'klc-flow-save:'+String(scene);}
function klcFlowRememberSave(f,j){var id=f.querySelector("input[name='id']"),scene=id?Number(id.value):0,rev=j.operation_revision!=null?j.operation_revision:j.revision;if(!scene||!j.operation_id||rev==null)return;try{sessionStorage.setItem(klcFlowPendingKey(scene),JSON.stringify({operation_type:'scene',scene_id:scene,operation_id:String(j.operation_id),operation_revision:Number(rev),boot_generation:Number(j.boot_generation),server_applied_revision:Number(j.applied_revision)}));}catch(ignore){}}
function klcFlowForgetSave(f){var id=f.querySelector("input[name='id']"),scene=id?Number(id.value):0;if(!scene)return;try{sessionStorage.removeItem(klcFlowPendingKey(scene));}catch(ignore){}}
function klcFlowSetRevision(f,value){var revision=f.querySelector("input[name='revision']");if(revision&&value!=null)revision.value=String(value);}
function klcFlowWaitForSave(f,j){if(!j.queued||!j.operation_id)return Promise.resolve(j);var started=Date.now(),scene=Number(j.scene_id||((f.querySelector("input[name='id']")||{}).value)),ownRev=Number(j.operation_revision!=null?j.operation_revision:j.revision),boot=Number(j.boot_generation),baseline=Number(j.server_applied_revision);return new Promise(function(resolve,reject){function invalid(text){klcFlowForgetSave(f);if(Number.isFinite(baseline))klcFlowSetRevision(f,baseline);reject(new Error(text));}function poll(){fetch('/api/scene/storage-status?operation_id='+encodeURIComponent(String(j.operation_id)),{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json().then(function(s){if(!r.ok||!s.ok)throw new Error(s.error||('HTTP '+r.status));return s;});}).then(function(s){if(s.operation_type!=='scene'||String(s.operation_id)!==String(j.operation_id)||Number(s.scene_id)!==scene||Number(s.operation_revision)!==ownRev||Number(s.boot_generation)!==boot){invalid('Speicherstatus gehört zu einem anderen Vorgang; Szene wird neu geladen');return;}if(j.resume)klcFlowSetRevision(f,ownRev);if(s.state==='done'){klcFlowForgetSave(f);klcFlowSetRevision(f,s.operation_revision);j.saved=true;resolve(j);return;}if(s.state==='failed'){klcFlowForgetSave(f);if(s.failure_phase==='runtime_apply'){klcFlowSetRevision(f,s.applied_revision);reject(new Error((s.error||'Runtime-Übernahme fehlgeschlagen')+'; vorherige Szene wurde wiederhergestellt'));return;}if(s.failure_phase==='runtime_rollback'||s.rollback_failed){f.querySelectorAll('input,select,button,textarea').forEach(function(e){e.disabled=true;});reject(new Error('Kritischer Runtime-Rücknahmefehler; Seite neu laden oder Recovery verwenden'));return;}klcFlowSetRevision(f,s.operation_revision);reject(new Error((s.error||'Flash-Commit fehlgeschlagen')+'; Szene ist im RAM aktiv, erneutes Speichern ist möglich'));return;}if(s.state==='superseded'){klcFlowForgetSave(f);if(Number.isFinite(baseline))klcFlowSetRevision(f,baseline);reject(new Error('Speicherauftrag durch eine neuere Bearbeitung ersetzt; Szene neu laden'));return;}klcFlowSaveStatus(f,klcFlowSaveText('speichert …')+' '+s.state,false);if(Date.now()-started>30000){reject(new Error('Zeitüberschreitung beim dauerhaften Speichern'));return;}setTimeout(poll,80);}).catch(function(error){if(error&&String(error.message||'').indexOf('nicht mehr im Statusfenster')>=0){klcFlowForgetSave(f);if(Number.isFinite(baseline))klcFlowSetRevision(f,baseline);}reject(error);});}poll();});}
function klcFlowSaveAsync(e,f){e.preventDefault();e.stopImmediatePropagation();if(f.dataset.saving==='1')return;klcFlowSyncBeforeSave(f);var button=f.querySelector("button[type='submit']"),csrf=f.querySelector("input[name='csrf']"),data=new URLSearchParams();new FormData(f).forEach(function(value,key){data.append(key,String(value));});f.dataset.saving='1';if(button){button.dataset.saveLabel=button.textContent;button.disabled=true;button.textContent=klcFlowSaveText('speichert …');}klcFlowSaveStatus(f,klcFlowSaveText('speichert …'),false);fetch('/api/scene/config',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8','X-KLC-CSRF':csrf?csrf.value:''},body:data.toString()}).then(function(r){return r.text().then(function(text){var j=null;try{j=JSON.parse(text);}catch(ignore){}if(!r.ok||!j||j.ok!==true)throw new Error((j&&(j.error||j.message))||text||('HTTP '+r.status));return j;});}).then(function(j){j.scene_id=Number((f.querySelector("input[name='id']")||{}).value);j.server_applied_revision=Number(j.applied_revision);klcFlowSetRevision(f,j.operation_revision!=null?j.operation_revision:j.revision);klcFlowRememberSave(f,j);return klcFlowWaitForSave(f,j);}).then(function(j){klcFlowSaveStatus(f,klcFlowSaveText('gespeichert'),false);if(button)button.textContent=klcFlowSaveText('gespeichert');try{if(parent&&parent!==window)parent.postMessage({type:'klc-flow-saved'},'*');}catch(ignore){}setTimeout(function(){f.dataset.saving='';if(button){button.disabled=false;button.textContent=button.dataset.saveLabel||'Ablauf speichern';}},900);}).catch(function(error){if(error&&error.name==='TypeError'){klcFlowSaveStatus(f,'Schneller Speicherweg nicht erreichbar – klassischer Speicherweg wird verwendet …',false);f.dataset.saving='';if(button){button.disabled=false;button.textContent=button.dataset.saveLabel||'Ablauf speichern';}setTimeout(function(){HTMLFormElement.prototype.submit.call(f);},100);return;}f.dataset.saving='';if(button){button.disabled=false;button.textContent=button.dataset.saveLabel||'Ablauf speichern';}klcFlowSaveStatus(f,klcFlowSaveText('nicht gespeichert')+': '+(error.message||''),true);klcFlowNotifyParentResize();});}
function klcFlowResumeSave(f){var id=f.querySelector("input[name='id']"),scene=id?Number(id.value):0,raw=null,serverRevision=Number((f.querySelector("input[name='revision']")||{}).value);if(!scene)return;try{raw=sessionStorage.getItem(klcFlowPendingKey(scene));}catch(ignore){}if(!raw)return;var p=null;try{p=JSON.parse(raw);}catch(ignore){}if(!p||p.operation_type!=='scene'||p.scene_id!==scene||typeof p.operation_id!=='string'||!/^[1-9][0-9]*$/.test(p.operation_id)||!Number.isInteger(p.operation_revision)||!Number.isInteger(p.boot_generation)){klcFlowForgetSave(f);klcFlowSetRevision(f,serverRevision);return;}klcFlowSaveStatus(f,klcFlowSaveText('Speicherstatus wird wieder aufgenommen …'),false);klcFlowWaitForSave(f,{queued:true,resume:true,scene_id:scene,operation_id:p.operation_id,operation_revision:p.operation_revision,revision:p.operation_revision,boot_generation:p.boot_generation,server_applied_revision:serverRevision}).then(function(){klcFlowSaveStatus(f,klcFlowSaveText('gespeichert'),false);}).catch(function(error){klcFlowSaveStatus(f,klcFlowSaveText('nicht gespeichert')+': '+(error.message||''),true);});}
document.addEventListener('submit',function(e){var f=e.target;if(f&&f.classList&&f.classList.contains('flow-scene-form')&&window.fetch)klcFlowSaveAsync(e,f);},true);
document.addEventListener('DOMContentLoaded',function(){document.querySelectorAll('select.js-scene-pixel-mode').forEach(function(sel){klcScenePixelModeChanged(sel);});document.querySelectorAll('form').forEach(function(f){if(f.querySelector('.js-flow-main-effect')){f.addEventListener('submit',function(){['start','main','end'].forEach(function(p){klcFlowSyncMotionHidden(f,p);});klcFlowSyncLegacyFill(f);klcFlowSyncLegacySegment(f);klcFlowUpdateSegmentSoftMax(f);klcFlowSyncLegacyTail(f);klcFlowSyncLegacyStep(f);klcFlowSyncLegacyPulse(f);});['start_fill_percent','main_fill_percent','end_fill_percent'].forEach(function(nm){var e=f.querySelector("input[name='"+nm+"']");if(e)e.addEventListener('input',function(){klcFlowSyncLegacyFill(f);});});['start_segment_percent','main_segment_percent','end_segment_percent'].forEach(function(nm){var e=f.querySelector("input[name='"+nm+"']");if(e)e.addEventListener('input',function(){klcFlowSyncLegacySegment(f);klcFlowUpdateSegmentSoftMax(f);});});var se=f.querySelector("input[name='segment_soft_edge_pixels']");if(se)se.addEventListener('input',function(){klcFlowUpdateSegmentSoftMax(f);});['start_tail_percent','main_tail_percent','end_tail_percent'].forEach(function(nm){var e=f.querySelector("input[name='"+nm+"']");if(e)e.addEventListener('input',function(){klcFlowSyncLegacyTail(f);});});['start_step_ms','main_step_ms','end_step_ms'].forEach(function(nm){var e=f.querySelector("input[name='"+nm+"']");if(e)e.addEventListener('input',function(){klcFlowSyncLegacyStep(f);});});['start_pulse_period_ms','main_pulse_period_ms','end_pulse_period_ms'].forEach(function(nm){var e=f.querySelector("input[name='"+nm+"']");if(e)e.addEventListener('input',function(){klcFlowSyncLegacyPulse(f);});});klcFlowApplyParamConstraints(f);klcFlowPopulateEffectSelect(f.querySelector('.js-flow-start-effect'),'start');klcFlowPopulateEffectSelect(f.querySelector('.js-flow-main-effect'),'main');klcFlowPopulateEffectSelect(f.querySelector('.js-flow-end-effect'),'end');klcFlowEnhanceMotionSelect(f.querySelector('.js-flow-start-effect'),'start');klcFlowEnhanceMotionSelect(f.querySelector('.js-flow-main-effect'),'main');klcFlowEnhanceMotionSelect(f.querySelector('.js-flow-end-effect'),'end');klcFlowUpdateSegmentSoftMax(f);klcFlowSliderize(f);klcFlowUpdateDynamicFields(f);klcFlowUpdateActiveState(f);klcFlowResumeSave(f);}});try{if(location.search.indexOf('saved=1')>=0&&parent&&parent!==window)parent.postMessage({type:'klc-flow-saved'},'*');}catch(e){} });
)KLCJS";
}
