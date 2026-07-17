"""Regressionstests fuer die KLS1-Korrekturen ab Firmware 0.0.445."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "klc_main - Codex"


def src(name: str) -> str:
    return (FW / name).read_text(encoding="utf-8").replace("\r\n", "\n")


def test_failed_commit_keeps_edit_revision_and_dirty_state() -> None:
    store = src("klc_scene_store.cpp")
    fail = store[store.index("void klcSceneStoreFail"):
                 store.index("void klcSceneStoreStartNext")]
    assert "g_scene_dirty[g_active.scene_id]=true" in fail
    assert "g_ram_revision[g_active.scene_id]=" not in fail
    assert "g_persisted_crc" in store and "g_persisted_length" in store


def test_authority_and_replacement_are_ab_transactional() -> None:
    store = src("klc_scene_store.cpp")
    assert "klc_scenes_auth_a.kla" in store
    assert "klc_scenes_auth_b.kla" in store
    assert "klc_scenes_txn_a.klt" in store
    assert "klc_scenes_txn_b.klt" in store
    marker = store[store.index("bool klcSceneStoreWriteMarker"):
                   store.index("uint8_t klcSceneStoreQueueDepth")]
    assert "remove(KLC_SCENE_STORE_MARKER_PATH)" not in marker
    begin = store[store.index("bool klcSceneStoreBegin"):
                  store.index("bool klcSceneStoreEnqueue")]
    assert "transaction_matches" in begin
    assert "writes_needed" in begin
    assert "old_path" not in begin


def test_semantic_validation_and_equal_sequence_diagnostic() -> None:
    store = src("klc_scene_store.cpp")
    assert "klcConfigValidateSceneDetailed(*scene,scene_id" in store
    assert "a.sequence==b.sequence" in store
    assert "gleiche Sequenz mit unterschiedlichem Payload" in store


def test_runtime_apply_runs_at_loop_boundary_and_rolls_back() -> None:
    for name in ("klc_web_server_scene_config.cpp",
                 "klc_web_server_flow_tools.cpp"):
        text = src(name)
        assert "klcSceneStoreEnqueue" in text
        assert "klcScenesApplyConfigUpdate" not in text
    store = src("klc_scene_store.cpp")
    apply_pos = store.index("case KLC_SCENE_STORE_RUNTIME_APPLY")
    persist_pos = store.index("case KLC_SCENE_STORE_PREPARE", apply_pos)
    assert apply_pos < persist_pos
    assert "previous_scene" in store
    assert "rollback_ok" in store


def test_coalescing_supersedes_with_new_operation_id() -> None:
    store = src("klc_scene_store.cpp")
    assert "KLC_SCENE_STORE_SUPERSEDED" in store
    assert "target->operation_id=klcSceneStoreNextOperationId()" in store
    assert "existing_operation" not in store
    assert "KLC_SCENE_STATUS_HISTORY_SIZE = 32U" in store


def test_backup_and_classic_paths_have_durable_status() -> None:
    config = src("klc_web_server_knx_config.cpp")
    flow = src("klc_web_server_flow_tools.cpp")
    routes = src("klc_web_server_routes.cpp")
    ui = src("klc_web_ui_flow_editor_pages.cpp")
    assert "klcSceneStoreHasDirtyScenes()" in config
    assert "Backup gesperrt" in config
    assert "/flow/save-status" in flow and "/flow/save-status" in routes
    assert "s.state==='superseded'" in ui


def test_storage_writer_coordinator_covers_long_lived_writers() -> None:
    store = src("klc_scene_store.cpp")
    save = src("klc_storage_save_commit.cpp")
    backup = src("klc_web_server_knx_config.cpp")
    language = src("klc_web_server_update_upload.cpp")
    assert "KLC_STORAGE_WRITER_SCENE" in store
    assert "KLC_STORAGE_WRITER_CONFIG" in save
    assert "KLC_STORAGE_WRITER_IMPORT" in backup
    assert "KLC_STORAGE_WRITER_LANGUAGE" in language


def test_terminal_steps_are_included_in_max_step_measurement() -> None:
    store = src("klc_scene_store.cpp")
    fail = store[store.index("void klcSceneStoreFail"):
                 store.index("void klcSceneStoreStartNext")]
    commit = store[store.index("case KLC_SCENE_STORE_COMMIT"):
                   store.index("default:break", store.index("case KLC_SCENE_STORE_COMMIT"))]
    assert "micros()-g_current_step_started_us" in fail
    assert "micros()-step_started" in commit
