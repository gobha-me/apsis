"""Build optional WASI test binaries and compare them with native public output."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

from test_cli import ADJACENT, REORDERED, SEEDS, probe_snapshot


GALAXY = Path(__file__).resolve().parents[1]
NATIVE_BUILD = None
WASM_BUILD = None
NODE = None


def build_wasm(sdk, build_dir):
    build_dir.mkdir(parents=True, exist_ok=True)
    compiler = sdk / "bin" / "clang++"
    build_lock = os.environ.get("GALAXY_BUILD_LOCK")
    flags = [
        "--target=wasm32-wasip1",
        "-std=c++23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        # The public-API test helper reports failures using C++ exceptions.
        # SDK 33+ requires standard EH plus explicit libunwind at link time:
        # https://github.com/WebAssembly/wasi-sdk/blob/main/CppExceptions.md
        "-fwasm-exceptions",
        "-mllvm",
        "-wasm-use-legacy-eh=false",
        "-I",
        str(GALAXY / "include"),
    ]

    def compile_or_link(arguments):
        command = [str(compiler), *flags, *arguments]
        if build_lock:
            command = ["flock", build_lock, *command]
        subprocess.run(
            command,
            check=True,
            timeout=300,
        )

    print("Building optional WASI map object", flush=True)
    map_object = build_dir / "map.o"
    compile_or_link(["-c", str(GALAXY / "src" / "map.cpp"), "-o", str(map_object)])
    for source, output in (
        (GALAXY / "tests" / "map_probe.cpp", "probe.wasm"),
        (GALAXY / "src" / "cli.cpp", "cli.wasm"),
    ):
        print(f"Building optional WASI {output}", flush=True)
        compile_or_link(
            [str(source), str(map_object), "-lunwind", "-o", str(build_dir / output)]
        )


class NativeWasmTests(unittest.TestCase):
    maxDiff = None

    def run_program(self, command, arguments):
        return subprocess.run(
            [*command, *arguments],
            check=False,
            capture_output=True,
            timeout=30,
        )

    def native_command(self, program):
        return [str(NATIVE_BUILD / program)]

    def wasm_command(self, program):
        return [
            NODE,
            "--no-warnings",
            str(GALAXY / "tests" / "run_wasi.mjs"),
            str(WASM_BUILD / program),
        ]

    def successful_output(self, command, arguments):
        completed = self.run_program(command, arguments)
        self.assertEqual(completed.returncode, 0, completed.stderr.decode())
        self.assertEqual(completed.stderr, b"")
        return completed.stdout

    def test_native_wasm_public_snapshots_and_cli_json_are_identical(self):
        cases = {
            "unrevealed": (),
            "empty_center": ((0, 0),),
            "extreme_cells": (
                (-(1 << 31), (1 << 31) - 1),
                ((1 << 31) - 1, -(1 << 31)),
            ),
            "adjacent": ADJACENT,
            "reversed": tuple(reversed(ADJACENT)),
            "shuffled": REORDERED,
            "repeated": REORDERED + ADJACENT,
        }
        for seed in SEEDS:
            for name, regions in cases.items():
                with self.subTest(seed=seed, case=name):
                    probe_arguments = [str(seed), *(f"{cx},{cy}" for cx, cy in regions)]
                    native_records = self.successful_output(
                        self.native_command("galaxy-map-probe"), probe_arguments
                    )
                    wasm_records = self.successful_output(
                        self.wasm_command("probe.wasm"), probe_arguments
                    )
                    self.assertEqual(wasm_records, native_records)
                    cli_arguments = ["--seed", str(seed)]
                    for cx, cy in regions:
                        cli_arguments.extend(("--region", f"{cx},{cy}"))
                    native_json = json.loads(
                        self.successful_output(
                            self.native_command("galaxy-cli"), cli_arguments
                        )
                    )
                    wasm_json = json.loads(
                        self.successful_output(
                            self.wasm_command("cli.wasm"), cli_arguments
                        )
                    )
                    self.assertEqual(wasm_json, native_json)
                    self.assertEqual(
                        wasm_json, probe_snapshot(wasm_records.decode(), regions)
                    )

    def test_native_wasm_seed_overflow_errors_are_identical(self):
        arguments = ["--seed", "18446744073709551616"]
        native = self.run_program(self.native_command("galaxy-cli"), arguments)
        wasm = self.run_program(self.wasm_command("cli.wasm"), arguments)
        for completed in (native, wasm):
            self.assertEqual(completed.returncode, 2)
            self.assertEqual(completed.stdout, b"")
            error = json.loads(completed.stderr)
            self.assertEqual(error["status"], "invalid_arguments")
            self.assertTrue(error["error"])
        self.assertEqual(json.loads(wasm.stderr), json.loads(native.stderr))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--native-build", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--node", default="node")
    arguments = parser.parse_args()
    NATIVE_BUILD = arguments.native_build.resolve()
    WASM_BUILD = arguments.build_dir.resolve()
    NODE = arguments.node
    build_wasm(arguments.sdk.resolve(), WASM_BUILD)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
