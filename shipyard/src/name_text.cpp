#include "internal.hpp"

#include <utf8proc.h>

namespace apsis::shipyard::detail {
std::size_t grapheme_count(std::string_view utf8) {
    // The public parser already checked UTF-8 and bounded its byte length.
    auto* bytes = reinterpret_cast<const utf8proc_uint8_t*>(utf8.data());
    auto remaining = static_cast<utf8proc_ssize_t>(utf8.size());
    std::size_t count = 0;
    // A virtual control before the string guarantees the initial GB1 boundary
    // (GB4), including leading combining marks, without changing its bytes.
    utf8proc_int32_t previous = 0;
    utf8proc_int32_t state = 0;
    while (remaining > 0) {
        utf8proc_int32_t current = 0;
        const auto width = utf8proc_iterate(bytes, remaining, &current);
        count += static_cast<std::size_t>(utf8proc_grapheme_break_stateful(previous, current, &state));
        bytes += width;
        remaining -= width;
        previous = current;
    }
    return count;
}
} // namespace apsis::shipyard::detail
