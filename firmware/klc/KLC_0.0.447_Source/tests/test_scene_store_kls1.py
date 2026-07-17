"""Architektur- und Referenztests fuer den persistenten KLS1-Szenenspeicher."""

from __future__ import annotations

import binascii
import re
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "klc_main - Codex"


def source(name: str) -> str:
    return (FW / name).read_text(encoding="utf-8").replace("\r\n", "\n")


def sequence_newer(candidate: int, reference: int) -> bool:
    difference = (candidate - reference) & 0xFFFFFFFF
    signed = difference if difference < 0x80000000 else difference - 0x100000000
    return candidate != reference and signed > 0


def build_reference_header(scene_id: int, slot: int, sequence: int, payload: bytes) -> bytes:
    prefix = struct.pack(
        "<4sHBBIHHI", b"KLS1", 1, scene_id, slot, sequence,
        len(payload), 0, binascii.crc32(payload) & 0xFFFFFFFF,
    )
    return prefix + struct.pack("<I", binascii.crc32(prefix) & 0xFFFFFFFF)


class SceneStoreKls1Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.store = source("klc_scene_store.cpp")
        cls.header = source("klc_scene_store.h")
        cls.scene_handler = source("klc_web_server_scene_config.cpp")
        cls.flow_tools = source("klc_web_server_flow_tools.cpp")
        cls.routes = source("klc_web_server_routes.cpp")
        cls.ui = source("klc_web_ui_flow_editor_pages.cpp")
        cls.main = source("klc_main.ino")
        cls.ota = source("klc_ota_core_helpers.cpp")

    def test_crc32_and_header_reference_vectors(self) -> None:
        self.assertEqual(0xCBF43926, binascii.crc32(b"123456789") & 0xFFFFFFFF)
        header = build_reference_header(1, 0, 0x12345678, b"scene")
        self.assertEqual(24, len(header))
        self.assertEqual(b"KLS1", header[:4])
        self.assertEqual(1, struct.unpack_from("<H", header, 4)[0])
        self.assertEqual(binascii.crc32(header[:20]) & 0xFFFFFFFF,
                         struct.unpack_from("<I", header, 20)[0])
        self.assertIn("0xEDB88320UL", self.store)
        self.assertIn("klcSceneStoreCrc32(header,20U)", self.store)

    def test_sequence_comparison_handles_wraparound(self) -> None:
        self.assertTrue(sequence_newer(2, 1))
        self.assertFalse(sequence_newer(1, 2))
        self.assertTrue(sequence_newer(0, 0xFFFFFFFF))
        self.assertFalse(sequence_newer(0xFFFFFFFF, 0))
        self.assertFalse(sequence_newer(7, 7))
        self.assertIn("(int32_t)(candidate-reference)>0", self.store)

    def test_codec_is_explicit_and_encode_decode_fields_match(self) -> None:
        self.assertNotRegex(self.store, r"write\s*\([^\n]*sizeof\s*\(\s*KlcSceneConfig")
        encoded = re.findall(r"P(?:8|16|32)\((\w+)\)", self.store)
        decoded = re.findall(r"G(?:8|16|32)\((\w+)\)", self.store)
        # Die drei Makrodefinitionen selbst ergeben je einen Platzhalter.
        encoded = [field for field in encoded if field != "field"]
        decoded = [field for field in decoded if field != "field"]
        self.assertGreater(len(encoded), 100)
        self.assertEqual(encoded, decoded)
        self.assertIn("r.ok&&r.used==r.length", self.store)

    def test_scene_handlers_do_not_call_full_config_commit(self) -> None:
        for text in (self.scene_handler, self.flow_tools):
            self.assertNotIn("klcStorageSaveConfig", text)
            self.assertNotIn("klcConfigValidateDetailed", text)
        self.assertNotIn("KlcConfigWorkspaceLease", self.flow_tools)
        self.assertIn("klcConfigValidateSceneDetailed", self.scene_handler)
        self.assertIn("klcConfigValidateSceneDetailed", self.flow_tools)

    def test_ab_write_verification_and_isolated_recovery_exist(self) -> None:
        self.assertIn("1U-g_persisted_slot", self.store)
        self.assertIn("KLC_SCENE_STORE_VERIFY_HEADER", self.store)
        self.assertIn("KLC_SCENE_STORE_VERIFY_PAYLOAD", self.store)
        self.assertIn("if(!va&&!vb)", self.store)
        self.assertIn("klcConfigLoadSceneDefault(cfg.scenes[id],id)", self.store)
        self.assertIn("klcSceneStoreSequenceNewer(b.sequence,a.sequence)", self.store)

    def test_commit_is_cooperative_and_bounded(self) -> None:
        self.assertIn("KLC_SCENE_STORE_CHUNK = 64U", self.store)
        self.assertIn("klcSceneStoreTick();", self.main)
        self.assertNotIn("delay(", self.store)
        states = ["PREPARE", "OPEN", "WRITE_HEADER", "WRITE_PAYLOAD",
                  "FLUSH", "CLOSE", "VERIFY_OPEN", "VERIFY_HEADER",
                  "VERIFY_PAYLOAD", "COMMIT", "DONE", "FAILED"]
        for state in states:
            self.assertIn(f"KLC_SCENE_STORE_{state}", self.header)

    def test_revision_queue_status_and_webui_confirmation(self) -> None:
        self.assertIn("expected_revision!=g_applied_revision[scene_id]", self.store)
        self.assertIn("g_queue[i].scene_id==scene_id", self.store)
        self.assertIn("KLC_SCENE_STORE_QUEUE_CAPACITY = 2U", self.header)
        self.assertIn('/api/scene/storage-status', self.routes)
        self.assertIn("klcFlowWaitForSave", self.ui)
        self.assertIn("s.state==='done'", self.ui)
        self.assertIn("s.state==='failed'", self.ui)
        self.assertIn("operation_id", self.scene_handler)

    def test_migration_and_storage_coordination_are_explicit(self) -> None:
        self.assertIn("klcSceneStoreWriteSlotSync", self.store)
        self.assertIn("klcSceneStoreWriteMarker", self.store)
        self.assertLess(self.store.index("klcSceneStoreWriteSlotSync(id,0U,1U"),
                        self.store.index("if(!klcSceneStoreWriteMarker(digest))"))
        self.assertIn("klcSceneStoreIsBusy()", self.ota)
        self.assertIn("klcOtaIsUpdateRunning()", self.store)
        self.assertIn("klcSceneStoreRestoreAuthorityMarker", self.store)


if __name__ == "__main__":
    unittest.main()
