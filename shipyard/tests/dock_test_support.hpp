#pragma once

#include "test_support.hpp"

#include <apsis/shipyard/validator.hpp>

#include <cstdint>

namespace dock_test {
using namespace test;

inline void fill_rectangle(Json &blueprint, int x, int y, int width,
                           int height) {
  for (int column = x; column < x + width; ++column)
    for (int row = y; row < y + height; ++row)
      blueprint["decks"][0]["floors"]
               [std::to_string(column) + "," + std::to_string(row)] = true;
}

inline std::uint64_t add_module(Json &blueprint, std::string_view type, int x,
                                int y, int rotation = 0,
                                std::string_view interface = "") {
  const auto id = blueprint.at("nextId").get<std::uint64_t>();
  Json module = {
      {"id", id}, {"type", type}, {"x", x}, {"y", y}, {"r", rotation}};
  if (!interface.empty())
    module["interface"] = interface;
  blueprint["decks"][0]["modules"].push_back(std::move(module));
  blueprint["nextId"] = id + 1;
  return id;
}

// Clear exterior faces point left for the airlock and down for the port/engine.
// A broad floor corridor surrounds the small nonwalkable systems.
inline Json base(std::string_view build_type) {
  auto blueprint = blank(build_type);
  fill_rectangle(blueprint, 2, 2, 10, 10);
  add_module(blueprint, "cockpit", 3, 3);
  add_module(blueprint, "microReactor", 5, 3);
  add_module(blueprint, "microLifeSupport", 7, 3);
  add_module(blueprint, "airlock", 2, 5, 1);
  return blueprint;
}

inline Json station() {
  auto blueprint = base("station");
  add_module(blueprint, "dockingPort", 6, 10);
  return blueprint;
}

inline Json visitor() {
  auto blueprint = base("ship");
  add_module(blueprint, "microEngine", 9, 10);
  return blueprint;
}

// Luna, under owner delegation 2026-11-23: translate the complete real
// visitor (+4,+5) to the Manhattan-one boundary, keeping its material,
// systems, clear approaches and internal crew routes unchanged.
inline Json aligned_visitor(Json blueprint = visitor()) {
  for (auto &deck : blueprint["decks"]) {
    Json floors = Json::object();
    for (const auto &[key, value] : deck["floors"].items()) {
      const auto comma = key.find(',');
      floors[std::to_string(std::stoi(key.substr(0, comma)) + 4) + "," +
             std::to_string(std::stoi(key.substr(comma + 1)) + 5)] = value;
    }
    deck["floors"] = std::move(floors);
    for (auto &module : deck["modules"]) {
      module["x"] = module.at("x").get<int>() + 4;
      module["y"] = module.at("y").get<int>() + 5;
    }
  }
  return blueprint;
}

inline bool has_issue(const Json &report, std::string_view code,
                      std::string_view path = "") {
  return std::any_of(report.at("issues").begin(), report.at("issues").end(),
                     [&](const Json &item) {
                       return item.at("code") == code &&
                              (path.empty() || item.at("path") == path);
                     });
}

inline Json dock_at_port(const Json &host, const Json &ship,
                         const RulesSnapshot &active, const SchemaSet &schema,
                         DockOptions options = {}) {
  return dock(host.dump(), 0, 5, ship.dump(), active, schema, options);
}
} // namespace dock_test
