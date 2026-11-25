// Read-only adapter: default public validator options, no authoring/perf changes.
#include <apsis/shipyard/validator.hpp>

#include <csignal>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::size_t maxBytes = 8 * 1024 * 1024;

std::string bounded_read(std::istream& stream) {
    std::string bytes(maxBytes + 1, '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > maxBytes) throw std::runtime_error("input exceeds 8 MiB");
    if (stream.bad()) throw std::runtime_error("input read failed");
    bytes.resize(count);
    return bytes;
}
} // namespace

int main(int argc, char** argv) {
    namespace sy = apsis::shipyard;
    try {
        if (argc != 3)
            throw std::runtime_error("usage: shipyard-validate SCHEMA_DIRECTORY RULES_FILE < DEFINITION");
        auto schemas = sy::SchemaSet::load(argv[1]);
        if (!schemas.ok()) {
            std::cerr << sy::Json({{"error", "schema_loading"}, {"issues", schemas.issues}}).dump() << '\n';
            return 2;
        }
        std::ifstream stream(argv[2], std::ios::binary);
        if (!stream.is_open()) throw std::runtime_error("cannot open rules file");
        auto rules = sy::load_rules(bounded_read(stream), *schemas.value);
        if (!rules.ok()) {
            std::cerr << sy::Json({{"error", "rules_loading"}, {"issues", rules.issues}}).dump() << '\n';
            return 2;
        }
        const auto definition = bounded_read(std::cin);
        // Structural FAIL is a successful measurement; operational failures exit 2.
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN);
#endif
        std::cout << sy::validate(definition, *rules.value, *schemas.value).dump() << '\n' << std::flush;
        if (!std::cout) throw std::runtime_error("stdout write failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << sy::Json({{"error", "adapter_error"}, {"message", error.what()}}).dump() << '\n';
        return 2;
    }
}
