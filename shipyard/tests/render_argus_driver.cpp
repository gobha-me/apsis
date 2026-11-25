#include "test_support.hpp"

#include <apsis/shipyard/render.hpp>
#include <apsis/shipyard/validator.hpp>

// Additional test-only observation transport; rules snapshots are public
// outputs. This transport is test-only. All normalization, rules loading and
// rendering use installed public APIs; Python independently decodes the
// returned bytes.
int main() {
  using namespace test;
  try {
    const auto schema = schemas();
    const auto installed = rules(schema);
    for (std::string line; std::getline(std::cin, line);) {
      const auto request = Json::parse(line);
      auto active = installed;
      Json response = Json::object();
      if (request.contains("rules")) {
        auto loaded =
            load_rules(request.at("rules").get<std::string>(), schema);
        response["rulesLoad"] = {{"ok", loaded.ok()},
                                 {"issues", loaded.issues},
                                 {"snapshot", nullptr}};
        if (!loaded.ok()) {
          std::cout << response.dump() << '\n';
          continue;
        }
        response["rulesLoad"]["snapshot"] = {
            {"data", loaded.value->data()},
            {"sha256", loaded.value->sha256()},
            {"version", loaded.value->version()}};
        active = std::move(*loaded.value);
      }
      RenderOptions options;
      const auto settings = request.value("options", Json::object());
      options.width = settings.value("width", options.width);
      options.height = settings.value("height", options.height);
      options.labels = settings.value("labels", options.labels);
      RenderLimits limits;
      const auto bounds = request.value("limits", Json::object());
      limits.maxDecks = bounds.value("maxDecks", limits.maxDecks);
      limits.maxDimension = bounds.value("maxDimension", limits.maxDimension);
      limits.maxTotalPixels =
          bounds.value("maxTotalPixels", limits.maxTotalPixels);
      limits.maxOutputBytes =
          bounds.value("maxOutputBytes", limits.maxOutputBytes);
      InputLimits input_limits;
      const auto input_bounds = request.value("inputLimits", Json::object());
      input_limits.maxBytes =
          input_bounds.value("maxBytes", input_limits.maxBytes);
      input_limits.maxNesting =
          input_bounds.value("maxNesting", input_limits.maxNesting);
      const auto input = request.at("input").get<std::string>();
      const auto result =
          render(input, active, schema, options, limits, input_limits);
      response["envelope"] = result.envelope;
      response["artifacts"] = Json::array();
      constexpr char digits[] = "0123456789abcdef";
      for (const auto &artifact : result.artifacts) {
        std::string hex;
        hex.reserve(artifact.bytes.size() * 2);
        for (const auto byte : artifact.bytes) {
          hex.push_back(digits[byte >> 4]);
          hex.push_back(digits[byte & 15]);
        }
        response["artifacts"].push_back({{"filename", artifact.filename},
                                         {"mediaType", artifact.mediaType},
                                         {"sha256", artifact.sha256},
                                         {"width", artifact.width},
                                         {"height", artifact.height},
                                         {"hex", std::move(hex)}});
      }
      if (request.value("validate", false)) {
        const auto validated =
            validate(input, active, schema, {}, input_limits);
        response["validation"] = validated;
        if (validated.at("pass").get<bool>())
          response["canonicalDefinition"] =
              canonical_json(validated.at("data").at("definition"));
      }
      std::cout << response.dump() << '\n';
    }
  } catch (const std::exception &error) {
    std::cerr << "Renderer test driver: " << error.what() << '\n';
    return 1;
  }
}
