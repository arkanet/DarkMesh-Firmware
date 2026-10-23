"""Source-level regression guards for the experimental CLIENT_HIDDEN Ghost bridge.

These tests cannot replace a compiled firmware build and three-node RF verification.
Run: python3 -m unittest discover -s tests/ghost -p 'test_*.py' -v
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
ROUTING = (ROOT / "src/modules/RoutingModule.cpp").read_text()
SERVICE = (ROOT / "src/mesh/MeshService.cpp").read_text()


class GhostSourceRegression(unittest.TestCase):
    def test_enabled_only_for_client_hidden(self):
        self.assertIn(
            "config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN",
            ROUTING,
        )
        self.assertNotIn("config.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN", ROUTING)

    def test_ghost_delivers_before_local_only_drop(self):
        ghost = ROUTING.index("if (ghostCapture && mp.from != 0)")
        mirror = ROUTING.index("service->handleFromRadio(&mp);", ghost)
        filter_begin = ROUTING.index("// Beginning of logic whether to drop")
        self.assertLess(ghost, mirror)
        self.assertLess(mirror, filter_begin)

    def test_single_delivery_other_roles_unchanged(self):
        self.assertRegex(
            ROUTING,
            r"if \\(!ghostCapture && \\(isBroadcast\\(mp\\.to\\) \\|\\| isToUs\\(&mp\\)\\) && \\(mp\\.from != 0\\)\\)",
        )
        self.assertEqual(ROUTING.count("service->handleFromRadio(&mp);"), 2)

    def test_no_radio_rebroadcast_changes(self):
        self.assertIn("router->sniffReceived(&mp, r);", ROUTING)
        self.assertIn("if (!maybePKI)", ROUTING)
        self.assertIn("return false;", ROUTING)

    def test_encrypted_packet_does_not_access_decoded_union_for_queue_policy(self):
        self.assertIn(
            "const bool isDecoded = p->which_payload_variant == meshtastic_MeshPacket_decoded_tag;",
            SERVICE,
        )
        self.assertIn("if (isDecoded && moduleConfig.store_forward.enabled", SERVICE)
        self.assertIn("if (isDecoded && (p->decoded.portnum ==", SERVICE)

    def test_channel_independent_no_secret_or_magic_hash(self):
        self.assertNotIn("0xBF", ROUTING)
        self.assertNotIn("CUSTOM", ROUTING)


if __name__ == "__main__":
    unittest.main()
