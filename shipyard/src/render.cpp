#include <apsis/shipyard/render.hpp>
#include <apsis/shipyard/validator.hpp>
#include "internal.hpp"
#include "holds.hpp"
#include "render_font.hpp"

#include <miniz.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <new>

namespace apsis::shipyard {
namespace {
using Color = std::array<std::uint8_t, 4>;
constexpr Color background{12, 22, 30, 255}, grid_background{18, 31, 40, 255};
constexpr Color border{43, 64, 77, 255}, ink{230, 240, 245, 255};
constexpr Color dark_ink{8, 16, 24, 255};
constexpr RenderLimits hard_limits{};
constexpr std::size_t max_input_modules = 4096, max_input_floors = 262144;
constexpr std::uint64_t max_grid_cells = 262144;

struct RenderFailure { Issue issue; };
[[noreturn]] void fail(std::string code, std::string message, std::string path = "") {
    throw RenderFailure{detail::error(std::move(code), std::move(message), std::move(path))};
}

std::string sha256(const void* bytes, std::size_t length) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    if (EVP_Digest(bytes, length, digest.data(), &size, EVP_sha256(), nullptr) != 1)
        fail("render_encoding", "Could not hash a render artifact.");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned int i = 0; i < size; ++i) {
        result += hex[digest[i] >> 4];
        result += hex[digest[i] & 15];
    }
    return result;
}

std::string font_hash() {
    std::vector<std::uint8_t> asset(detail::render_glyphs.begin(), detail::render_glyphs.end());
    for (const auto& glyph : detail::render_font) asset.insert(asset.end(), glyph.begin(), glyph.end());
    return sha256(asset.data(), asset.size());
}

Color rule_color(const Json& value) {
    const auto text = value.get<std::string>();
    if (text.size() != 7 || text.front() != '#') return {128, 128, 128, 255};
    Color color{0, 0, 0, 255};
    for (std::size_t i = 0; i < 3; ++i) {
        unsigned int channel = 0;
        const auto start = text.data() + 1 + i * 2;
        const auto parsed = std::from_chars(start, start + 2, channel, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != start + 2) return {128, 128, 128, 255};
        color[i] = static_cast<std::uint8_t>(channel);
    }
    return color;
}

std::string number(std::size_t n) {
    auto result = std::to_string(n);
    if (result.size() < 2) result.insert(result.begin(), '0');
    return result;
}

std::string slug(std::string_view name) {
    std::string result;
    for (const unsigned char c : name) {
        if (result.size() >= 40) break;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) result += static_cast<char>(c);
        else if (c >= 'A' && c <= 'Z') result += static_cast<char>(c - 'A' + 'a');
        else if (!result.empty() && result.back() != '-') result += '-';
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result.empty() ? "deck" : result;
}

struct Canvas {
    int width, height;
    std::vector<std::uint8_t> pixels;
    Canvas(std::uint32_t w, std::uint32_t h)
        : width(static_cast<int>(w)), height(static_cast<int>(h)),
          pixels(static_cast<std::size_t>(w) * h * 4) {
        rect(0, 0, width, height, background);
    }
    void rect(int x, int y, int w, int h, Color color) {
        const int right = std::clamp(x + w, 0, width), bottom = std::clamp(y + h, 0, height);
        for (int py = std::max(0, y); py < bottom; ++py)
            for (int px = std::max(0, x); px < right; ++px) {
                const auto offset = (static_cast<std::size_t>(py) * width + px) * 4;
                std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
    }
    void line(int x, int y, int end_x, int end_y, Color color) {
        // Arrow shafts are cardinal and their wings are exactly 45 degrees.
        const int sx = std::clamp(end_x - x, -1, 1);
        const int sy = std::clamp(end_y - y, -1, 1);
        const auto steps = static_cast<std::size_t>(
            std::max(std::abs(end_x - x), std::abs(end_y - y)));
        for (std::size_t step = 0; step <= steps; ++step) {
            rect(x, y, 1, 1, color);
            x += sx;
            y += sy;
        }
    }
    void text(int x, int y, std::string_view text, int max_width, Color color) {
        const int end = x + max_width;
        for (unsigned char c : text) {
            if ((c & 0xc0) == 0x80) continue;
            if (x + 5 > end) break;
            if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
            auto glyph = detail::render_glyphs.find(static_cast<char>(c));
            if (glyph == std::string_view::npos) glyph = detail::render_glyphs.find('?');
            for (int row = 0; row < 7; ++row) {
                auto bits = static_cast<unsigned int>(detail::render_font[glyph][row]);
                while (bits != 0) {
                    const int col = 4 - std::countr_zero(bits);
                    rect(x + col, y + row, 1, 1, color);
                    bits &= bits - 1U;
                }
            }
            x += 6;
        }
    }
};

struct Camera {
    int x, y, cell, grid_width, grid_height;
    Json json() const {
        return {{"projection", "top-down-full-grid"}, {"originX", x}, {"originY", y},
            {"cellPixels", cell}, {"gridWidth", grid_width}, {"gridHeight", grid_height},
            {"positiveX", "right"}, {"positiveY", "down"}};
    }
};

Camera camera(const Json& rules, const RenderOptions& options) {
    const auto gw = rules.at("grid").at("w").get<std::uint64_t>();
    const auto gh = rules.at("grid").at("h").get<std::uint64_t>();
    if (gw > max_grid_cells || gh > max_grid_cells || gw * gh > max_grid_cells)
        fail("render_resource_limit", "Active grid exceeds the renderer's operational cell limit.", "/grid");
    const int width = static_cast<int>(options.width), height = static_cast<int>(options.height);
    const int cell = std::min((width - 32) / static_cast<int>(gw),
                              (height - 48) / static_cast<int>(gh));
    if (cell < 1) fail("render_resolution", "The requested panel cannot fit every active grid cell.", "/options");
    return {(width - static_cast<int>(gw) * cell) / 2,
        32 + (height - 32 - static_cast<int>(gh) * cell) / 2,
        cell, static_cast<int>(gw), static_cast<int>(gh)};
}

void check_input_work(const Json& input) {
    const Json* blueprint = &input;
    if (input.is_object() && input.contains("blueprint")) blueprint = &input.at("blueprint");
    std::vector<const Json*> pending{blueprint};
    std::size_t modules = 0, floors = 0;
    while (!pending.empty()) {
        const auto& current = *pending.back();
        pending.pop_back();
        if (!current.is_object() || !current.contains("decks") || !current.at("decks").is_array()) continue;
        if (const auto holds = current.find("holds"); holds != current.end() && holds->is_array()) {
            modules += holds->size();
            if (modules > max_input_modules)
                fail("render_resource_limit", "Input exceeds the renderer's operational module/hold-entry limit.");
        }
        for (const auto& deck : current.at("decks")) {
            if (!deck.is_object()) continue;
            if (deck.contains("floors") && deck.at("floors").is_object()) {
                floors += deck.at("floors").size();
                if (floors > max_input_floors)
                    fail("render_resource_limit", "Input exceeds the renderer's operational floor-entry limit.");
            }
            if (!deck.contains("modules") || !deck.at("modules").is_array()) continue;
            modules += deck.at("modules").size();
            if (modules > max_input_modules)
                fail("render_resource_limit", "Input exceeds the renderer's operational module-entry limit.");
            for (const auto& module : deck.at("modules"))
                if (module.is_object() && module.contains("craft")) pending.push_back(&module.at("craft"));
        }
    }
}

Canvas draw_deck(const Json& blueprint, std::size_t index, const Json& rules,
                 const RenderOptions& options, const Camera& view) {
    const auto& deck = blueprint.at("decks").at(index);
    Canvas canvas(options.width, options.height);
    canvas.rect(view.x, view.y, view.grid_width * view.cell, view.grid_height * view.cell, grid_background);
    if (options.labels)
        canvas.text(8, 10, "DECK " + number(index + 1) + ": " + deck.at("name").get<std::string>(),
                    canvas.width - 16, ink);
    const auto floor_color = rule_color(rules.at("floor").at("color"));
    for (const auto& [key, unused] : deck.at("floors").items()) {
        (void)unused;
        int x = 0, y = 0;
        const auto comma = key.find(',');
        std::from_chars(key.data(), key.data() + comma, x);
        std::from_chars(key.data() + comma + 1, key.data() + key.size(), y);
        const int inset = view.cell >= 3 ? 1 : 0;
        canvas.rect(view.x + x * view.cell + inset, view.y + y * view.cell + inset,
                    view.cell - inset, view.cell - inset, floor_color);
    }
    if (const auto holds = blueprint.find("holds"); holds != blueprint.end()) {
        for (const auto& hold : *holds) {
            if (index < hold.at("firstDeck").get<std::size_t>() ||
                index > hold.at("lastDeck").get<std::size_t>()) continue;
            const int x = view.x + hold.at("x").get<int>() * view.cell;
            const int y = view.y + hold.at("y").get<int>() * view.cell;
            const int right = x + hold.at("w").get<int>() * view.cell - 1;
            const int bottom = y + hold.at("h").get<int>() * view.cell - 1;
            canvas.line(x, y, right, y, border);
            canvas.line(right, y, right, bottom, border);
            canvas.line(right, bottom, x, bottom, border);
            canvas.line(x, bottom, x, y, border);
            if (options.labels)
                canvas.text(x + 2, y + 2, "HOLD " + detail::hold_label(hold), right - x - 3, ink);
        }
    }
    std::map<std::string, const Json*, std::less<>> catalogue;
    for (const auto& part : rules.at("parts")) catalogue.emplace(part.at("id").get<std::string>(), &part);
    for (const auto& module : deck.at("modules")) {
        const auto& part = *catalogue.at(module.at("type").get<std::string>());
        const auto& direction = rules.at("rotations").at(module.at("r").get<std::size_t>());
        const int dx = direction.at("dx"), dy = direction.at("dy");
        const int w = part.at(dx == 0 ? "w" : "h").get<int>() * view.cell;
        const int h = part.at(dx == 0 ? "h" : "w").get<int>() * view.cell;
        const int x = view.x + module.at("x").get<int>() * view.cell;
        const int y = view.y + module.at("y").get<int>() * view.cell;
        canvas.rect(x, y, w, h, border);
        const int inset = view.cell >= 3 ? 1 : 0;
        canvas.rect(x + inset, y + inset, w - 2 * inset, h - 2 * inset, rule_color(part.at("color")));
        if (options.labels && w >= 11 && h >= 11)
            canvas.text(x + 3, y + 3, part.at("id").get_ref<const std::string&>(), w - 6, dark_ink);
        if (view.cell >= 4) {
            const int cx = x + w / 2, cy = y + h / 2;
            const int length = std::max(1, std::min({view.cell / 2, w / 3, h / 3}));
            const int tx = cx + dx * length, ty = cy + dy * length;
            canvas.line(cx, cy, tx, ty, dark_ink);
            const int wing = std::min(2, std::min(w, h) / 2 - 1);
            canvas.line(tx, ty, tx - dx * wing + dy * wing, ty - dy * wing - dx * wing, dark_ink);
            canvas.line(tx, ty, tx - dx * wing - dy * wing, ty - dy * wing + dx * wing, dark_ink);
        }
    }
    return canvas;
}

RenderArtifact png(const Canvas& canvas, std::string filename) {
    std::size_t size = 0;
    std::unique_ptr<void, decltype(&mz_free)> encoded(
        tdefl_write_image_to_png_file_in_memory_ex(canvas.pixels.data(), canvas.width,
            canvas.height, 4, &size, 6, MZ_FALSE), &mz_free);
    if (!encoded) fail("render_encoding", "Could not encode a PNG.");
    const auto* first = static_cast<const std::uint8_t*>(encoded.get());
    return {std::move(filename), "image/png", {first, first + size},
        sha256(first, size), static_cast<std::uint32_t>(canvas.width), static_cast<std::uint32_t>(canvas.height)};
}

Json metadata(const RenderArtifact& artifact) {
    Json result = {{"filename", artifact.filename}, {"mediaType", artifact.mediaType},
        {"bytes", artifact.bytes.size()}, {"sha256", artifact.sha256}};
    if (artifact.width) { result["width"] = artifact.width; result["height"] = artifact.height; }
    return result;
}

struct ZipOutput {
    std::vector<std::uint8_t> bytes;
    std::size_t limit;
    bool limited = false;
};
std::size_t zip_write(void* opaque, mz_uint64 offset, const void* data, std::size_t size) {
    auto& out = *static_cast<ZipOutput*>(opaque);
    const auto bounded_offset = std::min<mz_uint64>(offset, out.limit);
    const auto bounded_size = std::min(size, out.limit);
    if (offset != bounded_offset || size != bounded_size ||
        bounded_offset + bounded_size > out.limit) {
        out.limited = true; return 0;
    }
    try {
        const auto begin = static_cast<std::size_t>(offset);
        out.bytes.resize(std::max(out.bytes.size(), begin + size));
        std::copy_n(static_cast<const std::uint8_t*>(data), size, out.bytes.begin() + static_cast<std::ptrdiff_t>(begin));
    } catch (const std::bad_alloc&) { out.limited = true; return 0; }
    return size;
}
struct ZipWriter {
    mz_zip_archive archive{};
    bool initialized = false;
    ~ZipWriter() { if (initialized) mz_zip_writer_end(&archive); }
};

RenderArtifact zip(const std::vector<RenderArtifact>& artifacts, std::size_t byte_limit) {
    ZipOutput output{{}, byte_limit, false};
    ZipWriter writer;
    writer.archive.m_pWrite = zip_write;
    writer.archive.m_pIO_opaque = &output;
    writer.initialized = mz_zip_writer_init(&writer.archive, 0) == MZ_TRUE;
    bool okay = writer.initialized;
    for (const auto& artifact : artifacts) {
        if (!okay) break;
        okay = mz_zip_writer_add_mem(&writer.archive, artifact.filename.c_str(),
            artifact.bytes.data(), artifact.bytes.size(), 0) == MZ_TRUE;
    }
    if (okay) okay = mz_zip_writer_finalize_archive(&writer.archive) == MZ_TRUE;
    if (!okay) {
        if (output.limited) fail("render_resource_limit", "Rendered output exceeds the operational byte limit.");
        fail("render_encoding", "Could not encode the stack ZIP.");
    }
    auto hash = sha256(output.bytes.data(), output.bytes.size());
    return {"stack.zip", "application/zip", std::move(output.bytes), std::move(hash)};
}

RenderResult failed(const RulesSnapshot& rules, std::vector<Issue> issues) {
    sort_issues(issues);
    return {{{"schema", "apsis-shipyard-result-v1"}, {"operation", "render"},
        {"status", "FAIL"}, {"pass", false}, {"rulesVersion", rules.version()},
        {"rulesSha256", rules.sha256()}, {"issues", std::move(issues)}, {"data", nullptr}}, {}};
}
} // namespace

RenderResult render(std::string_view bytes, const RulesSnapshot& rules,
                    const SchemaSet& schemas, RenderOptions options,
                    RenderLimits limits, InputLimits input_limits) {
    try {
        if (options.width < 64 || options.height < 64 || options.width > 2048 || options.height > 2048)
            fail("invalid_render_options", "Deck dimensions must each be between 64 and 2048 pixels.", "/options");
        if (limits.maxDecks == 0 || limits.maxDecks > hard_limits.maxDecks ||
            limits.maxDimension == 0 || limits.maxDimension > hard_limits.maxDimension ||
            limits.maxTotalPixels == 0 || limits.maxTotalPixels > hard_limits.maxTotalPixels ||
            limits.maxOutputBytes == 0 || limits.maxOutputBytes > hard_limits.maxOutputBytes)
            fail("invalid_render_limits", "Render limits must be positive and cannot exceed the documented hard limits.", "/limits");
        const auto view = camera(rules.data(), options);
        auto parsed = parse_json(bytes, input_limits);
        if (!parsed.ok()) return failed(rules, std::move(parsed.issues));
        check_input_work(*parsed.value);
        const auto& raw_input = *parsed.value;
        const auto& raw_blueprint = raw_input.is_object() && raw_input.contains("blueprint")
            ? raw_input.at("blueprint") : raw_input;
        if (raw_blueprint.is_object() && raw_blueprint.contains("decks") &&
            raw_blueprint.at("decks").is_array() && raw_blueprint.at("decks").size() > limits.maxDecks)
            fail("render_resource_limit", "Deck count exceeds the operational render limit.");
        auto checked = validate(bytes, rules, schemas, {}, input_limits);
        checked["operation"] = "render";
        if (!checked.at("pass").get<bool>()) { checked["data"] = nullptr; return {std::move(checked), {}}; }
        const auto& definition = checked.at("data").at("definition");
        const auto& blueprint = definition.at("blueprint");
        const auto& decks = blueprint.at("decks");
        const auto count = decks.size();
        const std::uint64_t stack_height = static_cast<std::uint64_t>(options.height) * count;
        const std::uint64_t total_pixels = static_cast<std::uint64_t>(options.width) * stack_height * 2;
        if (count > limits.maxDecks || options.width > limits.maxDimension ||
            stack_height > limits.maxDimension || total_pixels > limits.maxTotalPixels)
            fail("render_resource_limit", "Deck count, dimensions, or total pixels exceed operational render limits.", "/options");
        const auto normalized = canonical_json(definition);
        Json manifest = {{"schema", "apsis-shipyard-render-manifest-v1"},
            {"rendererVersion", renderer_version}, {"rulesVersion", rules.version()},
            {"rulesSha256", rules.sha256()}, {"definitionSha256", sha256(normalized.data(), normalized.size())},
            {"blueprintName", blueprint.at("name")}, {"camera", view.json()}, {"fixedTimeSeconds", 0},
            {"font", {{"version", "apsis-bitmap-5x7-v1"}, {"sha256", font_hash()}}},
            {"options", {{"width", options.width}, {"height", options.height}, {"labels", options.labels},
                {"stackLayout", "vertical-blueprint-order"}}},
            {"decks", Json::array()}, {"artifacts", Json::array()}};
        if (const auto holds = blueprint.find("holds"); holds != blueprint.end() && !holds->empty()) {
            manifest["holds"] = *holds;
            for (auto& hold : manifest["holds"]) hold["label"] = detail::hold_label(hold);
        }
        std::vector<RenderArtifact> artifacts;
        std::size_t remaining_bytes = limits.maxOutputBytes;
        const auto append = [&](RenderArtifact artifact) {
            if (artifact.bytes.size() > remaining_bytes)
                fail("render_resource_limit", "Rendered output exceeds the operational byte limit.");
            remaining_bytes -= artifact.bytes.size();
            artifacts.push_back(std::move(artifact));
        };
        Canvas stack(options.width, static_cast<std::uint32_t>(stack_height));
        for (std::size_t index = 0; index < count; ++index) {
            auto canvas = draw_deck(blueprint, index, rules.data(), options, view);
            std::copy(canvas.pixels.begin(), canvas.pixels.end(),
                stack.pixels.begin() + static_cast<std::ptrdiff_t>(index * canvas.pixels.size()));
            const auto name = decks[index].at("name").get<std::string>();
            append(png(canvas, "deck-" + number(index + 1) + "-" + slug(name) + ".png"));
            auto image = metadata(artifacts.back());
            image["deckIndex"] = index;
            image["name"] = name;
            manifest["decks"].push_back(std::move(image));
            manifest["artifacts"].push_back(metadata(artifacts.back()));
        }
        append(png(stack, "stack.png"));
        manifest["artifacts"].push_back(metadata(artifacts.back()));
        const auto manifest_bytes = canonical_json(manifest) + "\n";
        append({"manifest.json", "application/json", {manifest_bytes.begin(), manifest_bytes.end()},
            sha256(manifest_bytes.data(), manifest_bytes.size())});
        append(zip(artifacts, remaining_bytes));
        Json artifact_metadata = Json::array();
        for (const auto& artifact : artifacts) artifact_metadata.push_back(metadata(artifact));
        checked["data"] = {{"manifest", std::move(manifest)}, {"artifacts", std::move(artifact_metadata)}};
        return {std::move(checked), std::move(artifacts)};
    } catch (const RenderFailure& failure) {
        return failed(rules, {failure.issue});
    } catch (const std::bad_alloc&) {
        return failed(rules, {detail::error("render_resource_limit", "Render allocation could not be satisfied.")});
    }
}
} // namespace apsis::shipyard
