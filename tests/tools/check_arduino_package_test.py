"""Regression tests for installed-package diagnostics and source isolation."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_arduino_package as checker

PROFILE = json.loads((Path(__file__).resolve().parents[2] / "tools/arduino/profile.json").read_text())


class ArduinoPackageCheckerTests(unittest.TestCase):
    def test_missing_cli_reports_install_command(self):
        with patch.object(checker, "run", side_effect=FileNotFoundError("arduino-cli")):
            with self.assertRaisesRegex(checker.CheckFailure, "Install Arduino CLI 1.3.1"):
                checker.check_environment("arduino-cli", Path("config.yaml"), PROFILE)

    def test_incompatible_cli_reports_actual_and_expected(self):
        with patch.object(checker, "run", return_value="arduino-cli Version: 1.2.0 Commit: old"):
            with self.assertRaisesRegex(checker.CheckFailure, "1.2.0.*1.3.1"):
                checker.check_environment("arduino-cli", Path("config.yaml"), PROFILE)

    def test_missing_core_reports_exact_install_command(self):
        with patch.object(checker, "run", side_effect=[
            "arduino-cli Version: 1.3.1 Commit: 08ff7e2b", '{"platforms": []}'
        ]):
            with self.assertRaisesRegex(checker.CheckFailure, "core install esp32:esp32@3.3.8"):
                checker.check_environment("arduino-cli", Path("config.yaml"), PROFILE)

    def test_incompatible_core_reports_installed_version(self):
        with patch.object(checker, "run", side_effect=[
            "arduino-cli Version: 1.3.1 Commit: 08ff7e2b",
            json.dumps({"platforms": [{"id": "esp32:esp32", "installed_version": "3.3.9"}]})
        ]):
            with self.assertRaisesRegex(checker.CheckFailure, "3.3.9.*3.3.8"):
                checker.check_environment("arduino-cli", Path("config.yaml"), PROFILE)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.root, self.library, self.build = (base / name for name in ("repo", "installed", "build"))
        self.hardware = base / "data/packages"
        self.revision = "a" * 40
        for directory in (self.root, self.library, self.build, self.hardware):
            directory.mkdir(parents=True)
        metadata = self.hardware / "esp32/hardware/esp32/3.3.8/installed.json"
        metadata.parent.mkdir(parents=True)
        metadata.write_text(json.dumps({"packages": [{"name": "esp32", "platforms": [
            {"version": "3.3.8", "checksum": "SHA-256:" + PROFILE["arduino_core_sha256"]}
        ]}]}))
        (self.library / "ARDUINO_PROFILE.json").write_text(json.dumps(PROFILE))
        files = []
        self.entries = []
        compiler = self.hardware / "esp32/tools/esp-x32/2601/bin/xtensa-esp32-elf-g++"
        for name in checker.PACKAGE_SOURCES:
            source = self.library / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("// canonical source\n")
            files.append({"packaged": name, "sha256": hashlib.sha256(source.read_bytes()).hexdigest()})
            output = self.build / (source.name + ".o")
            output.write_bytes(b"object")
            output.with_suffix(".d").write_text(f"{output}: {source}\n")
            self.entries.append({"directory": str(self.root), "file": str(source),
                                 "arguments": [str(compiler), "-DESP_PLATFORM", "-std=gnu++2a",
                                               "-std=gnu++17", "-I" + str(self.hardware / "qio_qspi/include"),
                                               "-c", str(source), "-o", str(output)]})
        (self.library / "PROVENANCE.json").write_text(json.dumps({"source_revision": self.revision, "files": files}))
        (self.build / "build.options.json").write_text(json.dumps({"fqbn": PROFILE["fqbn"],
                                                                 "hardwareFolders": str(self.hardware)}))
        settings = "# Espressif IoT Development Framework (ESP-IDF) 5.5.4 Project Configuration\n"
        settings += "\n".join(f"{key}={'y' if value == 1 else value}" for key, value in PROFILE["sdk_settings"].items() if value is not None)
        (self.build / "sdkconfig").write_text(settings)
        (self.build / "package_probe.ino.elf").write_bytes(b"\x7fELFlinked")
        self.save_entries()

    def save_entries(self):
        (self.build / "compile_commands.json").write_text(json.dumps(self.entries))

    def check(self, sketch_name=None):
        with patch.object(checker, "run", return_value=PROFILE["compiler_version"]):
            args = (self.root, self.library, self.build, PROFILE, self.revision)
            if sketch_name is None:
                checker.check_build(*args)
            else:
                checker.check_build(*args, sketch_name)

    def test_public_sketch_linked_outputs_are_accepted(self):
        (self.build / "package_probe.ino.elf").unlink()
        for name in ("ReceiveOnlyRuntime", "RuntimeSyntheticHarness"):
            with self.subTest(sketch=name):
                elf = self.build / f"{name}.ino.elf"
                elf.write_bytes(b"\x7fELFlinked")
                self.check(name)
                elf.unlink()

    def test_probe_output_cannot_satisfy_public_sketch_check(self):
        with self.assertRaisesRegex(checker.CheckFailure, "missing linked ReceiveOnlyRuntime.ino.elf"):
            self.check("ReceiveOnlyRuntime")

    def test_invalid_public_sketch_output_is_rejected(self):
        (self.build / "RuntimeSyntheticHarness.ino.elf").write_bytes(b"not linked ELF")
        with self.assertRaisesRegex(checker.CheckFailure, "missing linked RuntimeSyntheticHarness.ino.elf"):
            self.check("RuntimeSyntheticHarness")

    def test_sketch_name_cannot_escape_build_directory(self):
        for name in ("../package_probe", "/tmp/package_probe", "package_probe.ino", "", "a/b", "a\\b"):
            with self.subTest(sketch=name):
                with self.assertRaisesRegex(checker.CheckFailure, "sketch name"):
                    self.check(name)

    def test_clean_package_allows_sdk_app_main_source(self):
        source = self.hardware / "cores/app_main.cpp"
        source.parent.mkdir()
        source.write_text("// unrelated SDK source\n")
        entry = dict(self.entries[0], file=str(source))
        output = self.build / "sdk.o"
        output.write_bytes(b"object")
        output.with_suffix(".d").write_text(f"{output}: {source}\n")
        entry["arguments"] = [*entry["arguments"][:-4], "-c", str(source), "-o", str(output)]
        self.entries.append(entry)
        self.save_entries()
        self.check()

    def test_missing_package_source_is_rejected(self):
        self.entries.pop()
        self.save_entries()
        with self.assertRaisesRegex(checker.CheckFailure, "compiled package sources"):
            self.check()

    def test_unexpected_bench_binding_is_rejected(self):
        self.entries[0]["file"] = str(self.library / "src/bench_can_ack/binding.cpp")
        self.save_entries()
        with self.assertRaisesRegex(checker.CheckFailure, "unexpected package source"):
            self.check()

    def test_repository_header_dependency_is_rejected(self):
        output = Path(self.entries[0]["arguments"][-1])
        output.with_suffix(".d").write_text(f"{output}: {self.root}/components/leaked.hpp\n")
        with self.assertRaisesRegex(checker.CheckFailure, "repository leakage"):
            self.check()

    def test_response_file_repository_include_is_rejected(self):
        response = self.build / "flags"
        response.write_text(f"-I{self.root}/components/vehicle_core/include")
        self.entries[0]["arguments"].append("@" + str(response))
        self.save_entries()
        with self.assertRaisesRegex(checker.CheckFailure, "repository leakage"):
            self.check()

    def test_cpp17_must_be_effective_last_option(self):
        self.entries[0]["arguments"].append("-std=gnu++2a")
        self.save_entries()
        with self.assertRaisesRegex(checker.CheckFailure, "effective language"):
            self.check()

    def test_profile_mismatch_reports_setting(self):
        path = self.build / "sdkconfig"
        path.write_text(path.read_text().replace("CONFIG_FREERTOS_HZ=1000", "CONFIG_FREERTOS_HZ=100"))
        with self.assertRaisesRegex(checker.CheckFailure, "CONFIG_FREERTOS_HZ.*100.*1000"):
            self.check()

    def test_backend_flag_must_be_present(self):
        self.entries[0]["arguments"].remove("-DESP_PLATFORM")
        self.save_entries()
        with self.assertRaisesRegex(checker.CheckFailure, "ESP_PLATFORM"):
            self.check()

    def test_similarly_named_macro_does_not_select_backend(self):
        arguments = self.entries[0]["arguments"]
        arguments[arguments.index("-DESP_PLATFORM")] = "-DESP_PLATFORM_FAKE"
        self.save_entries()
        with self.assertRaisesRegex(checker.CheckFailure, "ESP_PLATFORM"):
            self.check()

    def test_corrupted_installed_source_is_rejected(self):
        Path(self.entries[0]["file"]).write_text("// corrupt installed copy")
        with self.assertRaisesRegex(checker.CheckFailure, "installed file hash mismatch"):
            self.check()

    def test_mismatched_compiler_is_rejected(self):
        with patch.object(checker, "run", return_value="compiler 14.1.0"):
            with self.assertRaisesRegex(checker.CheckFailure, "compiler reported.*14.1.0"):
                checker.check_build(self.root, self.library, self.build, PROFILE, self.revision)

    def test_missing_linked_output_is_rejected(self):
        (self.build / "package_probe.ino.elf").unlink()
        with self.assertRaisesRegex(checker.CheckFailure, "missing linked"):
            self.check()

    def test_missing_depfile_is_rejected(self):
        Path(self.entries[0]["arguments"][-1]).with_suffix(".d").unlink()
        with self.assertRaisesRegex(checker.CheckFailure, "dependency evidence"):
            self.check()

    def test_old_source_revision_is_rejected(self):
        self.revision = "b" * 40
        with self.assertRaisesRegex(checker.CheckFailure, "source revision"):
            self.check()


if __name__ == "__main__":
    unittest.main()
