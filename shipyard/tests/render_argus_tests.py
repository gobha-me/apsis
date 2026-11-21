"""Pinned public renderer observations for the Argus exact source mutants.

Pixel goldens are explicit, independently hashed RGBA rasters. They do not read
the renderer's font, arrow routines or output as their expected value.
"""

import argparse
import json
import subprocess
import unittest
from pathlib import Path

import render_tests as public


ARROW = (
    ".........",
    "....#....",
    "....#....",
    "....#....",
    "....#....",
    "....#....",
    "....#....",
    "..#.#.#..",
    "...###...",
    "....#....",
    ".........",
)
# Hand-transcribed 5x7 title shapes, separated by the documented one-pixel gap.
# The full seven-character crop fixes the one-based two-digit DECK 01 title.
DECK01_GLYPHS = (
    ("####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."),
    ("#####", "#....", "#....", "####.", "#....", "#....", "#####"),
    (".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."),
    ("#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"),
    (".....",) * 7,
    (".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."),
    ("..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."),
)
DECK01 = tuple(".".join(glyph[row] for glyph in DECK01_GLYPHS) for row in range(7))


def raster(rows, ink, background):
    return bytes(channel for row in rows for pixel in row
                 for channel in (ink if pixel == "#" else background))


def crop(image, x, y, width, height):
    return bytes(channel for yy in range(y, y + height)
                 for xx in range(x, x + width) for channel in image.at(xx, yy))


def work_input(kind, count):
    """Syntactically real blueprints, deliberately failing later validation."""
    design = {
        "schema": "apsis-shipyard-v4", "name": "Work boundary", "buildType": "ship",
        "budget": 100000, "loads": {"cargo": 0, "jumpFuel": 1, "sublightFuel": 1},
        "decks": [{"name": "Boundary", "modules": [], "floors": {}}],
        "nextId": 1, "migrationNotes": [],
    }
    if kind == "modules":
        design["decks"][0]["modules"] = [
            {"id": index + 1, "type": "cockpit", "x": 0, "y": 0, "r": 0}
            for index in range(count)
        ]
    else:
        design["decks"][0]["floors"] = {f"{x},{y}": True
            for y in range(512) for x in range(512)}
        if count > 262144:
            design["decks"][0]["floors"]["512,0"] = True
    return design


def custom_rules(source, kind):
    data = json.loads((source / "rules/apsis-rules-v4.json").read_bytes())
    if kind == "color":
        next(part for part in data["parts"] if part["id"] == "reactor")["color"] = "x12a4f6"
    elif kind == "grid":
        data["grid"] = {"w": 512, "h": 512}
    return data


def requests(source):
    fixture = lambda name: json.loads((source / f"fixtures/v3/apsis-{name}-definition-v3.json").read_bytes())
    wren = fixture("wren")
    def request(design, **kw):
        return {"input": public.compact(design), "validate": True, **kw}
    station = public.PublicRenderer().station()
    all_requests = {
        "arrowGolden": request(station, options={"labels": False}),
        "titleGolden": request(station),
        "pngLevel6": request(wren),
        "dimension200": request(wren, options={"width": 200, "height": 200},
                                limits={"maxDimension": 200}),
        "dimension199": request(wren, options={"width": 200, "height": 200},
                                limits={"maxDimension": 199}),
        "zeroDeckLimit": request(wren, limits={"maxDecks": 0}),
        "zipByteLimit": request(wren, limits={"maxOutputBytes": 40000}),
        "modules4096": request(work_input("modules", 4096)),
        "modules4097": request(work_input("modules", 4097)),
        "floors262144": request(work_input("floors", 262144)),
        "floors262145": request(work_input("floors", 262145)),
        "colorFallback": request(wren, options={"labels": False},
                                 rules=public.compact(custom_rules(source, "color"))),
        "grid262144": request(wren, options={"width": 2048, "height": 2048, "labels": False},
                              rules=public.compact(custom_rules(source, "grid"))),
    }
    parent = work_input("modules", 1)
    parent["nextId"] = 2
    parent["decks"][0]["modules"][0]["craft"] = work_input("modules", 4096)
    all_requests["nested4097"] = request(parent)
    return all_requests


class Argus(public.PublicRenderer):
    def render(self, design, **kwargs):
        kwargs.setdefault("validate", True)
        return super().render(design, **kwargs)

    def assert_only_error(self, result, code, path, message):
        self.rejected(result, code)
        self.assertEqual(result["envelope"]["issues"], [{"code": code, "severity": "error",
            "path": path, "message": message, "details": {}}])

    def accepted_rules(self, result, expected):
        loaded = result["rulesLoad"]
        self.assertTrue(loaded["ok"])
        self.assertEqual(loaded["issues"], [])
        self.assertEqual(loaded["snapshot"], {"data": expected, "version": 4,
            "sha256": public.sha(public.compact(expected).encode())})

    def test_arrow_wing_and_deck01_independent_pixel_goldens(self):
        expected_arrow = raster(ARROW, public.INK, (114, 205, 232, 255))
        expected_title = raster(DECK01, public.TITLE, public.BACKGROUND)
        self.assertEqual(public.sha(expected_arrow), "248928a75031ae5e2e12988846f9b31ea255f90293601cfc37802698d797305c")
        self.assertEqual(public.sha(expected_title), "13d3b14cb98bfaff2f360090c6c6f4fcb72de062694d6d192415f1fb1c864489")
        station = self.station()
        result = self.render(station, options={"labels": False})
        _, images = self.verify_bundle(result, station, {"labels": False})
        # 26x24 grid, cell=24, origin=(88,48); cockpit (3,3), center=(172,132).
        actual_arrow = crop(images[0], 168, 131, 9, 11)
        self.assertEqual(actual_arrow, expected_arrow)
        self.assertEqual(public.sha(actual_arrow), "248928a75031ae5e2e12988846f9b31ea255f90293601cfc37802698d797305c")
        labeled = self.render(station)
        _, titled = self.verify_bundle(labeled, station)
        actual_title = crop(titled[0], 8, 10, 41, 7)
        self.assertEqual(actual_title, expected_title)
        self.assertEqual(public.sha(actual_title), "13d3b14cb98bfaff2f360090c6c6f4fcb72de062694d6d192415f1fb1c864489")

    def test_level6_png_exact_bytes_and_digest(self):
        design = self.fixture("wren")
        result = self.render(design)
        self.verify_bundle(result, design)
        image = result["artifacts"][0]
        # Independent hashlib pin from the reviewed Original, before mutations;
        # compressed bytes fix level 6 even when level 9 decodes to equal pixels.
        self.assertEqual(len(image["raw"]), 12035)
        self.assertEqual(public.sha(image["raw"]), "b3a35b042307647db05dcb4fca9f5851fc5eb0f601af4b9eac1976c9a0cba058")
        self.assertEqual(image["sha256"], "b3a35b042307647db05dcb4fca9f5851fc5eb0f601af4b9eac1976c9a0cba058")

    def test_dimension_zero_deck_and_zip_byte_limit_diagnostics(self):
        cases = requests(public.SOURCE)
        self.verify_bundle(self.calls(cases["dimension200"])[0], self.fixture("wren"),
                           {"width": 200, "height": 200})
        self.assert_only_error(self.calls(cases["dimension199"])[0], "render_resource_limit", "/options",
            "Deck count, dimensions, or total pixels exceed operational render limits.")
        self.assert_only_error(self.calls(cases["zeroDeckLimit"])[0], "invalid_render_limits", "/limits",
            "Render limits must be positive and cannot exceed the documented hard limits.")
        baseline = self.render(self.fixture("wren"))
        non_zip = sum(len(item["raw"]) for item in baseline["artifacts"][:-1])
        self.assertLess(non_zip, 40000)
        self.assertGreater(sum(len(item["raw"]) for item in baseline["artifacts"]), 40000)
        self.assert_only_error(self.calls(cases["zipByteLimit"])[0], "render_resource_limit", "",
            "Rendered output exceeds the operational byte limit.")

    def test_exact_module_floor_and_nested_work_boundaries(self):
        cases = requests(public.SOURCE)
        for key in ("modules4096", "floors262144"):
            with self.subTest(key=key):
                result = self.calls(cases[key])[0]
                self.rejected(result)
                issues = result["envelope"]["issues"]
                codes_paths = {(issue["code"], issue["path"]) for issue in issues}
                self.assertNotIn("render_resource_limit", [issue["code"] for issue in issues])
                self.assertIn(("grid_capacity", "/decks/0"), codes_paths)
                capacity = {"code": "grid_capacity", "severity": "error", "path": "/decks/0",
                    "message": "Floor or module count exceeds the active grid capacity.", "details": {}}
                self.assertIn(capacity, issues)
                if key == "modules4096":
                    self.assertEqual(issues, [capacity, {"code": "invalid_next_id", "severity": "error",
                        "path": "/nextId", "details": {},
                        "message": "Next module ID must exceed all existing IDs and stay within the active rules limit."}])
                else:
                    self.assertIn(("invalid_coordinate", "/decks/0/floors/0,100"), codes_paths)
                    self.assertIn({"code": "invalid_coordinate", "severity": "error", "details": {},
                        "path": "/decks/0/floors/0,100",
                        "message": "Floor coordinate is outside the active build grid."}, issues)
                    self.assertEqual(len(issues), 128)
                    marker = next(issue for issue in issues if issue["code"] == "issues_truncated")
                    self.assertEqual(marker["path"], "")
                    self.assertEqual(marker["severity"], "error")
                    self.assertEqual(marker["details"], {"limit": 128, "suppressedAtLeast": 1})
        for key, resource in (("modules4097", "module"), ("nested4097", "module"),
                              ("floors262145", "floor")):
            with self.subTest(key=key):
                self.assert_only_error(self.calls(cases[key])[0], "render_resource_limit", "",
                    f"Input exceeds the renderer's operational {resource}-entry limit.")

    def test_accepted_rule_color_fallback_grid_boundary_and_three_pixel_floor(self):
        cases = requests(public.SOURCE)
        color_rules = custom_rules(public.SOURCE, "color")
        result = self.calls(cases["colorFallback"])[0]
        self.accepted_rules(result, color_rules)
        manifest, images = self.verify_bundle(result, self.fixture("wren"),
            {"labels": False}, public.compact(color_rules).encode())
        camera = manifest["camera"]
        # Reactor at (9,7) must use neutral presentation fallback, not #12a4f6.
        self.assertEqual(images[0].at(camera["originX"] + 9 * camera["cellPixels"] + 2,
                                     camera["originY"] + 7 * camera["cellPixels"] + 2),
                         (128, 128, 128, 255))
        grid_rules = custom_rules(public.SOURCE, "grid")
        result = self.calls(cases["grid262144"])[0]
        self.accepted_rules(result, grid_rules)
        manifest, images = self.verify_bundle(result, self.fixture("wren"),
            {"width": 2048, "height": 2048, "labels": False}, public.compact(grid_rules).encode())
        self.assertEqual(manifest["camera"], {"projection": "top-down-full-grid", "originX": 256,
            "originY": 272, "cellPixels": 3, "gridWidth": 512, "gridHeight": 512,
            "positiveX": "right", "positiveY": "down"})
        # Floor (13,3) has no module: all nine pixels pin its top/left inset.
        self.assertEqual(crop(images[0], 295, 281, 3, 3), bytes(
            public.GRID * 3 + public.GRID + (139, 159, 172, 255) * 2
            + public.GRID + (139, 159, 172, 255) * 2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--snapshot", action="store_true")
    args = parser.parse_args()
    public.DRIVER, public.SOURCE = args.driver, args.source
    if args.snapshot:
        # No unittest/expected values on this path. Every response includes full
        # public envelopes, rules snapshots, artifact metadata and exact bytes.
        outputs = {}
        for key, request in requests(args.source).items():
            process = subprocess.run([str(args.driver)], input=public.compact(request) + "\n",
                                     text=True, encoding="utf-8", capture_output=True, check=True)
            outputs[key] = json.loads(process.stdout)
        print(public.compact(outputs), flush=True)
    else:
        unittest.main(argv=[__file__], verbosity=2)
