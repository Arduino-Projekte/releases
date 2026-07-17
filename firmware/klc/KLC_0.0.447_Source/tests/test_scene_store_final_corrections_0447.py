from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "klc_main - Codex"


def source(name: str) -> str:
    return (FW / name).read_text(encoding="utf-8")


def test_replacement_waits_for_backend_activation_and_rebinds_fallback():
    main = source("klc_main.ino")
    store = source("klc_scene_store.cpp")
    assert "KLC_SCENE_TXN_AWAIT_ACTIVATION" in store
    assert "klcSceneStoreWriteMarker(digest)" in store
    assert main.index("klcLedEngineBegin(g_config)") < main.index(
        "klcSceneStoreFinalizeActivatedConfig"
    )
    assert "active_from_lkg?\"lkg\"" in main
    assert "active_from_previous?\"previous\"" in main
    assert "Config-/KLS-Generation abgeglichen" in store
    assert "ab_authority.config_generation!=loaded_digest" in store


def test_generation_model_excludes_network_but_binds_led_and_scenes():
    store = source("klc_scene_store.cpp")
    digest = store[store.index("bool klcSceneStoreConfigDigest"):]
    digest = digest[:digest.index("void klcSceneStoreBuildHeader")]
    assert "cfg.outputs" in digest
    assert "cfg.power" in digest
    assert "cfg.scenes[id]" in digest
    assert "cfg.network" not in digest
    assert "for(uint8_t id=1U" in digest  # Szene 0 bleibt ausgeschlossen.


def test_accepted_applied_and_persisted_revisions_are_separate():
    store = source("klc_scene_store.cpp")
    header = source("klc_scene_store.h")
    enqueue = store[store.index("bool klcSceneStoreEnqueue"):]
    enqueue = enqueue[:enqueue.index("void klcSceneStoreTick")]
    tick = store[store.index("void klcSceneStoreTick"):]
    assert "accepted_revision" in header
    assert "applied_revision" in header
    assert "persisted_revision" in header
    assert "g_accepted_revision[scene_id]=revision" in enqueue
    assert "g_applied_revision[scene_id]" not in enqueue.split(
        "g_accepted_revision[scene_id]=revision"
    )[1]
    assert "g_applied_revision[g_active.scene_id]=g_active.edit_revision" in tick
    assert "g_persisted_revision[g_active.scene_id]=g_active.edit_revision" in tick


def test_operation_ids_are_reboot_unique_and_json_strings():
    store = source("klc_scene_store.cpp")
    api = source("klc_web_server_scene_config.cpp")
    flow = source("klc_web_ui_flow_editor_pages.cpp")
    assert "((uint64_t)g_boot_generation<<32)|counter" in store
    assert "KLC_SCENE_BOOT_GENERATION_PATH" in store
    assert '\\\"operation_id\\\":\\\"%llu\\\"' in api
    assert "operation_type:'scene'" in flow
    assert "s.operation_type!=='scene'" in flow
    assert "Number(s.boot_generation)!==boot" in flow
    assert "Number(s.scene_id)!==scene" in flow
    assert "Number(s.operation_revision)!==ownRev" in flow


def test_runtime_failures_and_slot_refresh_have_distinct_outcomes():
    store = source("klc_scene_store.cpp")
    flow = source("klc_web_ui_flow_editor_pages.cpp")
    assert "KLC_SCENE_FAILURE_RUNTIME_APPLY" in store
    assert "KLC_SCENE_FAILURE_RUNTIME_ROLLBACK" in store
    assert "KLC_SCENE_FAILURE_STORAGE_VERIFY" in store
    assert "klcSceneStoreRefreshSlotStatus(g_active.scene_id)" in store
    assert "klcFlowSetRevision(f,s.applied_revision)" in flow
    assert "klcFlowSetRevision(f,s.operation_revision)" in flow
    assert "e.disabled=true" in flow


def test_lkg_and_all_replacement_clients_are_coordinated_and_traceable():
    storage = source("klc_storage_serialization_api.cpp")
    normal_import = source("klc_web_server_knx_config.cpp")
    setup = source("klc_setup_wizard_backup.cpp")
    reset = source("klc_reset.cpp")
    assert "KlcStorageWriterLease writer(KLC_STORAGE_WRITER_CONFIG" in storage
    assert normal_import.count("replacement_operation_id") >= 3
    assert "scene_replacement_operation_id" in setup
    assert "klcSceneStoreReplacementOperationId" in reset
