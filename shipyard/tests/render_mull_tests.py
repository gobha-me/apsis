"""Additive public renderer boundaries from actual cumulative Mull evidence."""

import argparse
import copy
import json
import subprocess
import unittest
from pathlib import Path

import render_argus_tests as argus
import render_tests as public


# Captured through the frozen, reviewed public driver, then independently hashed
# with hashlib and sha256sum before testing the behavior-preserving cleanup.
PNG_GOLDENS = {
    "slugWide": {
        "bytes": 12097,
        "sha256": "8271ae0272435925f0e19d75d35fc27e55f7ac91afb8ba59789391556a755997"
    },
    "slugClip": {
        "bytes": 1702,
        "sha256": "82532eda8281661cced98f4878edbb559abd74e49683485ad79cc68d89c8bcf9"
    },
    "cellFour": {
        "bytes": 701,
        "sha256": "36830d5f204a22565770a2bbee39e538962564d2c617c58b97353af293b2e537"
    },
    "cellEleven": {
        "bytes": 2107,
        "sha256": "2a5ff7155b8bbdaa5978363624694ca9952618640bd42287d3d5e03ef6ac80fa"
    },
    "rotatedTank": {
        "bytes": 1869,
        "sha256": "b28420d36b58ec11882bf886d4b7444076fa15f5940476eb80a640307f131ec4"
    },
    "tenDecks": {
        "bytes": 974,
        "sha256": "a2d4919cc143bdd588900cf6ed8e331053776a9a6efb73143bfc6817869ceb29"
    }
}

CELL_FOUR = ("BBBB", "BFFB", "BIII", "BBIB")
TANK_ARROW = (
    ".........", ".....#...", "....#....", "...####..",
    "....#....", ".....#...", ".........",
)
Z_GLYPH = ("#####", "....#", "...#.", "..#..", ".#...", "#....", "#####")


def expanded_requests(source):
    cases = argus.requests(source)
    installed = json.loads((source / "rules/apsis-rules-v4.json").read_bytes())
    fixture = lambda name: json.loads(
        (source / f"fixtures/v3/apsis-{name}-definition-v3.json").read_bytes())

    def request(design, active=None, **settings):
        return {"input": public.compact(design), "validate": True,
                "rules": public.compact(installed if active is None else active),
                **settings}

    station = public.PublicRenderer().station()
    named = fixture("wren")
    named["blueprint"]["decks"][0]["name"] = "az09Zz" + "q" * 42
    cases["slugWide"] = request(named)
    cases["slugClip"] = request(named, options={"width": 201, "height": 200})
    cases["cellOne"] = request(station, options={"width": 64, "height": 96, "labels": False})
    cases["cellFour"] = request(station, options={"width": 144, "height": 144, "labels": False})
    cases["cellEleven"] = request(station, options={"width": 318, "height": 312})
    tank = copy.deepcopy(station)
    tank["decks"][0]["modules"][-1].update(type="tank", r=1)
    cases["rotatedTank"] = request(tank, options={"width": 318, "height": 312, "labels": False})

    for name, width, height, constrained in (
            ("thinWidth", 262144, 1, "h"), ("thinHeight", 1, 262144, "w")):
        active = copy.deepcopy(installed)
        active["grid"] = {"w": width, "h": height}
        for part in active["parts"]:
            part[constrained] = 1
        cases[name] = request(fixture("wren"), active, validate=False)
    overcap = copy.deepcopy(installed)
    overcap["grid"] = {"w": 513, "h": 512}
    cases["gridOvercap"] = request(fixture("wren"), overcap, validate=False,
        options={"width": 2048, "height": 2048, "labels": False})

    ten = fixture("wayfarer-anchorage")
    blueprint = ten["blueprint"]
    floors = blueprint["decks"][-1]["floors"]
    for level in range(4, 11):
        blueprint["decks"].append({"name": f"Level {level}",
            "floors": copy.deepcopy(floors), "modules": [{"id": blueprint["nextId"],
                "type": "ladder", "x": 13, "y": 10, "r": 0}]})
        blueprint["nextId"] += 1
    active = copy.deepcopy(installed)
    active["limits"]["maxDecks"] = 10
    cases["tenDecks"] = request(ten, active, options={"width": 200, "height": 200})

    # Fixed input budgets measured on the frozen Original. Snapshot inputs do
    # not adapt to the output of whichever driver is being compared.
    wren = fixture("wren")
    cases["budgetBaseline"] = request(wren)
    cases["prefixPngBudget"] = request(wren, limits={"maxOutputBytes": 24069})
    cases["exactTotalBudget"] = request(wren, limits={"maxOutputBytes": 51016})
    cases["oneByteShortBudget"] = request(wren, limits={"maxOutputBytes": 51015})
    return cases


class MullBoundaries(public.PublicRenderer):
    accepted_rules = argus.Argus.accepted_rules
    assert_only_error = argus.Argus.assert_only_error

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.cases = expanded_requests(public.SOURCE)

    def observed(self, key):
        request = self.cases[key]
        result = self.calls(request)[0]
        self.accepted_rules(result, json.loads(request["rules"]))
        return result

    def accepted(self, key):
        result = self.observed(key)
        request = self.cases[key]
        design = json.loads(request["input"])
        manifest, images = self.verify_bundle(result, design, request.get("options"),
                                               request["rules"].encode())
        return result, manifest, images

    def golden(self, key, result, index=0):
        image = result["artifacts"][index]
        expected = PNG_GOLDENS[key]
        self.assertEqual(len(image["raw"]), expected["bytes"])
        self.assertEqual(public.sha(image["raw"]), expected["sha256"])
        self.assertEqual(image["sha256"], expected["sha256"])

    def test_ascii_slug_lowercase_z_and_title_right_margin(self):
        expected_name = "deck-01-az09zz" + "q" * 34 + ".png"
        wide, _, images = self.accepted("slugWide")
        self.assertEqual(wide["artifacts"][0]["filename"], expected_name)
        self.golden("slugWide", wide)
        expected_z = argus.raster(Z_GLYPH, public.TITLE, public.BACKGROUND)
        self.assertEqual(argus.crop(images[0], 68, 10, 5, 7), expected_z)
        # Both literal uppercase Z and lowercase z select the same Z glyph.
        self.assertEqual(argus.crop(images[0], 86, 10, 5, 7), expected_z)
        self.assertEqual(argus.crop(images[0], 92, 10, 5, 7), expected_z)
        clipped, _, narrow = self.accepted("slugClip")
        self.assertEqual(clipped["artifacts"][0]["filename"], expected_name)
        self.golden("slugClip", clipped)
        self.assertEqual(argus.crop(narrow[0], 193, 10, 8, 7),
                         bytes(public.BACKGROUND * (8 * 7)))

    def test_cells_one_three_four_and_eleven_have_exact_public_pixels(self):
        one, manifest, images = self.accepted("cellOne")
        self.assertEqual(manifest["camera"]["cellPixels"], 1)
        self.assertEqual(images[0].at(22, 55), (114, 205, 232, 255))
        self.assertEqual(len(one["artifacts"]), 4)
        _, manifest, images = self.accepted("grid262144")
        self.assertEqual(manifest["camera"]["cellPixels"], 3)
        self.assertEqual(images[0].at(292, 287), public.BORDER)
        four, manifest, images = self.accepted("cellFour")
        self.assertEqual(manifest["camera"]["cellPixels"], 4)
        colors = {"B": public.BORDER, "F": (114, 205, 232, 255), "I": public.INK}
        expected_four = bytes(channel for row in CELL_FOUR for pixel in row
                              for channel in colors[pixel])
        self.assertEqual(argus.crop(images[0], 32, 52, 4, 4), expected_four)
        self.golden("cellFour", four)
        eleven, manifest, images = self.accepted("cellEleven")
        self.assertEqual(manifest["camera"]["cellPixels"], 11)
        # This is the top row of a complete five-pixel-wide C glyph. Its ink
        # lies above the arrow, so facing ink cannot satisfy the assertion.
        self.assertEqual(argus.crop(images[0], 52, 76, 5, 1),
                         bytes((114, 205, 232, 255) + public.INK * 3 + (114, 205, 232, 255)))
        self.golden("cellEleven", eleven)

    def test_supported_rotated_tank_height_bounds_arrow_length(self):
        result, manifest, images = self.accepted("rotatedTank")
        active = json.loads(self.cases["rotatedTank"]["rules"])
        tank = next(part for part in active["parts"] if part["id"] == "tank")
        self.assertEqual((tank["w"], tank["h"]), (1, 3))
        self.assertEqual(active["rotations"][1], {"dx": -1, "dy": 0})
        self.assertEqual(manifest["camera"]["cellPixels"], 11)
        fill = public.color(tank["color"])
        self.assertEqual(images[0].at(104, 111), fill)
        self.assertEqual(argus.crop(images[0], 103, 108, 9, 7),
                         argus.raster(TANK_ARROW, public.INK, fill))
        self.golden("rotatedTank", result)

    def test_accepted_thin_and_overcap_rules_preserve_error_stage_and_path(self):
        for key in ("thinWidth", "thinHeight"):
            with self.subTest(key=key):
                self.assert_only_error(self.observed(key), "render_resolution", "/options",
                    "The requested panel cannot fit every active grid cell.")
        self.assert_only_error(self.observed("gridOvercap"), "render_resource_limit", "/grid",
            "Active grid exceeds the renderer's operational cell limit.")

    def test_tenth_deck_uses_two_digits_in_filename_title_and_manifest(self):
        result, manifest, images = self.accepted("tenDecks")
        self.assertEqual(result["artifacts"][9]["filename"], "deck-10-level-10.png")
        self.assertEqual([deck["deckIndex"] for deck in manifest["decks"]], list(range(10)))
        self.assertEqual([deck["name"] for deck in manifest["decks"]],
                         ["Engineering", "Concourse", "Habitation"]
                         + [f"Level {level}" for level in range(4, 11)])
        glyphs = argus.DECK01_GLYPHS[:5] + (argus.DECK01_GLYPHS[6], argus.DECK01_GLYPHS[5])
        title = tuple(".".join(glyph[row] for glyph in glyphs) for row in range(7))
        self.assertEqual(argus.crop(images[9], 8, 10, 41, 7),
                         argus.raster(title, public.TITLE, public.BACKGROUND))
        self.golden("tenDecks", result, 9)

    def test_prefix_png_and_exact_total_byte_budgets(self):
        baseline, _, _ = self.accepted("budgetBaseline")
        self.assertEqual(sum(len(item["raw"]) for item in baseline["artifacts"]), 51016)
        self.assertEqual(sum(len(item["raw"]) for item in baseline["artifacts"][:2]), 24070)
        self.assert_only_error(self.observed("prefixPngBudget"), "render_resource_limit", "",
                              "Rendered output exceeds the operational byte limit.")
        exact, _, _ = self.accepted("exactTotalBudget")
        self.assertEqual(exact, baseline)
        self.assert_only_error(self.observed("oneByteShortBudget"), "render_resource_limit", "",
                              "Rendered output exceeds the operational byte limit.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--snapshot", action="store_true")
    parser.add_argument("--input-corpus", type=Path)
    parser.add_argument("--write-input-corpus", type=Path)
    args = parser.parse_args()
    public.DRIVER, public.SOURCE = args.driver, args.source
    cases = json.loads(args.input_corpus.read_bytes()) if args.input_corpus else expanded_requests(args.source)
    if args.write_input_corpus:
        args.write_input_corpus.write_text(public.compact(cases) + "\n")
    if args.snapshot:
        outputs = {}
        for key, request in cases.items():
            process = subprocess.run([str(args.driver)], input=public.compact(request) + "\n",
                                     text=True, encoding="utf-8", capture_output=True, check=True)
            outputs[key] = json.loads(process.stdout)
        print(public.compact(outputs), flush=True)
    else:
        unittest.main(argv=[__file__], verbosity=2)
