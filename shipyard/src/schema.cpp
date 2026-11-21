#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace apsis::shipyard {
namespace {
using Documents = std::map<std::string, Json, std::less<>>;
constexpr std::size_t maxIssues = 128;
// References and alternatives consume several schema steps per formula level.
// Admit all 64 formula levels; load_rules checks that independent bound first.
constexpr std::size_t maxSchemaSteps = 512;

// Only the three anchored patterns used by the bundled schemas are supported.
// Linear ASCII scans keep large untrusted strings out of recursive regex
// engines and make this intentionally small schema vocabulary explicit.
bool supported_pattern(std::string_view pattern) {
    return pattern == "^(0|[1-9][0-9]*),(0|[1-9][0-9]*)$" ||
           pattern == "^[0-9a-f]{64}$" ||
           pattern == "^[A-Za-z][A-Za-z0-9_]*$";
}
bool pattern_matches(std::string_view value, std::string_view pattern) {
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto letter = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    if (pattern == "^[A-Za-z][A-Za-z0-9_]*$") {
        return !value.empty() && letter(value.front()) &&
            std::all_of(value.begin() + 1, value.end(), [&](char c) {
                return letter(c) || digit(c) || c == '_';
            });
    }
    if (pattern == "^[0-9a-f]{64}$") {
        return value.size() == 64 &&
            std::all_of(value.begin(), value.end(), [&](char c) {
                return digit(c) || (c >= 'a' && c <= 'f');
            });
    }
    if (pattern == "^(0|[1-9][0-9]*),(0|[1-9][0-9]*)$") {
        const auto coordinate = [&](std::string_view text) {
            return text == "0" || (!text.empty() && text.front() >= '1' && text.front() <= '9' &&
                std::all_of(text.begin() + 1, text.end(), digit));
        };
        const auto comma = value.find(',');
        return comma != std::string_view::npos && coordinate(value.substr(0, comma)) &&
            coordinate(value.substr(comma + 1));
    }
    return false;
}

const Json* document(const Documents& docs, std::string name) {
    if (const auto it = docs.find(name); it != docs.end()) return &it->second;
    if (const auto pos = name.find_last_of('/'); pos != std::string::npos) name.erase(0, pos + 1);
    if (!name.ends_with(".schema.json")) name += ".schema.json";
    const auto it = docs.find(name);
    return it == docs.end() ? nullptr : &it->second;
}
struct Target { const Json* root; const Json* node; };
Target resolve(const Documents& docs, const Json& root, const std::string& ref) {
    const auto hash = ref.find('#');
    const auto name = ref.substr(0, hash);
    const auto* target = name.empty() ? &root : document(docs, name);
    if (!target) throw detail::error("invalid_schema", "Unresolved schema reference.");
    const std::string fragment = hash == std::string::npos ? "" : ref.substr(hash + 1);
    try {
        return {target, &target->at(Json::json_pointer(fragment))};
    } catch (const Json::exception&) {
        throw detail::error("invalid_schema", "Unresolved schema JSON Pointer.");
    }
}
bool integer(const Json& v) {
    return v.is_number_integer() || (v.is_number_float() &&
        std::isfinite(v.get<double>()) && std::floor(v.get<double>()) == v.get<double>());
}
bool type_matches(const Json& v, const std::string& type) {
    if (type == "object") return v.is_object();
    if (type == "array") return v.is_array();
    if (type == "string") return v.is_string();
    if (type == "boolean") return v.is_boolean();
    if (type == "null") return v.is_null();
    if (type == "number") return v.is_number();
    if (type == "integer") return integer(v);
    return false;
}
void check_schema(const Json& schema, const Documents& docs, const Json& root,
                  const std::string& path) {
    if (schema.is_boolean()) return;
    auto fail = [&] { throw detail::error("invalid_schema", "Invalid or unsupported schema structure.", path); };
    if (!schema.is_object()) fail();
    static const std::set<std::string> annotations{
        "$schema", "$id", "title", "description", "$comment", "default", "examples"
    };
    for (const auto& [key, value] : schema.items()) {
        const auto here = path + "/" + detail::pointer_token(key);
        if (annotations.contains(key)) {
            if (key != "default" && key != "examples" && !value.is_string()) fail();
            if (key == "examples" && !value.is_array()) fail();
            // Nested identifiers would introduce reference scopes that this
            // local registry deliberately does not implement.
            if (key == "$id" && &schema != &root) fail();
        } else if (key == "$ref") {
            if (!value.is_string()) fail();
            const auto target = resolve(docs, root, value.get<std::string>());
            if (!target.node->is_object() && !target.node->is_boolean()) fail();
        } else if (key == "$defs" || key == "properties") {
            if (!value.is_object()) fail();
            for (const auto& [name, child] : value.items())
                check_schema(child, docs, root, here + "/" + detail::pointer_token(name));
        } else if (key == "type") {
            const auto valid = [](const Json& t) {
                static const std::set<std::string> types{
                    "object", "array", "string", "number", "integer", "boolean", "null"};
                return t.is_string() && types.contains(t.get<std::string>());
            };
            if (value.is_array()) {
                if (value.empty()) fail();
                std::set<std::string> seen;
                for (const auto& t : value)
                    if (!valid(t) || !seen.insert(t.get<std::string>()).second) fail();
            } else if (!valid(value)) fail();
        } else if (key == "required") {
            if (!value.is_array()) fail();
            std::set<std::string> seen;
            for (const auto& name : value)
                if (!name.is_string() || !seen.insert(name.get<std::string>()).second) fail();
        } else if (key == "anyOf" || key == "oneOf" || key == "allOf" || key == "prefixItems") {
            if (!value.is_array() || value.empty()) fail();
            for (std::size_t i = 0; i < value.size(); ++i)
                check_schema(value[i], docs, root, here + "/" + std::to_string(i));
        } else if (key == "items" || key == "additionalProperties" || key == "propertyNames" ||
                   key == "if" || key == "then" || key == "else" || key == "not") {
            check_schema(value, docs, root, here);
        } else if (key == "minimum" || key == "maximum" ||
                   key == "exclusiveMinimum" || key == "exclusiveMaximum") {
            if (!value.is_number()) fail();
        } else if (key == "minItems" || key == "maxItems" || key == "minLength" ||
                   key == "maxLength" || key == "minProperties" || key == "maxProperties") {
            if (!integer(value) || value.get<double>() < 0) fail();
        } else if (key == "pattern") {
            if (!value.is_string() || !supported_pattern(value.get_ref<const std::string&>())) fail();
        } else if (key == "uniqueItems") {
            if (!value.is_boolean()) fail();
        } else if (key == "enum") {
            if (!value.is_array() || value.empty()) fail();
            for (std::size_t i = 0; i < value.size(); ++i)
                for (std::size_t j = 0; j < i; ++j) if (value[i] == value[j]) fail();
        } else if (key != "const") fail();
    }
}

void validate_node(const Json& value, const Json& schema, const Json& root,
                   const Documents& docs, const std::string& path,
                   std::vector<Issue>& issues, std::size_t depth = 0) {
    if (issues.size() >= maxIssues) return;
    auto fail = [&](std::string message) {
        if (issues.size() < maxIssues) issues.push_back(detail::error("schema_error", std::move(message), path));
    };
    if (depth > maxSchemaSteps) { fail("Schema evaluation exceeds the operational recursion limit."); return; }
    if (schema.is_boolean()) {
        if (!schema.get<bool>()) fail("Value is forbidden by the schema.");
        return;
    }
    if (schema.contains("$ref")) {
        const auto target = resolve(docs, root, schema["$ref"].get<std::string>());
        validate_node(value, *target.node, *target.root, docs, path, issues, depth + 1);
    }
    auto matches = [&](const Json& candidate) {
        std::vector<Issue> trial;
        validate_node(value, candidate, root, docs, path, trial, depth + 1);
        return trial.empty();
    };
    if (schema.contains("type")) {
        const auto& type = schema["type"];
        bool valid = false;
        if (type.is_array()) {
            for (const auto& t : type) valid |= type_matches(value, t.get<std::string>());
        } else valid = type_matches(value, type.get<std::string>());
        if (!valid) { fail("Value has an invalid JSON type."); return; }
    }
    if (schema.contains("const") && value != schema["const"]) fail("Value does not match the schema constant.");
    if (schema.contains("enum") &&
        std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end())
        fail("Value is outside the schema enumeration.");
    for (const auto key : {"anyOf", "oneOf"}) if (schema.contains(key)) {
        std::size_t count = 0;
        for (const auto& alternative : schema[key]) if (matches(alternative)) ++count;
        if (count == 0 || (std::string_view(key) == "oneOf" && count != 1))
            fail("Value does not match the schema alternatives.");
    }
    if (schema.contains("allOf"))
        for (const auto& child : schema["allOf"])
            validate_node(value, child, root, docs, path, issues, depth + 1);
    if (schema.contains("not") && matches(schema["not"])) fail("Value matches a forbidden schema.");
    if (schema.contains("if")) {
        const auto branch = matches(schema["if"]) ? "then" : "else";
        if (schema.contains(branch))
            validate_node(value, schema[branch], root, docs, path, issues, depth + 1);
    }
    if (value.is_number()) {
        const double number = value.get<double>();
        if (!std::isfinite(number)) fail("Number must be finite.");
        if (schema.contains("minimum") && number < schema["minimum"].get<double>()) fail("Number is below the schema minimum.");
        if (schema.contains("maximum") && number > schema["maximum"].get<double>()) fail("Number exceeds the schema maximum.");
        if (schema.contains("exclusiveMinimum") && number <= schema["exclusiveMinimum"].get<double>()) fail("Number must exceed the schema minimum.");
        if (schema.contains("exclusiveMaximum") && number >= schema["exclusiveMaximum"].get<double>()) fail("Number must be below the schema maximum.");
    }
    auto bounds = [&](std::size_t count, const char* min, const char* max) {
        if (schema.contains(min) && static_cast<double>(count) < schema[min].get<double>()) fail("Value is shorter than the schema minimum.");
        if (schema.contains(max) && static_cast<double>(count) > schema[max].get<double>()) fail("Value exceeds the schema size limit.");
    };
    if (value.is_string()) {
        const auto& string = value.get_ref<const std::string&>();
        bounds(detail::character_count(string), "minLength", "maxLength");
        if (schema.contains("pattern") &&
            !pattern_matches(string, schema["pattern"].get_ref<const std::string&>()))
            fail("String does not match the schema pattern.");
    }
    if (value.is_object()) {
        bounds(value.size(), "minProperties", "maxProperties");
        if (schema.contains("required"))
            for (const auto& key : schema["required"]) if (!value.contains(key.get<std::string>())) {
                if (issues.size() < maxIssues)
                    issues.push_back(detail::error("schema_error", "Required field is missing.",
                                     path + "/" + detail::pointer_token(key.get<std::string>())));
            }
        for (const auto& [key, child] : value.items()) {
            const auto childPath = path + "/" + detail::pointer_token(key);
            if (schema.contains("propertyNames"))
                validate_node(Json(key), schema["propertyNames"], root, docs, childPath, issues, depth + 1);
            if (schema.contains("properties") && schema["properties"].contains(key))
                validate_node(child, schema["properties"][key], root, docs, childPath, issues, depth + 1);
            else if (schema.contains("additionalProperties"))
                validate_node(child, schema["additionalProperties"], root, docs, childPath, issues, depth + 1);
        }
    }
    if (value.is_array()) {
        bounds(value.size(), "minItems", "maxItems");
        const std::size_t prefix = schema.contains("prefixItems") ? schema["prefixItems"].size() : 0;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i < prefix) validate_node(value[i], schema["prefixItems"][i], root, docs,
                                         path + "/" + std::to_string(i), issues, depth + 1);
            else if (schema.contains("items")) validate_node(value[i], schema["items"], root, docs,
                                         path + "/" + std::to_string(i), issues, depth + 1);
            if (schema.value("uniqueItems", false))
                for (std::size_t j = 0; j < i; ++j)
                    if (value[i] == value[j]) { fail("Array items must be unique."); break; }
        }
    }
}
} // namespace

Result<SchemaSet> SchemaSet::load(const std::filesystem::path& directory, InputLimits limits) {
    try {
        SchemaSet result;
        std::set<std::string> ids;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".schema.json")) continue;
            std::ifstream stream(entry.path(), std::ios::binary);
            if (!stream) return {std::nullopt, {detail::error("schema_io", "Cannot read a schema document.")}};
            std::string bytes;
            char c;
            while (bytes.size() <= limits.maxBytes && stream.get(c)) bytes += c;
            if (stream.bad()) return {std::nullopt, {detail::error("schema_io", "Cannot read a schema document.")}};
            auto parsed = parse_json(bytes, limits);
            if (!parsed.ok()) return {std::nullopt, std::move(parsed.issues)};
            if (!parsed.value->is_object() ||
                parsed.value->value("$schema", "") != "https://json-schema.org/draft/2020-12/schema" ||
                !parsed.value->contains("$id") || !(*parsed.value)["$id"].is_string() ||
                !ids.insert((*parsed.value)["$id"].get<std::string>()).second)
                return {std::nullopt, {detail::error("invalid_schema", "Expected unique Draft 2020-12 schema documents.")}};
            result.documents_.emplace(entry.path().filename().string(), std::move(*parsed.value));
        }
        for (const auto name : {"apsis-ship-rules-v4", "apsis-shipyard-v4", "apsis-ship-definition-v4",
                                "apsis-shipyard-v3", "apsis-ship-definition-v3"})
            if (!document(result.documents_, name))
                return {std::nullopt, {detail::error("invalid_schema", "Required shipyard schema is missing.")}};
        for (const auto& [name, root] : result.documents_) check_schema(root, result.documents_, root, "/" + name);
        return {std::move(result), {}};
    } catch (const Issue& issue) {
        return {std::nullopt, {issue}};
    } catch (const std::filesystem::filesystem_error&) {
        return {std::nullopt, {detail::error("schema_io", "Cannot read the schema directory.")}};
    } catch (const Json::exception&) {
        return {std::nullopt, {detail::error("invalid_schema", "Invalid schema document.")}};
    }
}
std::vector<Issue> SchemaSet::validate(const Json& value, std::string_view schema) const {
    std::vector<Issue> issues;
    const auto* root = document(documents_, std::string(schema));
    if (!root) return {detail::error("unknown_schema", "The requested schema is not installed.")};
    try { validate_node(value, *root, *root, documents_, "", issues); }
    catch (const Issue& issue) { issues.push_back(issue); }
    sort_issues(issues);
    return issues;
}
} // namespace apsis::shipyard
