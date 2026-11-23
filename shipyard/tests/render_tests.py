"""Independent public-API renderer checks. No miniz or renderer internals used."""

import argparse
import copy
import hashlib
import io
import json
import re
import struct
import subprocess
import unittest
import zipfile
import zlib
from pathlib import Path


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compact(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def png_decode(data):
    """PNG chunk/CRC validation and the five standard scanline filters (RGBA8)."""
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature")
    cursor, chunks, compressed = 8, [], bytearray()
    while cursor < len(data):
        require(cursor + 12 <= len(data), "truncated PNG chunk header")
        length = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4 : cursor + 8]
        end = cursor + 12 + length
        require(end <= len(data), "truncated PNG payload")
        payload = data[cursor + 8 : end - 4]
        crc = struct.unpack_from(">I", data, end - 4)[0]
        require(zlib.crc32(kind + payload) == crc, f"PNG {kind!r} CRC")
        chunks.append(kind)
        if kind == b"IHDR":
            require(len(chunks) == 1 and length == 13, "single leading IHDR")
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", payload)
            )
            require(width > 0 and height > 0, "positive PNG dimensions")
            require(
                (depth, color, compression, filtering, interlace) == (8, 6, 0, 0, 0),
                "documented noninterlaced RGBA8 PNG",
            )
        elif kind == b"IDAT":
            require(chunks[0] == b"IHDR", "IDAT after IHDR")
            compressed.extend(payload)
        elif kind == b"IEND":
            require(length == 0 and end == len(data), "IEND and no trailing bytes")
        else:
            require(kind[0] & 32, "unknown critical PNG chunk")
        cursor = end
    require(chunks.count(b"IHDR") == chunks.count(b"IEND") == 1, "PNG endpoints")
    require(chunks[-1] == b"IEND" and b"IDAT" in chunks, "PNG chunk order")
    first, last = chunks.index(b"IDAT"), len(chunks) - 1 - chunks[::-1].index(b"IDAT")
    require(
        all(kind == b"IDAT" for kind in chunks[first : last + 1]), "consecutive IDAT"
    )
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed) + decoder.flush()
    require(
        decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail,
        "complete, unique zlib stream",
    )
    stride = width * 4
    require(len(raw) == height * (stride + 1), "decoded scanline byte count")
    pixels, previous = bytearray(), bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        filtering = raw[start]
        require(filtering <= 4, "PNG filter number")
        current = bytearray(raw[start + 1 : start + 1 + stride])
        if filtering:
            for column in range(stride):
                left = current[column - 4] if column >= 4 else 0
                above = previous[column]
                upper_left = previous[column - 4] if column >= 4 else 0
                if filtering == 1:
                    predictor = left
                elif filtering == 2:
                    predictor = above
                elif filtering == 3:
                    predictor = (left + above) // 2
                else:
                    base = left + above - upper_left
                    distances = (
                        abs(base - left),
                        abs(base - above),
                        abs(base - upper_left),
                    )
                    predictor = (left, above, upper_left)[
                        distances.index(min(distances))
                    ]
                current[column] = (current[column] + predictor) & 255
        pixels.extend(current)
        previous = current
    require(all(alpha == 255 for alpha in pixels[3::4]), "all PNG pixels opaque")
    return Image(width, height, bytes(pixels))


class Image:
    def __init__(self, width, height, pixels):
        self.width, self.height, self.pixels = width, height, pixels

    def at(self, x, y):
        require(0 <= x < self.width and 0 <= y < self.height, "pixel in bounds")
        start = 4 * (y * self.width + x)
        return tuple(self.pixels[start : start + 4])


def color(text):
    if re.fullmatch(r"#[0-9A-Fa-f]{6}", text):
        return (*bytes.fromhex(text[1:]), 255)
    return (128, 128, 128, 255)


BACKGROUND = (12, 22, 30, 255)
GRID = (18, 31, 40, 255)
BORDER = (43, 64, 77, 255)
INK = (8, 16, 24, 255)
TITLE = (230, 240, 245, 255)


class PublicRenderer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules_bytes = (SOURCE / "rules/apsis-rules-v4.json").read_bytes()
        cls.rules = json.loads(cls.rules_bytes)
        cls.call_count = 0

    @classmethod
    def tearDownClass(cls):
        print(f"{cls.__name__}: {cls.call_count} real public render calls", flush=True)

    def fixture(self, name):
        return json.loads(
            (SOURCE / f"fixtures/v3/apsis-{name}-definition-v3.json").read_bytes()
        )

    def request(self, design, **kwargs):
        return {
            "input": design if isinstance(design, str) else compact(design),
            **kwargs,
        }

    def calls(self, *requests):
        type(self).call_count += len(requests)
        process = subprocess.run(
            [str(DRIVER)],
            input="".join(compact(r) + "\n" for r in requests),
            text=True,
            encoding="utf-8",
            capture_output=True,
            timeout=90,
            check=False,
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        results = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual(len(results), len(requests))
        for result in results:
            for artifact in result["artifacts"]:
                artifact["raw"] = bytes.fromhex(artifact.pop("hex"))
        return results

    def render(self, design, **kwargs):
        return self.calls(self.request(design, **kwargs))[0]

    def passed(self, result):
        envelope = result["envelope"]
        self.assertTrue(envelope["pass"], compact(envelope["issues"]))
        self.assertEqual(envelope["status"], "PASS")
        self.assertEqual(envelope["schema"], "apsis-shipyard-result-v1")
        self.assertEqual(envelope["operation"], "render")
        return envelope

    def rejected(self, result, code=None):
        envelope = result["envelope"]
        self.assertFalse(envelope["pass"])
        self.assertEqual(envelope["status"], "FAIL")
        self.assertEqual(envelope["operation"], "render")
        self.assertIsNone(envelope["data"])
        self.assertEqual(result["artifacts"], [])
        self.assertTrue(envelope["issues"])
        self.assertTrue(any(i["severity"] == "error" for i in envelope["issues"]))
        if code:
            self.assertIn(code, [i["code"] for i in envelope["issues"]])

    def verify_bundle(self, result, design, options=None, rules_bytes=None):
        envelope = self.passed(result)
        rules_bytes = self.rules_bytes if rules_bytes is None else rules_bytes
        rules = json.loads(rules_bytes)
        options = {"width": 800, "height": 640, "labels": True, **(options or {})}
        blueprint = design.get("blueprint", design)
        decks = blueprint["decks"]
        artifacts = result["artifacts"]
        self.assertEqual(len(artifacts), len(decks) + 3)
        names = [a["filename"] for a in artifacts]
        self.assertEqual(names[-3:], ["stack.png", "manifest.json", "stack.zip"])
        self.assertEqual(len(set(names)), len(names))
        for index, name in enumerate(names[:-3], 1):
            self.assertRegex(name, rf"^deck-{index:02d}-[a-z0-9]+(?:-[a-z0-9]+)*\.png$")
            self.assertLessEqual(
                len(name.removeprefix(f"deck-{index:02d}-").removesuffix(".png")), 40
            )
        self.assertEqual(envelope["rulesVersion"], 4)
        self.assertEqual(envelope["rulesSha256"], sha(rules_bytes))
        metadata = []
        images = []
        for index, artifact in enumerate(artifacts):
            raw = artifact["raw"]
            self.assertEqual(artifact["sha256"], sha(raw))
            item = {
                "filename": artifact["filename"],
                "mediaType": artifact["mediaType"],
                "sha256": sha(raw),
                "bytes": len(raw),
            }
            if index <= len(decks):
                self.assertEqual(artifact["mediaType"], "image/png")
                image = png_decode(raw)
                expected_height = options["height"] * (
                    len(decks) if index == len(decks) else 1
                )
                self.assertEqual(
                    (image.width, image.height), (options["width"], expected_height)
                )
                self.assertEqual(
                    (artifact["width"], artifact["height"]), (image.width, image.height)
                )
                item.update(width=image.width, height=image.height)
                images.append(image)
            else:
                self.assertEqual((artifact["width"], artifact["height"]), (0, 0))
                self.assertEqual(
                    artifact["mediaType"],
                    "application/json"
                    if index == len(decks) + 1
                    else "application/zip",
                )
            metadata.append(item)
        self.assertEqual(envelope["data"]["artifacts"], metadata)
        self.assertEqual(
            images[-1].pixels, b"".join(image.pixels for image in images[:-1])
        )
        manifest_bytes = artifacts[-2]["raw"]
        manifest = json.loads(manifest_bytes)
        self.assertEqual(manifest_bytes, (compact(manifest) + "\n").encode())
        self.assertEqual(manifest["schema"], "apsis-shipyard-render-manifest-v1")
        self.assertEqual(manifest["rendererVersion"], "apsis-cpu-1")
        self.assertEqual(manifest["rulesVersion"], 4)
        self.assertEqual(manifest["rulesSha256"], sha(rules_bytes))
        self.assertEqual(
            manifest["definitionSha256"], sha(result["canonicalDefinition"].encode())
        )
        self.assertEqual(manifest["blueprintName"], blueprint["name"])
        self.assertEqual(manifest["fixedTimeSeconds"], 0)
        self.assertEqual(
            manifest["options"], {**options, "stackLayout": "vertical-blueprint-order"}
        )
        self.assertEqual(manifest["font"]["version"], "apsis-bitmap-5x7-v1")
        self.assertRegex(manifest["font"]["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(manifest["artifacts"], metadata[:-2])
        self.assertEqual(
            manifest["decks"],
            [
                {**metadata[i], "deckIndex": i, "name": deck["name"]}
                for i, deck in enumerate(decks)
            ],
        )
        w, h = options["width"], options["height"]
        gw, gh = rules["grid"]["w"], rules["grid"]["h"]
        cell = min((w - 32) // gw, (h - 48) // gh)
        self.assertEqual(
            manifest["camera"],
            {
                "cellPixels": cell,
                "gridWidth": gw,
                "gridHeight": gh,
                "originX": (w - gw * cell) // 2,
                "originY": 32 + (h - 32 - gh * cell) // 2,
                "positiveX": "right",
                "positiveY": "down",
                "projection": "top-down-full-grid",
            },
        )
        self.assertEqual(envelope["issues"], result["validation"]["issues"])
        zip_bytes = artifacts[-1]["raw"]
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as archive:
            self.assertEqual(archive.namelist(), names[:-1])
            self.assertEqual(archive.comment, b"")
            self.assertIsNone(archive.testzip())
            next_offset = 0
            for info, artifact in zip(archive.infolist(), artifacts[:-1], strict=True):
                self.assertEqual(
                    info.header_offset, next_offset, "physical ZIP entry order"
                )
                self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
                self.assertEqual(info.comment, b"")
                self.assertFalse(info.is_dir())
                self.assertFalse(info.flag_bits & 1, "ZIP must not be encrypted")
                self.assertEqual(info.file_size, len(artifact["raw"]))
                self.assertEqual(info.compress_size, info.file_size)
                self.assertEqual(info.CRC, zlib.crc32(artifact["raw"]))
                self.assertEqual(archive.read(info), artifact["raw"])
                # Cross-check local records as well as zipfile's central directory.
                fields = struct.unpack_from(
                    "<IHHHHHIIIHH", zip_bytes, info.header_offset
                )
                (
                    signature,
                    _,
                    flags,
                    method,
                    dos_time,
                    dos_date,
                    crc,
                    packed,
                    size,
                    nl,
                    el,
                ) = fields
                self.assertEqual(signature, 0x04034B50)
                self.assertEqual((flags & 1, method, dos_time, dos_date), (0, 0, 0, 33))
                self.assertEqual(flags, info.flag_bits)
                expected_sizes = (info.CRC, info.file_size, info.file_size)
                if flags & 8:
                    self.assertIn((crc, packed, size), ((0, 0, 0), expected_sizes))
                else:
                    self.assertEqual((crc, packed, size), expected_sizes)
                start = info.header_offset + 30
                self.assertEqual(zip_bytes[start : start + nl], info.filename.encode())
                start += nl + el
                self.assertEqual(
                    zip_bytes[start : start + info.compress_size], artifact["raw"]
                )
                next_offset = start + info.compress_size
                if flags & 8:
                    # Standard streaming ZIPs put CRC and lengths in a descriptor.
                    if zip_bytes[next_offset : next_offset + 4] == b"PK\x07\x08":
                        next_offset += 4
                    self.assertEqual(
                        struct.unpack_from("<III", zip_bytes, next_offset),
                        expected_sizes,
                    )
                    next_offset += 12
        eocd = zip_bytes.rfind(b"PK\x05\x06")
        self.assertEqual(eocd + 22, len(zip_bytes), "ZIP ends at uncommented EOCD")
        (
            _,
            disk,
            directory_disk,
            on_disk,
            count,
            directory_size,
            directory_offset,
            comment,
        ) = struct.unpack_from("<IHHHHIIH", zip_bytes, eocd)
        self.assertEqual(
            (disk, directory_disk, on_disk, count, comment),
            (0, 0, len(artifacts) - 1, len(artifacts) - 1, 0),
        )
        self.assertEqual(directory_offset, next_offset)
        self.assertEqual(directory_offset + directory_size, eocd)
        return manifest, images

    def station(self):
        # Broad connected public blueprint, with room for a rotated 2x3 cargo.
        modules = [
            {"id": 1, "type": "cockpit", "x": 3, "y": 3, "r": 0},
            {"id": 2, "type": "microReactor", "x": 5, "y": 3, "r": 0},
            {"id": 3, "type": "microLifeSupport", "x": 7, "y": 3, "r": 0},
            {"id": 4, "type": "airlock", "x": 2, "y": 5, "r": 1},
            {"id": 5, "type": "dockingPort", "x": 6, "y": 10, "r": 0},
            {"id": 6, "type": "cargo", "x": 7, "y": 6, "r": 0},
        ]
        return {
            "schema": "apsis-shipyard-v4",
            "name": "Pixel station",
            "buildType": "station",
            "budget": 100000,
            "loads": {"cargo": 0, "jumpFuel": 1, "sublightFuel": 1},
            "decks": [
                {
                    "name": "Pixels",
                    "modules": modules,
                    "floors": {
                        f"{x},{y}": True for y in range(2, 12) for x in range(2, 12)
                    },
                }
            ],
            "nextId": 7,
            "migrationNotes": [],
        }

    def hold_station(self, count=4, first=1, last=2):
        design = self.station()
        design["decks"][0]["modules"].pop()  # This fixture has no cargo module.
        design["nextId"] = 6
        floors = copy.deepcopy(design["decks"][0]["floors"])
        for d in range(1, count):
            design["decks"].append({"name": f"Normal deck {d+1}",
                                    "floors": copy.deepcopy(floors), "modules": []})
        for d, deck in enumerate(design["decks"]):
            deck["modules"].append({"id": design["nextId"], "type": "ladder",
                                    "x": 7, "y": 6, "r": 0})
            design["nextId"] += 1
            if d > 0:
                del deck["floors"]["9,6"]
        design["holds"] = [{"x": 8, "y": 5, "w": 3, "h": 4,
                            "firstDeck": first, "lastDeck": last}]
        return design


class Artifacts(PublicRenderer):
    def test_canonical_ships_and_station(self):
        for name in ("wren", "skiff", "kestrel-tender", "wayfarer-anchorage"):
            with self.subTest(name=name):
                design = self.fixture(name)
                first, second = self.calls(*[self.request(design, validate=True)] * 2)
                self.verify_bundle(first, design)
                self.assertEqual(first, second, "same-process repeated bytes/metadata")
                self.assertEqual(
                    first,
                    self.render(design, validate=True),
                    "new-process bytes/metadata",
                )
                if name == "wayfarer-anchorage":
                    self.assertIn(
                        "pending_F20", [i["code"] for i in first["envelope"]["issues"]]
                    )
                    self.assertNotIn("performance", first["envelope"]["data"])

    def test_sanitization_duplicates_and_deck_order(self):
        design = self.fixture("wayfarer-anchorage")
        names = ["../A / B...", "../A / B...", "星船"]
        for deck, name in zip(design["blueprint"]["decks"], names, strict=True):
            deck["name"] = name
        result = self.render(design, options={"labels": False}, validate=True)
        manifest, images = self.verify_bundle(result, design, {"labels": False})
        self.assertEqual(
            [d["filename"] for d in manifest["decks"]],
            ["deck-01-a-b.png", "deck-02-a-b.png", "deck-03-deck.png"],
        )
        # Reverse the source array, preserving each deck's actual content.
        reversed_design = copy.deepcopy(design)
        reversed_design["blueprint"]["decks"].reverse()
        reordered = self.render(
            reversed_design, options={"labels": False}, validate=True
        )
        _, reverse_images = self.verify_bundle(
            reordered, reversed_design, {"labels": False}
        )
        self.assertEqual(
            [i.pixels for i in reverse_images[:-1]], [i.pixels for i in images[-2::-1]]
        )
        long_name = self.fixture("wren")
        long_name["blueprint"]["decks"][0]["name"] = "X" * 48
        truncated = self.render(long_name, validate=True)
        self.verify_bundle(truncated, long_name)
        self.assertEqual(
            truncated["artifacts"][0]["filename"], "deck-01-" + "x" * 40 + ".png"
        )

    def test_call_order_options_and_cache_authority(self):
        design = self.fixture("wren")
        first, _, last = self.calls(
            self.request(design, validate=True),
            self.request(self.fixture("skiff"), options={"labels": False}),
            self.request(design, validate=True),
        )
        self.assertEqual(first, last)
        normalized = json.loads(first["canonicalDefinition"])
        normal = self.render(normalized, validate=True)
        misleading = copy.deepcopy(normalized)
        misleading["derived"] = {
            "cost": -999,
            "dryMass": 1e30,
            "performance": {"range": 999},
        }
        misleading["validation"] = {"ready": False, "jumpReady": False, "issues": []}
        misleading["rules"]["floor"]["color"] = "#ff0000"
        cached = self.render(misleading, validate=True)
        self.verify_bundle(cached, misleading)
        # Migration reports may record changed embedded rule identity. Pixels may not change.
        self.assertEqual(
            [a["raw"] for a in cached["artifacts"] if a["mediaType"] == "image/png"],
            [a["raw"] for a in normal["artifacts"] if a["mediaType"] == "image/png"],
        )
        no_labels = self.render(design, options={"labels": False}, validate=True)
        manifest, images = self.verify_bundle(no_labels, design, {"labels": False})
        titled = png_decode(first["artifacts"][0]["raw"])
        self.assertNotEqual(images[0].pixels, titled.pixels)
        self.assertIn(bytes(TITLE), titled.pixels[: 32 * titled.width * 4])
        self.assertNotIn(bytes(TITLE), images[0].pixels)
        self.assertEqual(
            manifest["font"], json.loads(first["artifacts"][-2]["raw"])["font"]
        )

    def test_documented_text_case_and_unicode_fallback(self):
        for first_name, second_name in (("alpha", "ALPHA"), ("星船", "??")):
            with self.subTest(first_name=first_name):
                first, second = self.station(), self.station()
                first["name"] = first["decks"][0]["name"] = first_name
                second["name"] = second["decks"][0]["name"] = second_name
                a, b = (
                    self.render(first, validate=True),
                    self.render(second, validate=True),
                )
                self.verify_bundle(a, first)
                self.verify_bundle(b, second)
                self.assertEqual(a["artifacts"][0]["raw"], b["artifacts"][0]["raw"])


class Geometry(PublicRenderer):
    def check_geometry(self, design, active, manifest, image, deck_index=0):
        camera = manifest["camera"]
        cell, ox, oy = camera["cellPixels"], camera["originX"], camera["originY"]
        gw, gh = active["grid"]["w"], active["grid"]["h"]
        self.assertGreaterEqual(cell, 8, "large-cell semantic probe fixture")
        self.assertEqual(image.at(0, 0), BACKGROUND)
        self.assertEqual(image.at(ox - 1, oy), BACKGROUND)
        self.assertEqual(image.at(ox + gw * cell, oy), BACKGROUND)
        self.assertEqual(image.at(ox, oy - 1), BACKGROUND)
        self.assertEqual(image.at(ox, oy + gh * cell), BACKGROUND)
        parts = {p["id"]: p for p in active["parts"]}
        deck = design.get("blueprint", design)["decks"][deck_index]
        occupied = {}
        for module in deck["modules"]:
            part = parts[module["type"]]
            direction = active["rotations"][module["r"]]
            width, height = part["w"], part["h"]
            if direction["dx"]:
                width, height = height, width
            x, y = module["x"], module["y"]
            for yy in range(y, y + height):
                for xx in range(x, x + width):
                    self.assertNotIn(
                        (xx, yy), occupied, "valid positive geometry control"
                    )
                    occupied[xx, yy] = part
            left, top, right, bottom = (
                ox + x * cell,
                oy + y * cell,
                ox + (x + width) * cell,
                oy + (y + height) * cell,
            )
            # The entire perimeter, not just an interior color, establishes footprint extent.
            for xx in range(left, right):
                self.assertEqual(image.at(xx, top), BORDER)
                self.assertEqual(image.at(xx, bottom - 1), BORDER)
            for yy in range(top, bottom):
                self.assertEqual(image.at(left, yy), BORDER)
                self.assertEqual(image.at(right - 1, yy), BORDER)
            self.assertEqual(image.at(left + 2, top + 2), color(part["color"]))
            # The documented facing arrow must have ink inside this module.
            ink = [
                (xx, yy)
                for yy in range(top + 1, bottom - 1)
                for xx in range(left + 1, right - 1)
                if image.at(xx, yy) == INK
            ]
            self.assertTrue(ink)
            self.assertGreater(
                sum(
                    (xx - (left + right - 1) / 2) * direction["dx"]
                    + (yy - (top + bottom - 1) / 2) * direction["dy"]
                    for xx, yy in ink
                ),
                0,
                "arrow ink points in active facing direction",
            )
        # Probe every grid cell away from borders and central arrows. Empty grid,
        # floor-only cells, and occupied cells all have independently known owners.
        for y in range(gh):
            for x in range(gw):
                expected = (
                    color(occupied[x, y]["color"])
                    if (x, y) in occupied
                    else (
                        color(active["floor"]["color"])
                        if deck["floors"].get(f"{x},{y}")
                        else GRID
                    )
                )
                allowed = (expected, INK) if (x, y) in occupied else (expected,)
                self.assertIn(
                    image.at(ox + x * cell + 2, oy + y * cell + 2),
                    allowed,
                    f"semantic cell ({x},{y}), including documented arrow overlay",
                )
                if (x, y) not in occupied and deck["floors"].get(f"{x},{y}"):
                    self.assertEqual(image.at(ox + x * cell, oy + y * cell), GRID)

    def test_canonical_geometry(self):
        for name in ("wren", "skiff", "kestrel-tender", "wayfarer-anchorage"):
            design = self.fixture(name)
            result = self.render(design, options={"labels": False}, validate=True)
            manifest, images = self.verify_bundle(result, design, {"labels": False})
            for index, image in enumerate(images[:-1]):
                with self.subTest(name=name, deck=index):
                    self.check_geometry(design, self.rules, manifest, image, index)

    def test_active_rule_colors_dimensions_rotations_and_grid(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                design = self.station()
                design["decks"][0]["modules"][-1]["r"] = rotation
                result = self.render(design, options={"labels": False}, validate=True)
                manifest, images = self.verify_bundle(result, design, {"labels": False})
                self.check_geometry(design, self.rules, manifest, images[0])
        # Remap rotation indices and move the camera through public rules loading.
        active = copy.deepcopy(self.rules)
        active["grid"] = {"w": 30, "h": 28}
        active["floor"]["color"] = "#103b57"
        for part in active["parts"]:
            if part["id"] == "cargo":
                part.update(w=2, h=4, color="#aB12eF")
        active["rotations"] = active["rotations"][1:] + active["rotations"][:1]
        design = self.station()
        for module in design["decks"][0]["modules"]:
            module["r"] = (
                module["r"] - 1
            ) % 4  # Preserve each intended physical direction.
        for presentation_color in ("#aB12eF", "named-color"):
            with self.subTest(color=presentation_color):
                next(p for p in active["parts"] if p["id"] == "cargo")["color"] = (
                    presentation_color
                )
                rules_bytes = compact(active).encode()
                result = self.render(
                    design,
                    options={"labels": False},
                    rules=rules_bytes.decode(),
                    validate=True,
                )
                manifest, images = self.verify_bundle(
                    result, design, {"labels": False}, rules_bytes
                )
                self.check_geometry(design, active, manifest, images[0])

    def test_small_cells_and_panel_boundaries(self):
        design = self.fixture("skiff")
        for cell, width, height in (
            (1, 64, 72),
            (2, 84, 96),
            (3, 110, 120),
            (4, 136, 144),
        ):
            with self.subTest(cell=cell):
                options = {"width": width, "height": height, "labels": False}
                result = self.render(design, options=options, validate=True)
                manifest, images = self.verify_bundle(result, design, options)
                self.assertEqual(manifest["camera"]["cellPixels"], cell)
                # Every non-background pixel stays inside the documented full grid.
                camera = manifest["camera"]
                for y in range(height):
                    for x in range(width):
                        if not (
                            camera["originX"] <= x < camera["originX"] + 26 * cell
                            and camera["originY"] <= y < camera["originY"] + 24 * cell
                        ):
                            self.assertEqual(images[0].at(x, y), BACKGROUND)


    def test_luna_hold_span_manifest_border_labels_and_unchanged_other_decks(self):
        # Luna, under owner delegation 2026-11-23: one object, normal deck indices.
        design = self.hold_station()
        plain = copy.deepcopy(design)
        del plain["holds"]
        no_labels = {"labels": False}
        result = self.render(design, options=no_labels, validate=True)
        manifest, images = self.verify_bundle(result, design, no_labels)
        baseline = self.render(plain, options=no_labels, validate=True)
        _, ordinary = self.verify_bundle(baseline, plain, no_labels)
        self.assertEqual(manifest["holds"], [{**design["holds"][0], "label": "decks 2–3"}])
        self.assertEqual([d["deckIndex"] for d in manifest["decks"]], [0, 1, 2, 3])
        self.assertEqual([d["name"] for d in manifest["decks"]],
                         [d["name"] for d in design["decks"]])
        camera = manifest["camera"]
        cell, ox, oy = camera["cellPixels"], camera["originX"], camera["originY"]
        x, y = ox + 8 * cell, oy + 5 * cell
        right, bottom = x + 3 * cell - 1, y + 4 * cell - 1
        for d in (1, 2):
            for px, py in ((x, y), (right, y), (right, bottom), (x, bottom),
                           (x + cell, y), (x, y + cell)):
                self.assertEqual(images[d].at(px, py), BORDER)
            self.assertNotEqual(images[d].pixels, ordinary[d].pixels)
        for d in (0, 3):
            self.assertEqual(images[d].pixels, ordinary[d].pixels,
                             "Holds affect neither deck before first nor after last")
        titled = self.render(design, validate=True)
        labelled, title_images = self.verify_bundle(titled, design)
        self.assertEqual(labelled["holds"], manifest["holds"])
        for d in (1, 2):
            ink_pixels = sum(title_images[d].at(px, py) == TITLE
                             for py in range(y + 2, y + 9)
                             for px in range(x + 2, right - 1))
            self.assertGreater(ink_pixels, 0, "Real held panels carry the span label")
            self.assertFalse(any(images[d].at(px, py) == TITLE
                                 for py in range(y + 2, y + 9)
                                 for px in range(x + 2, right - 1)))
        empty = copy.deepcopy(plain)
        empty["holds"] = []
        empty_result = self.render(empty, options=no_labels, validate=True)
        self.assertEqual([a for a in empty_result["artifacts"] if a["mediaType"] == "image/png"],
                         [a for a in baseline["artifacts"] if a["mediaType"] == "image/png"],
                         "Empty hold metadata retains byte-identical deck and stack PNGs")

    def test_compact_hold_label_exact_digits_hyphen_and_endpoint_bitmap(self):
        # Literal reviewed 5x7 glyph expectations: no production font/helper is imported.
        glyphs = {
            "H": (17, 17, 17, 31, 17, 17, 17),
            "O": (14, 17, 17, 17, 17, 17, 14),
            "L": (16, 16, 16, 16, 16, 16, 31),
            "D": (30, 17, 17, 17, 17, 17, 30),
            " ": (0, 0, 0, 0, 0, 0, 0),
            "0": (14, 17, 19, 21, 25, 17, 14),
            "2": (14, 17, 1, 2, 4, 8, 31),
            "-": (0, 0, 0, 31, 0, 0, 0),
            "4": (2, 6, 10, 18, 31, 2, 2),
        }
        design = self.hold_station(count=4, first=1, last=3)
        titled = self.render(design, validate=True)
        manifest, images = self.verify_bundle(titled, design)
        plain = self.render(design, options={"labels": False}, validate=True)
        _, unlabelled = self.verify_bundle(plain, design, {"labels": False})
        self.assertEqual(manifest["holds"], [{**design["holds"][0], "label": "decks 2–4"}])
        camera = manifest["camera"]
        x = camera["originX"] + 8 * camera["cellPixels"] + 2
        y = camera["originY"] + 5 * camera["cellPixels"] + 2
        text = "HOLD 02-04"
        for deck in (1, 2, 3):
            for row in range(7):
                for index, char in enumerate(text):
                    for column in range(6):
                        ink = column < 5 and bool(glyphs[char][row] & (1 << (4-column)))
                        px, py = x + 6 * index + column, y + row
                        expected = TITLE if ink else unlabelled[deck].at(px, py)
                        self.assertEqual(images[deck].at(px, py), expected,
                                         (deck, text, row, index, column))


class Bounds(PublicRenderer):
    def test_rustbucket_rejects_and_empty_holds_are_noop(self):
        rustbucket = self.render(self.fixture("rustbucket"), validate=True)
        self.rejected(rustbucket, "missing_airlock")
        self.assertEqual(
            rustbucket["envelope"]["issues"], rustbucket["validation"]["issues"]
        )
        unreachable = [
            i
            for i in rustbucket["envelope"]["issues"]
            if i["code"] == "unreachable_component"
        ]
        self.assertEqual(len(unreachable), 3)
        self.assertEqual({i["details"]["moduleId"] for i in unreachable}, {1, 5, 6})
        self.assertTrue(all(i["severity"] == "error" for i in unreachable))
        holds = self.station()
        holds["holds"] = []
        empty = self.render(holds, validate=True)
        self.verify_bundle(empty, holds)
        plain = self.render(self.station(), validate=True)
        self.assertEqual([a for a in empty["artifacts"] if a["mediaType"] == "image/png"],
                         [a for a in plain["artifacts"] if a["mediaType"] == "image/png"])
        self.assertFalse(any(i["code"].startswith("pending_F1")
                             for i in empty["envelope"]["issues"]))
        holds["holds"] = [{"x": 0, "y": 0, "w": 1, "h": 1,
                            "firstDeck": 0, "lastDeck": 0}]
        self.rejected(self.render(holds), "hold_span")

    def test_combined_module_hold_work_bound_including_real_nested_craft(self):
        options = {"width": 320, "height": 320, "labels": False}
        design = self.hold_station(count=3, first=0, last=2)
        modules = sum(len(deck["modules"]) for deck in design["decks"])
        design["holds"] *= 4096 - modules
        exact = self.render(design, options=options, validate=True)
        self.passed(exact)
        self.assertEqual(len(json.loads(exact["artifacts"][-2]["raw"])["holds"]), 4096-modules)
        design["holds"].append(copy.deepcopy(design["holds"][0]))
        self.rejected(self.render(design, options=options), "render_resource_limit")

        # A real 3x5-cell, two-deck craft fits the catalogue shuttle berth.
        child = {"schema": "apsis-shipyard-v4", "name": "Small hold craft",
                 "buildType": "ship", "budget": 100000,
                 "loads": {"cargo": 0, "jumpFuel": 1, "sublightFuel": 1},
                 "nextId": 8, "migrationNotes": [], "decks": []}
        floors = {f"{x},{y}": True for x in range(3) for y in range(5)}
        modules = [{"id": 1, "type": "cockpit", "x": 0, "y": 0, "r": 0},
                   {"id": 2, "type": "microReactor", "x": 1, "y": 0, "r": 0},
                   {"id": 3, "type": "microLifeSupport", "x": 2, "y": 0, "r": 0},
                   {"id": 4, "type": "airlock", "x": 0, "y": 2, "r": 1},
                   {"id": 5, "type": "microEngine", "x": 2, "y": 3, "r": 0},
                   {"id": 6, "type": "ladder", "x": 1, "y": 2, "r": 0}]
        child["decks"].append({"name": "Bottom", "floors": floors, "modules": modules})
        upper = copy.deepcopy(floors)
        del upper["2,1"]
        child["decks"].append({"name": "Upper", "floors": upper,
                               "modules": [{"id": 7, "type": "ladder", "x": 1, "y": 2, "r": 0}]})
        nested = self.station()
        nested["decks"][0]["modules"].pop()
        nested["decks"][0]["modules"].append({"id": 6, "type": "shuttleDock", "x": 12,
                                              "y": 5, "r": 0, "craft": child})
        nested["nextId"] = 7
        nested["decks"][0]["floors"].update({f"{x},{y}": True
                                               for x in range(12, 16) for y in range(5, 11)})
        count = 6 + 7
        child["holds"] = [{"x": 1, "y": 1, "w": 2, "h": 2,
                            "firstDeck": 0, "lastDeck": 1}] * (4096-count)
        self.passed(self.render(nested, options=options, validate=True))
        child["holds"].append(copy.deepcopy(child["holds"][0]))
        self.rejected(self.render(nested, options=options), "render_resource_limit")

    def test_option_and_limit_rejections(self):
        design = self.fixture("wren")
        for field in ("width", "height"):
            for value in (0, 63, 2049, 4294967295):
                with self.subTest(field=field, value=value):
                    self.rejected(self.render(design, options={field: value}))
        self.rejected(
            self.render(design, options={"width": 64, "height": 64}),
            "render_resolution",
        )
        for field, maximum in (
            ("maxDecks", 16),
            ("maxDimension", 8192),
            ("maxTotalPixels", 16777216),
            ("maxOutputBytes", 67108864),
        ):
            with self.subTest(limit=field):
                self.rejected(self.render(design, limits={field: maximum + 1}))
                self.rejected(self.render(design, limits={field: 0}))
        self.rejected(self.render(design, limits={"maxDimension": 799}))
        station = self.fixture("wayfarer-anchorage")
        self.rejected(self.render(station, limits={"maxDecks": 2}))
        self.rejected(self.render(station, options={"width": 2048, "height": 1366}))
        # Independently lower the image-dimension bound below only the stack height.
        self.rejected(
            self.render(
                station,
                options={"width": 100, "height": 200},
                limits={"maxDimension": 599},
            )
        )

    def test_pixel_output_and_input_exact_boundaries(self):
        design = self.fixture("wren")
        baseline = self.render(design, validate=True)
        output_size = sum(len(a["raw"]) for a in baseline["artifacts"])
        for budget in (
            1,
            len(baseline["artifacts"][0]["raw"]) - 1,
            sum(len(a["raw"]) for a in baseline["artifacts"][:-1]),
        ):
            with self.subTest(partial_output_budget=budget):
                self.rejected(self.render(design, limits={"maxOutputBytes": budget}))
        pixels = 2 * 800 * 640
        for field, exact in (
            ("maxOutputBytes", output_size),
            ("maxTotalPixels", pixels),
            ("maxDimension", 800),
            ("maxDecks", 1),
        ):
            with self.subTest(limit=field):
                accepted = self.render(design, limits={field: exact}, validate=True)
                self.assertEqual(accepted, baseline)
                self.rejected(self.render(design, limits={field: exact - 1}))
        source = compact(design)
        self.assertEqual(
            self.render(
                source, inputLimits={"maxBytes": len(source.encode())}, validate=True
            ),
            baseline,
        )
        self.rejected(
            self.render(source, inputLimits={"maxBytes": len(source.encode()) - 1})
        )
        self.rejected(self.render(source, inputLimits={"maxNesting": 1}))
        for source in (
            '{"schema":"apsis-shipyard-v4","schema":"apsis-shipyard-v4"}',
            "{",
            "{}",
        ):
            self.rejected(self.render(source))
        station = self.fixture("wayfarer-anchorage")
        options = {"width": 2048, "height": 1365, "labels": False}
        near_cap = self.render(station, options=options, validate=True)
        self.verify_bundle(near_cap, station, options)
        self.assertEqual(2 * 3 * options["width"] * options["height"], 16773120)
        maximum = {"width": 2048, "height": 2048, "labels": False}
        self.verify_bundle(
            self.render(design, options=maximum, validate=True), design, maximum
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument(
        "--suite", choices=("Artifacts", "Geometry", "Bounds"), required=True
    )
    args = parser.parse_args()
    DRIVER, SOURCE = args.driver, args.source
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(globals()[args.suite])
    raise SystemExit(
        not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful()
    )
