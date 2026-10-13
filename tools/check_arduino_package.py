#!/usr/bin/env python3
"""Verify the selected Arduino tools and actual clean installed-package build.

This is compile/link evidence only. It does not run firmware or measure CAN.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PACKAGE_SOURCES = frozenset({
    "src/vehicle_core/vehicle_core.cpp", "src/can_bus/can_bus.cpp",
    "src/vehicle_telemetry/runtime.cpp", "src/vehicle_telemetry/can_bus_source.cpp",
})


class CheckFailure(ValueError):
    """An actionable unsupported-environment or package-boundary failure."""


def run(arguments):
    result = subprocess.run(arguments, check=False, capture_output=True, text=True, timeout=30)
    require(result.returncode == 0,
            f"command failed ({' '.join(map(str, arguments))}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout.strip()


def require(condition, message):
    if not condition:
        raise CheckFailure(message)


def within(path, directory):
    try:
        path.resolve().relative_to(directory.resolve())
        return True
    except ValueError:
        return False


def check_environment(cli, config, profile):
    try:
        output = run([cli, "--config-file", str(config), "version"])
    except (OSError, subprocess.SubprocessError) as error:
        raise CheckFailure(f"Arduino CLI unavailable: {error}. Install Arduino CLI {profile['arduino_cli']}.") from error
    actual = re.search(r"Version:\s*(\S+)", output)
    require(actual is not None and actual[1] == profile["arduino_cli"],
            f"Arduino CLI reported {output}; need exactly {profile['arduino_cli']}.")
    require(f"Commit: {profile['arduino_cli_commit']}" in output,
            f"Arduino CLI commit mismatch: {output}; need {profile['arduino_cli_commit']}.")
    platforms = json.loads(run([cli, "--config-file", str(config), "core", "list", "--json"]))
    core, version = profile["arduino_core"].split("@")
    installed = next((p.get("installed_version") for p in platforms.get("platforms", []) if p["id"] == core), None)
    require(installed == version,
            f"Arduino core installed {installed or 'none'}; need {core}@{version}. "
            f"Run: {cli} --config-file {config} core install {core}@{version}")
    print(f"OK Arduino CLI {profile['arduino_cli']} ({profile['arduino_cli_commit']}), core {core}@{version}")


def expand_arguments(arguments, directory, depth=0):
    require(depth < 8, "compiler response files are nested too deeply")
    expanded = []
    for argument in arguments:
        if argument.startswith("@"):
            path = Path(argument[1:])
            if not path.is_absolute():
                path = directory / path
            expanded.extend(expand_arguments(shlex.split(path.read_text()), directory, depth + 1))
        else:
            expanded.append(argument)
    return expanded


def check_build(root, library, build, profile, revision):
    root, library, build = (path.resolve() for path in (root, library, build))
    require(not within(library, root) and not within(build, root),
            "installed library and build must use clean paths outside the repository")
    provenance = json.loads((library / "PROVENANCE.json").read_text())
    require(provenance["source_revision"] == revision,
            f"installed source revision {provenance['source_revision']}; need current PR revision {revision}")
    require(json.loads((library / "ARDUINO_PROFILE.json").read_text()) == profile,
            "installed ARDUINO_PROFILE.json differs from the selected repository profile")
    for entry in provenance["files"]:
        path = library / entry["packaged"]
        require(within(path, library), f"invalid provenance path: {entry['packaged']}")
        require(hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"],
                f"installed file hash mismatch: {entry['packaged']}; reinstall the freshly staged ZIP")
    options = json.loads((build / "build.options.json").read_text())
    require(options["fqbn"] == profile["fqbn"],
            f"board options reported {options['fqbn']}; need {profile['fqbn']}")
    hardware = Path(options["hardwareFolders"]).resolve()
    core, version = profile["arduino_core"].split("@")
    metadata = hardware / "esp32/hardware/esp32" / version / "installed.json"
    packages = json.loads(metadata.read_text())["packages"]
    platform = next((p for package in packages if package["name"] == core.split(":")[0]
                     for p in package["platforms"] if p["version"] == version), None)
    require(platform is not None, f"installed core metadata missing for {core}@{version}; reinstall the pinned core")
    require(platform["checksum"] == "SHA-256:" + profile["arduino_core_sha256"],
            f"installed core archive checksum changed; need {profile['arduino_core_sha256']}")
    sdkconfig = (build / "sdkconfig").read_text()
    require(f"(ESP-IDF) {profile['esp_idf'].lstrip('v')} Project Configuration" in sdkconfig,
            f"SDK version mismatch; need bundled IDF {profile['esp_idf']}")
    settings = dict(re.findall(r"^(CONFIG_\w+)=(.*)$", sdkconfig, re.MULTILINE))
    for key, expected in profile["sdk_settings"].items():
        actual = settings.get(key)
        if actual == "y":
            actual = 1
        elif actual is not None and actual.isdigit():
            actual = int(actual)
        require(actual == expected, f"SDK setting {key} reported {actual}; need {expected}")
    database = json.loads((build / "compile_commands.json").read_text())
    require(database, "missing compiled source evidence: compile_commands.json is empty")
    compiled = set()
    compiler = None
    for entry in database:
        directory = Path(entry["directory"])
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        source = source.resolve()
        require(not within(source, root), f"repository leakage in compiled source: {source}")
        arguments = expand_arguments(entry["arguments"], directory)
        for index, argument in enumerate(arguments):
            include = None
            if argument in ("-I", "-isystem", "-iquote", "-include", "-iprefix"):
                include = arguments[index + 1]
            elif argument.startswith("-I"):
                include = argument[2:]
            if include:
                path = Path(include)
                if not path.is_absolute():
                    path = directory / path
                require(not within(path, root), f"repository leakage in compiler include: {path}")
        if within(source, library):
            name = source.relative_to(library).as_posix()
            require(name in PACKAGE_SOURCES, f"unexpected package source: {name}; exclude bench, app_main and host sources")
            compiled.add(name)
            language = [arg for arg in arguments if arg.startswith("-std=")]
            require(language and language[-1] == profile["language_option"],
                    f"effective language for {name} is {language[-1:]}; need {profile['language_option']}")
            backend = [arg for arg in arguments if (arg == "-DESP_PLATFORM" or arg.startswith("-DESP_PLATFORM=")) or arg == "-UESP_PLATFORM"]
            require(backend and backend[-1].startswith("-DESP_PLATFORM"), f"ESP_PLATFORM backend missing for {name}")
            require(any(f"/{profile['sdk_memory_type']}/include" in arg for arg in arguments),
                    f"SDK memory profile missing for {name}; need {profile['sdk_memory_type']}")
            tool = Path(arguments[0]).resolve()
            package, version = profile["compiler_package"].split("@")
            require(f"/{package}/{version}/bin/" in tool.as_posix(),
                    f"compiler package {tool}; need {profile['compiler_package']}")
            require(compiler is None or compiler == tool, "package sources use different compilers")
            compiler = tool
        else:
            require(within(source, hardware) or within(source, build / "sketch"),
                    f"unexpected source outside installed package/SDK/sketch: {source}")
        output_index = arguments.index("-o") + 1
        output = Path(arguments[output_index])
        if not output.is_absolute():
            output = directory / output
        depfile = output.with_suffix(".d")
        require(output.is_file() and depfile.is_file(), f"missing compiled object/dependency evidence: {output}")
        dependencies = shlex.split(depfile.read_text().replace("\\\n", " ").split(":", 1)[1])
        require(dependencies, f"missing dependency evidence: {depfile}")
        for dependency in dependencies:
            path = Path(dependency)
            if not path.is_absolute():
                path = directory / path
            require(not within(path, root), f"repository leakage in dependency evidence: {path}")
    require(compiled == PACKAGE_SOURCES,
            f"compiled package sources differ: missing {sorted(PACKAGE_SOURCES - compiled)}; extra {sorted(compiled - PACKAGE_SOURCES)}")
    output = run([str(compiler), "--version"]).splitlines()[0]
    require(output == profile["compiler_version"], f"compiler reported {output}; need {profile['compiler_version']}")
    elf = build / "package_probe.ino.elf"
    require(elf.is_file() and elf.read_bytes()[:4] == b"\x7fELF", "missing linked package_probe.ino.elf; run the actual compile/link command")
    print(f"OK {profile['compiler_version']}; {profile['esp_idf']}; {profile['sdk_memory_type']}; SDK settings {profile['sdk_settings']}")
    print(f"OK installed revision {revision}: {len(compiled)} package sources, {len(database)} objects/dependency files, linked ELF; no repository leakage")
    print(f"OK board options: {profile['fqbn']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default="arduino-cli")
    parser.add_argument("--config-file", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--build", type=Path)
    args = parser.parse_args()
    if bool(args.library) != bool(args.build):
        parser.error("--library and --build must be supplied together after a real compile/link")
    try:
        profile = json.loads((args.root / "tools/arduino/profile.json").read_text())
        check_environment(args.cli, args.config_file, profile)
        if args.build:
            revision = run(["git", "-C", str(args.root), "rev-parse", "HEAD"])
            check_build(args.root, args.library, args.build, profile, revision)
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        print(f"FAIL Arduino package check: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
