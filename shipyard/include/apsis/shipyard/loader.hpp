#pragma once

#include <nlohmann/json.hpp>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apsis::shipyard {
using Json = nlohmann::json;

struct Issue {
    std::string code;
    std::string severity;
    std::string message;
    std::string path;
    Json details = Json::object();
};
void to_json(Json& json, const Issue& issue);
void sort_issues(std::vector<Issue>& issues);

template<class T> struct Result {
    std::optional<T> value;
    std::vector<Issue> issues;
    [[nodiscard]] bool ok() const { return value.has_value(); }
};

// Operational parser bounds, independent of game and technical input rules.
struct InputLimits {
    std::size_t maxBytes = 8 * 1024 * 1024;
    std::size_t maxNesting = 64;
};
Result<Json> parse_json(std::string_view bytes, InputLimits limits = {});
std::string canonical_json(const Json& value);

// Validates precisely the documented Draft 2020-12 vocabulary used by our
// schemas; unsupported schema keywords and unresolved references are errors.
class SchemaSet {
public:
    static Result<SchemaSet> load(const std::filesystem::path& directory,
                                  InputLimits limits = {});
    [[nodiscard]] std::vector<Issue> validate(const Json& value,
                                            std::string_view schema) const;
private:
    std::map<std::string, Json, std::less<>> documents_;
};

class RulesSnapshot {
public:
    [[nodiscard]] const Json& data() const { return *data_; }
    [[nodiscard]] const std::string& sha256() const { return sha256_; }
    [[nodiscard]] int version() const { return 4; }
private:
    RulesSnapshot() = default;
    std::shared_ptr<const Json> data_;
    std::string sha256_;
    friend Result<RulesSnapshot> load_rules(std::string_view, const SchemaSet&, InputLimits);
};
Result<RulesSnapshot> load_rules(std::string_view bytes, const SchemaSet& schemas,
                                 InputLimits limits = {});

struct MigrationReport {
    int sourceVersion = 4;
    int targetVersion = 4;
    bool rulesIdentityChanged = false;
    std::vector<std::string> changes;
};
struct ImportedDefinition {
    Json definition;
    MigrationReport migration;
};
Result<ImportedDefinition> import_definition(std::string_view bytes,
                                             const RulesSnapshot& rules,
                                             const SchemaSet& schemas,
                                             InputLimits limits = {});

// Absolute epsilon applies only to computed comparisons. Input limits and
// discrete values are exact; serialization never rounds imported numbers.
bool numerically_equal(double left, double right, const RulesSnapshot& rules);
double round_computed(double value, const RulesSnapshot& rules);
} // namespace apsis::shipyard
