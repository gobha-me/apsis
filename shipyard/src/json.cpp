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
void add_issue(std::vector<Issue>& issues, Issue issue) {
    if (issues.size() < maxIssues - 1) {
        issues.push_back(std::move(issue));
        return;
    }
    if (issues.size() < maxIssues) {
        issues.push_back({"issues_truncated", "error",
                          "Issue limit reached; further checks and issues were dropped.",
                          "", {{"limit", maxIssues}, {"suppressedAtLeast", 1}}});
    }
    // The replaced diagnostic is suppressed; the unchecked remainder is not
    // counted. Never traverse the remainder just to calculate an exact total.
    throw IssueLimitReached{};
}
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

namespace {
// The callback DOM parser rescans an object's parent after every object end.
// The plain SAX DOM builder appends directly, keeping arrays of objects linear.
class CheckedDomParser : public nlohmann::detail::json_sax_dom_parser<Json> {
    using Base = nlohmann::detail::json_sax_dom_parser<Json>;
public:
    CheckedDomParser(Json& result, InputLimits limits) : Base(result), limits_(limits) {}

    bool start_object(std::size_t elements) {
        start_container();
        keys_.emplace_back();
        return Base::start_object(elements);
    }
    bool end_object() {
        keys_.pop_back();
        --nesting_;
        return Base::end_object();
    }
    bool start_array(std::size_t elements) {
        start_container();
        return Base::start_array(elements);
    }
    bool end_array() {
        --nesting_;
        return Base::end_array();
    }
    bool key(string_t& value) {
        if (!keys_.back().insert(value).second)
            throw detail::error("duplicate_key", "Duplicate JSON object key.");
        return Base::key(value);
    }
    bool number_float(number_float_t value, const string_t& token) {
        if (!std::isfinite(value))
            throw detail::error("invalid_number", "JSON numbers must be finite.");
        return Base::number_float(value, token);
    }
private:
    void start_container() {
        if (++nesting_ > limits_.maxNesting)
            throw detail::error("resource_limit", "JSON exceeds the operational nesting limit.");
    }
    InputLimits limits_;
    std::size_t nesting_ = 0;
    std::vector<std::set<std::string>> keys_;
};
} // namespace

Result<Json> parse_json(std::string_view bytes, InputLimits limits) {
    if (bytes.size() > limits.maxBytes)
        return {std::nullopt, {detail::error("resource_limit", "JSON exceeds the operational byte limit.")}};
    try {
        Json result;
        CheckedDomParser parser(result, limits);
        if (!Json::sax_parse(bytes.begin(), bytes.end(), &parser))
            return {std::nullopt, {detail::error("invalid_json", "Expected strict UTF-8 JSON.")}};
        return {std::move(result), {}};
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
