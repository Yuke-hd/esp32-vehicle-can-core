"""Package layout and reproducibility tests against actual canonical sources."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location("stage_arduino", Path(__file__).resolve().parents[2] / "tools/stage_arduino.py")
staging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(staging)


class ArduinoPackageTests(unittest.TestCase):
    def test_reproducible_archive_and_source_boundary(self):
        with tempfile.TemporaryDirectory() as temporary:
            first = staging.stage(Path(temporary) / "one", "HEAD")
            second = staging.stage(Path(temporary) / "two", first["source_revision"])
            self.assertEqual(first["package_sha256"], second["package_sha256"])
            with zipfile.ZipFile(first["archive"]) as archive:
                prefix = staging.LIBRARY + "/"
                paths = [name.removeprefix(prefix) for name in archive.namelist()]
                compiled = sorted(name for name in paths if name.endswith(".cpp"))
                self.assertEqual(compiled, ["src/can_bus/can_bus.cpp", "src/vehicle_core/vehicle_core.cpp",
                                            "src/vehicle_telemetry/can_bus_source.cpp", "src/vehicle_telemetry/runtime.cpp"])
                for internal in ("src/can_bus/frame_ring.hpp", "src/can_bus/lifecycle.hpp",
                                 "src/can_bus/configuration.hpp", "src/vehicle_telemetry/runtime_budget.hpp"):
                    self.assertIn(internal, paths)
                self.assertIn("src/vehicle_core/notification_channel.hpp", paths)
                self.assertNotIn("include/", paths)
                provenance = json.loads(archive.read(prefix + "PROVENANCE.json"))
                self.assertEqual(provenance["source_revision"], first["source_revision"])
                self.assertFalse(provenance["dependencies"]["third_party_sources_bundled"])
                for entry in provenance["files"]:
                    self.assertEqual(archive.read(prefix + entry["packaged"]),
                                     staging.git("show", f'{first["source_revision"]}:{entry["source"]}'))

    def test_existing_output_is_rejected_without_overwriting(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            sentinel = output / "keep"
            sentinel.write_text("existing data")
            with self.assertRaises(ValueError):
                staging.stage(output, "HEAD")
            self.assertEqual(sentinel.read_text(), "existing data")


if __name__ == "__main__":
    unittest.main()
