"""Regression tests for the architecture boundary checker."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
import check_architecture  # noqa: E402


class ArchitectureCheckerRegressionTests(unittest.TestCase):
    def test_vehicle_core_target_private_mazda_dependency_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="architecture-core-fixture-") as directory:
            root = Path(directory)
            shutil.copytree(
                REPOSITORY_ROOT / "lib/vehicle_core",
                root / "lib/vehicle_core",
            )
            shutil.copytree(
                REPOSITORY_ROOT / "lib/mazda/include",
                root / "lib/mazda/include",
            )

            source = root / "lib/vehicle_core/src/vehicle_core.cpp"
            source.write_text(
                '#include "mazda/definitions.hpp"\n' + source.read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            cmake = root / "lib/vehicle_core/CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8")
                + "\n"
                "target_include_directories(vehicle_core PRIVATE\n"
                "  ${CMAKE_CURRENT_SOURCE_DIR}/../mazda/include\n"
                ")\n",
                encoding="utf-8",
            )

            work_dir = root / "work"
            work_dir.mkdir()
            with self.assertRaises(check_architecture.ArchitectureFailure) as raised:
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    check_architecture._check_core_only(
                        root,
                        "cmake",
                        ("c++",),
                        work_dir,
                    )

            detail = str(raised.exception)
            self.assertIn("vehicle_core target", detail)
            self.assertIn("lib/mazda", detail)

    def test_active_cmake_and_yaml_build_files_reject_retired_capture_marker(self) -> None:
        with tempfile.TemporaryDirectory(prefix="architecture-capture-fixture-") as directory:
            root = Path(directory)
            files = (
                root / "CMakeLists.txt",
                root / "components/example/CMakeLists.txt",
                root / "firmware/example/CMakeLists.txt",
                root / "components/example/idf_component.yaml",
            )
            marker = "raw_" + "capture"
            for path in files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(f"set(RETIRED_DEPENDENCY {marker})\n", encoding="utf-8")

            with self.assertRaises(check_architecture.ArchitectureFailure) as raised:
                check_architecture._check_capture_removal(root)

            detail = str(raised.exception)
            for path in files:
                self.assertIn(path.relative_to(root).as_posix(), detail)


if __name__ == "__main__":
    unittest.main()
