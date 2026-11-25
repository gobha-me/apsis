#include "internal.hpp"

#include <algorithm>
#include <array>

namespace apsis::shipyard::detail {
std::string normalize_design_name(std::string_view utf8) {
    using namespace std::literals;
    // ECMAScript trim margins from the prototype: Zs, FEFF and LS/PS.
    // C0/DEL are rejected before trimming, including tab, LF, CR, VT and FF.
    constexpr std::array spaces{
        " "sv,
        "\xc2\xa0"sv,
        "\xe1\x9a\x80"sv,
        "\xe2\x80\x80"sv,
        "\xe2\x80\x81"sv,
        "\xe2\x80\x82"sv,
        "\xe2\x80\x83"sv,
        "\xe2\x80\x84"sv,
        "\xe2\x80\x85"sv,
        "\xe2\x80\x86"sv,
        "\xe2\x80\x87"sv,
        "\xe2\x80\x88"sv,
        "\xe2\x80\x89"sv,
        "\xe2\x80\x8a"sv,
        "\xe2\x80\xa8"sv,
        "\xe2\x80\xa9"sv,
        "\xe2\x80\xaf"sv,
        "\xe2\x81\x9f"sv,
        "\xe3\x80\x80"sv,
        "\xef\xbb\xbf"sv
    };
    for (;;) {
        const auto margin = std::find_if(spaces.begin(), spaces.end(),
            [utf8](std::string_view space) { return utf8.starts_with(space); });
        if (margin == spaces.end()) break;
        utf8.remove_prefix(margin->size());
    }
    for (;;) {
        const auto margin = std::find_if(spaces.begin(), spaces.end(),
            [utf8](std::string_view space) { return utf8.ends_with(space); });
        if (margin == spaces.end()) break;
        utf8.remove_suffix(margin->size());
    }
    if (utf8.empty()) return "Unnamed design";
    return std::string(utf8);
}
} // namespace apsis::shipyard::detail
