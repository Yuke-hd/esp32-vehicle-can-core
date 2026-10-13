#!/usr/bin/env python3
"""Stage immutable canonical sources as a deterministic Arduino library ZIP."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = "ESP32VehicleCANCore"
COMPONENTS = ("vehicle_core", "can_bus", "vehicle_telemetry")
SOURCES = ("vehicle_core/src/vehicle_core.cpp", "can_bus/src/can_bus.cpp",
           "vehicle_telemetry/src/runtime.cpp", "vehicle_telemetry/src/can_bus_source.cpp")
PROPERTIES = """name=ESP32 Vehicle CAN Core
version=0.2.0
author=ESP32 Vehicle CAN Core contributors
maintainer=ESP32 Vehicle CAN Core contributors
sentence=Generic receive-only CAN acquisition and injected telemetry runtime.
paragraph=C++17 classic ESP32 composition with generic readings and notifications. The application supplies one listen-only driver binding.
category=Communication
url=https://github.com/Yuke-hd/esp32-vehicle-can-core
architectures=esp32
includes=ESP32VehicleCANCore.h
"""
DEPENDENCIES = {
    "arduino_cli": "1.3.1", "arduino_core": "esp32:esp32@3.3.8",
    "esp_idf": "v5.5.4", "compiler_package": "esp-x32@2601",
    "core_archive_sha256": "8159cefaa2eea131360ac32393d741d462583297c8c1e5169ca48139fa324173",
    "core_index": "https://espressif.github.io/arduino-esp32/package_esp32_index.json",
    "third_party_sources_bundled": False,
}


DEPENDENCY_NOTICES = """# Arduino build dependencies (not bundled)

The generated library contains canonical project sources under Apache-2.0.
The following dependencies are installed separately by Arduino CLI; their
upstream distributions and notices remain authoritative.

- Arduino-ESP32 3.3.8: https://github.com/espressif/arduino-esp32/tree/3.3.8
  LGPL-2.1, https://github.com/espressif/arduino-esp32/blob/3.3.8/LICENSE.md
- ESP-IDF v5.5.4 bundled in esp32-libs 3.3.8:
  https://github.com/espressif/esp-idf/tree/v5.5.4 (Apache-2.0 and upstream notices).
- esp-x32 2601, GCC 14.2.0 / esp-14.2.0_20260121:
  https://github.com/espressif/crosstool-NG/releases/tag/esp-14.2.0_20260121
  GPL-3.0 with GCC runtime library exception and bundled toolchain notices.
- Arduino CLI 1.3.1: https://github.com/arduino/arduino-cli/tree/v1.3.1
  GPL-3.0, https://github.com/arduino/arduino-cli/blob/v1.3.1/LICENSE.txt

No Arduino core, SDK, toolchain, host-test dependency or private data is copied
into this library. See PROVENANCE.json for the exact project source revision.
"""


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def package_files(revision):
    files = {"library.properties": PROPERTIES.encode(),
             "src/ESP32VehicleCANCore.h": b'#pragma once\n\n// Include convenience only; APIs remain in their canonical namespaces.\n#include "vehicle_core/vehicle_core.hpp"\n#include "vehicle_core/notification_channel.hpp"\n#include "vehicle_telemetry/runtime.hpp"\n#include "vehicle_telemetry/can_bus_source.hpp"\n'}
    files["ARDUINO_DEPENDENCIES.md"] = DEPENDENCY_NOTICES.encode()
    files["ARDUINO_PROFILE.json"] = (ROOT / "tools/arduino/profile.json").read_bytes()
    entries = []
    paths = git("ls-tree", "-r", "--name-only", revision, "components").decode().splitlines()
    required = {f"components/{source}" for source in SOURCES} | {
        "components/can_bus/private_include/can_bus/frame_ring.hpp",
        "components/can_bus/private_include/can_bus/lifecycle.hpp",
        "components/can_bus/private_include/can_bus/configuration.hpp",
        "components/vehicle_telemetry/src/runtime_budget.hpp",
    }
    if missing := required - set(paths):
        raise ValueError(f"revision lacks required package inputs: {sorted(missing)}")
    for component in COMPONENTS:
        prefix = f"components/{component}/"
        for source in paths:
            if not source.startswith(prefix):
                continue
            relative = source[len(prefix):]
            if relative.startswith("include/"):
                destination = "src/" + relative[len("include/"):]
                visibility = "internal" if "/internal/" in destination else "public"
            elif relative.startswith("private_include/"):
                destination = "src/" + relative[len("private_include/"):]
                visibility = "internal"
            elif f"{component}/{relative}" in SOURCES or relative == "src/runtime_budget.hpp":
                destination = f"src/{component}/" + relative[len("src/"):]
                visibility = "internal"
            else:
                continue
            data = git("show", f"{revision}:{source}")
            if destination in files:
                raise ValueError(f"duplicate package path: {destination}")
            files[destination] = data
            entries.append({"source": source, "packaged": destination,
                            "visibility": visibility, "sha256": hashlib.sha256(data).hexdigest()})
    for notice in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        files[notice] = git("show", f"{revision}:{notice}")
    provenance = {"source_revision": revision, "language": "C++17 or newer",
                  "dependencies": DEPENDENCIES,
                  "packaging_inputs": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                       for name in ("tools/stage_arduino.py", "tools/arduino/profile.json")},
                  "files": sorted(entries, key=lambda x: x["packaged"])}
    files["PROVENANCE.json"] = (json.dumps(provenance, indent=2, sort_keys=True) + "\n").encode()
    return files


def stage(output, revision):
    revision = git("rev-parse", "--verify", f"{revision}^{{commit}}").decode().strip()
    if output.exists():
        raise ValueError("output must not exist; select a fresh staging path")
    files = package_files(revision)
    tree = output / LIBRARY
    tree.mkdir(parents=True)
    archive = output / f"{LIBRARY}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as package:
        for name, data in sorted(files.items()):
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            entry = zipfile.ZipInfo(f"{LIBRARY}/{name}", date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            package.writestr(entry, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / f"{LIBRARY}.zip.sha256").write_text(f"{digest}  {archive.name}\n")
    return {"source_revision": revision, "package_sha256": digest, "archive": str(archive)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", default="HEAD", help="immutable source commit (default HEAD)")
    args = parser.parse_args()
    try:
        print(json.dumps(stage(args.output, args.revision), sort_keys=True))
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"staging failed: {error}\n")


if __name__ == "__main__":
    main()
