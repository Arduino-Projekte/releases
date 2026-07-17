from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "klc_main - Codex"


def newer(candidate: int, reference: int) -> bool:
    delta = (candidate - reference) & 0xFFFFFFFF
    return candidate != reference and delta < 0x80000000


def winner(a, b):
    if b and (not a or newer(b[0], a[0])):
        return b
    return a


def target_wins(a, b, target):
    if not a and not b:
        return False
    if a and b and a[0] == b[0] and a[1] != b[1]:
        return False
    return winner(a, b)[1] == target


def migrate(a, b, target):
    if target_wins(a, b, target):
        return a, b
    current = winner(a, b)
    next_seq = 1 if current is None else (current[0] + 1) & 0xFFFFFFFF
    if current is b:
        a = (next_seq, target)
    elif current is a:
        b = (next_seq, target)
    else:
        a = (next_seq, target)
    assert target_wins(a, b, target)
    return a, b


def test_migration_makes_target_normal_boot_winner():
    cases = [
        ((5, "X"), (6, "Y")),
        ((6, "Y"), (5, "X")),
        ((6, "Y"), (6, "X")),
        ((6, "X"), (6, "Y")),
        ((5, "X"), None),
        (None, None),
        ((6, "X"), (5, "Y")),
    ]
    for a, b in cases:
        a, b = migrate(a, b, "X")
        assert target_wins(a, b, "X")
        # Ein zweiter Neustart trifft dieselbe normale Auswahl.
        assert winner(a, b)[1] == "X"


def test_sequence_wrap_is_newer_and_persists_target():
    a, b = migrate((0xFFFFFFFF, "Y"), (0xFFFFFFFE, "Z"), "X")
    assert winner(a, b) == (0, "X")
    assert newer(0, 0xFFFFFFFF)


def test_source_has_per_operation_revision_and_reload_retry():
    store = (FW / "klc_scene_store.cpp").read_text(encoding="utf-8")
    header = (FW / "klc_scene_store.h").read_text(encoding="utf-8")
    ui = (FW / "klc_web_ui_flow_editor_pages.cpp").read_text(encoding="utf-8")
    assert "operation_revision=job.edit_revision" in store
    assert "current_scene_revision" in header
    assert "storage_sequence" in header
    assert "sessionStorage.setItem" in ui
    assert "operation_revision:j.operation_revision" not in ui  # robuste Fallback-Auswahl
    assert "klcFlowSetRevision(f,ownRev)" in ui
    assert "erneutes Speichern ist möglich" in ui
    assert "retry_same_ram" in store


def test_runtime_apply_is_not_in_http_handlers():
    scene_http = (FW / "klc_web_server_scene_config.cpp").read_text(encoding="utf-8")
    flow_tools = (FW / "klc_web_server_flow_tools.cpp").read_text(encoding="utf-8")
    store = (FW / "klc_scene_store.cpp").read_text(encoding="utf-8")
    assert "klcScenesApplyConfigUpdate" not in scene_http
    assert "klcScenesApplyConfigUpdate" not in flow_tools
    assert "case KLC_SCENE_STORE_RUNTIME_APPLY" in store
    assert "Ruecknahme fehlgeschlagen" in store


def test_token_lease_and_new_writers_are_coordinated():
    coordinator = (FW / "klc_storage_coordinator.cpp").read_text(encoding="utf-8")
    header = (FW / "klc_storage_coordinator.h").read_text(encoding="utf-8")
    help_cpp = (FW / "klc_help.cpp").read_text(encoding="utf-8")
    setup = (FW / "klc_setup_wizard_backup.cpp").read_text(encoding="utf-8")
    assert "g_writer_token != token" in coordinator
    assert "KLC_STORAGE_WRITER_HELP" in header
    assert "KLC_STORAGE_WRITER_SETUP_BACKUP" in header
    assert "KlcStorageWriterLease writer(KLC_STORAGE_WRITER_HELP)" in help_cpp
    assert "g_setup_backup_writer_token" in setup


def test_diagnostic_and_import_status_routes_exist():
    routes = (FW / "klc_web_server_routes.cpp").read_text(encoding="utf-8")
    store = (FW / "klc_scene_store.cpp").read_text(encoding="utf-8")
    assert "/api/scene/storage-scenes" in routes
    assert "/api/scene/replacement-status" in routes
    assert "g_last_status.recovery_count=g_recovery_count" in store
    assert "KLC_SCENE_TXN_COMPLETE" in store
    assert "last_confirmed_scene" in (FW / "klc_scene_store.h").read_text(encoding="utf-8")
