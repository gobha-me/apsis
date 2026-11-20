#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace apsis::shipyard {
void to_json(Json& json, const Issue& issue) {
    json = {{"code", issue.code}, {"severity", issue.severity},
            {"message", issue.message}, {"path", issue.path}, {"details", issue.details}};
}
void sort_issues(std::vector<Issue>& issues) {
    std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) {
        return std::tie(a.path, a.code, a.severity) < std::tie(b.path, b.code, b.severity);
    });
}
namespace detail {
Issue error(std::string code, std::string message, std::string path) {
    return {std::move(code), "error", std::move(message), std::move(path), Json::object()};
}
std::string pointer_token(std::string_view token) {
    std::string out;
    for (const char c : token) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}
std::size_t character_count(std::string_view utf8) {
    // parse_json has already checked UTF-8. Count Unicode scalar values rather
    // than bytes, without trimming, normalization, or grapheme rewriting.
    return static_cast<std::size_t>(std::count_if(utf8.begin(), utf8.end(), [](unsigned char c) {
        return (c & 0xc0) != 0x80;
    }));
}
} // namespace detail

Result<Json> parse_json(std::string_view bytes, InputLimits limits) {
    if (bytes.size() > limits.maxBytes)
        return {std::nullopt, {detail::error("resource_limit", "JSON exceeds the operational byte limit.")}};
    std::vector<std::set<std::string>> keys;
    std::size_t nesting = 0;
    try {
        auto callback = [&](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start) {
                if (++nesting > limits.maxNesting)
                    throw detail::error("resource_limit", "JSON exceeds the operational nesting limit.");
                if (event == Json::parse_event_t::object_start) keys.emplace_back();
            } else if (event == Json::parse_event_t::object_end) {
                keys.pop_back();
                --nesting;
            } else if (event == Json::parse_event_t::array_end) {
                --nesting;
            } else if (event == Json::parse_event_t::key) {
                if (!keys.back().insert(value.get<std::string>()).second)
                    throw detail::error("duplicate_key", "Duplicate JSON object key.");
            } else if (event == Json::parse_event_t::value && value.is_number_float() &&
                       !std::isfinite(value.get<double>())) {
                throw detail::error("invalid_number", "JSON numbers must be finite.");
            }
            return true;
        };
        return {Json::parse(bytes.begin(), bytes.end(), callback, true, false), {}};
    } catch (const Issue& issue) {
        return {std::nullopt, {issue}};
    } catch (const Json::out_of_range&) {
        return {std::nullopt, {detail::error("invalid_number", "JSON number is outside the finite numeric range.")}};
    } catch (const Json::exception&) {
        // Parser diagnostics can contain input contents; keep those private.
        return {std::nullopt, {detail::error("invalid_json", "Expected strict UTF-8 JSON.")}};
    }
}
namespace {
void canonical_check(Json& value) {
    if (value.is_number_float()) {
        const double number = value.get<double>();
        if (!std::isfinite(number)) throw std::invalid_argument("Cannot serialize a nonfinite number.");
        if (number == 0) value = 0.0;
    } else if (value.is_structured()) {
        for (auto& child : value) canonical_check(child);
    }
}
} // namespace
std::string canonical_json(const Json& value) {
    auto normalized = value;
    canonical_check(normalized);
    return normalized.dump(-1, ' ', false, Json::error_handler_t::strict);
}
} // namespace apsis::shipyard
