// Host-only key extraction; no network or tablet dependency.
#include "key-catalog.hpp"
#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace ure_locale_host {
namespace {
constexpr std::size_t max_catalog_bytes = 32U * 1024U * 1024U;
constexpr std::size_t max_rows = 16384;
constexpr std::size_t max_source_bytes = 65536;
constexpr std::size_t max_table_bytes = 2U * 1024U * 1024U;
struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) { if (fd < 0) throw std::runtime_error("Cannot open key-generation file"); }
    ~Fd() { if (value >= 0) close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
bool same(const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
        a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
        a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
std::string read_catalog(const std::filesystem::path& input) {
    Fd fd(open(input.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat before{}, after{};
    if (fstat(fd.value, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size < 0 ||
        static_cast<std::uint64_t>(before.st_size) > max_catalog_bytes)
        throw std::runtime_error("Key catalog must be a bounded regular file");
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(before.st_size));
    std::array<char, 16384> chunk{};
    for (;;) {
        const auto count = ::read(fd.value, chunk.data(), chunk.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("Cannot read key catalog");
        if (count == 0) break;
        if (static_cast<std::size_t>(count) > max_catalog_bytes - bytes.size())
            throw std::runtime_error("Key catalog exceeds input budget");
        bytes.append(chunk.data(), static_cast<std::size_t>(count));
    }
    if (fstat(fd.value, &after) != 0 || !same(before, after) ||
        bytes.size() != static_cast<std::uint64_t>(before.st_size))
        throw std::runtime_error("Key catalog changed while reading");
    return bytes;
}
bool utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        unsigned tail = 0; std::uint32_t value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { tail = 1; value = first & 0x1fU; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { tail = 2; value = first & 0x0fU; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { tail = 3; value = first & 0x07U; minimum = 0x10000; }
        else return false;
        if (tail > text.size() - i) return false;
        for (unsigned n = 0; n < tail; ++n) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0U) != 0x80U) return false;
            value = (value << 6U) | (byte & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
bool ascii_letter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool key_name(std::string_view name) {
    if (name.empty() || name.size() > 128 || (!ascii_letter(static_cast<unsigned char>(name[0])) && name[0] != '_')) return false;
    for (const auto c : name)
        if (!ascii_letter(static_cast<unsigned char>(c)) && !(c >= '0' && c <= '9') && c != '_' && c != '.' && c != '-') return false;
    return true;
}
std::string literal(std::string_view text) {
    // Fixed three-digit octal escapes preserve UTF-8/control bytes and cannot
    // consume a following digit. JSON surrogate escapes are not C++ literals.
    std::string result = "\"";
    for (const auto raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\') { result += '\\'; result += raw; }
        else if (c >= 0x20 && c <= 0x7e && c != '?') result += raw;
        else { result += '\\'; result += static_cast<char>('0' + (c >> 6U));
            result += static_cast<char>('0' + ((c >> 3U) & 7U)); result += static_cast<char>('0' + (c & 7U)); }
    }
    return result + '"';
}
void publish_header(const std::filesystem::path& output, const std::string& bytes) {
    if (output.empty() || output.filename().empty() || output.filename() == "." || output.filename() == "..")
        throw std::runtime_error("Invalid key header destination");
    const auto parent = output.parent_path();
    Fd root(open(output.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    for (const auto& part : parent.relative_path()) {
        if (part == ".") continue;
        if (part == "..") throw std::runtime_error("Parent traversal is not permitted for key output");
        if (mkdirat(root.value, part.c_str(), 0700) != 0 && errno != EEXIST)
            throw std::runtime_error("Cannot create key output directory");
        Fd next(openat(root.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        const auto previous = root.value; root.value = next.value; next.value = previous;
    }
    const auto filename = output.filename().string();
    struct stat existing{};
    if (fstatat(root.value, filename.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(existing.st_mode) || existing.st_nlink != 1)
            throw std::runtime_error("Key output must not be an indirect or special file");
    } else if (errno != ENOENT) throw std::runtime_error("Cannot inspect key output");
    std::string temporary;
    int value = -1;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        temporary = ".ure-key-header-" + std::to_string(getpid()) + "-" + std::to_string(attempt);
        value = openat(root.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (value >= 0) break;
        if (errno != EEXIST) throw std::runtime_error("Cannot stage key header");
    }
    Fd staged(value);
    try {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = ::write(staged.value, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("Cannot write key header");
            offset += static_cast<std::size_t>(count);
        }
        if (fsync(staged.value) != 0) throw std::runtime_error("Cannot synchronize key header");
        if (renameat(root.value, temporary.c_str(), root.value, filename.c_str()) != 0)
            throw std::runtime_error("Cannot publish key header");
    } catch (...) { unlinkat(root.value, temporary.c_str(), 0); throw; }
    if (fsync(root.value) != 0) throw std::runtime_error("Key header published but directory synchronization failed");
}
}

KeyTable validate_keys(const Json::Value& catalog) {
    if (!catalog.isObject() || !catalog["schema_version"].isUInt() || catalog["schema_version"].asUInt() != 1 ||
        !catalog["strings"].isArray() || catalog["strings"].size() > max_rows)
        throw std::runtime_error("Invalid key catalog schema");
    KeyTable table;
    std::set<std::string> names;
    std::size_t total = 0;
    for (const auto& row : catalog["strings"]) {
        if (!row.isObject() || !row["source"].isString() || !row["name"].isString())
            throw std::runtime_error("Key catalog rows require source and name strings");
        const auto text = row["source"].asString(), name = row["name"].asString();
        if (text.empty() || text.size() > max_source_bytes || !utf8(text) || !key_name(name))
            throw std::runtime_error("Invalid key name or source bytes");
        if (text.size() + name.size() > max_table_bytes - total)
            throw std::runtime_error("Key table exceeds byte budget");
        total += text.size() + name.size();
        if ((text == "Extra") != (name == "ure_extra_tab"))
            throw std::runtime_error("Reserved Extra alias collision");
        if (!names.insert(name).second || !table.emplace(text, name).second)
            throw std::runtime_error("Duplicate key name or source text");
    }
    table.emplace("Extra", "ure_extra_tab");
    return table;
}
std::string key_header(const KeyTable& keys) {
    std::string header = "// Generated from a structurally validated catalog; translation review is separate.\n"
        "#pragma once\n#include <string_view>\nnamespace ure_locale {\n"
        "struct Entry { std::string_view source, key; };\ninline constexpr Entry entries[] = {\n";
    for (const auto& [text, key] : keys) header += "    {" + literal(text) + ", " + literal(key) + "},\n";
    return header + "};\n}\n";
}
void generate_keys(const std::filesystem::path& input, const std::filesystem::path& output) {
    const auto bytes = read_catalog(input);
    // This pinned JsonCpp still skips comments inside objects with
    // allowComments=false. Reject slash tokens outside JSON strings first.
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (const auto byte : bytes) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (byte == '\\') escaped = true;
            else if (byte == '"') quoted = false;
        } else if (byte == '"') quoted = true;
        else if (byte == '/') throw std::runtime_error("Comments are not permitted in key catalogs");
        else if (byte == '{' || byte == '[') {
            if (++depth > 64) throw std::runtime_error("Key catalog exceeds nesting budget");
        } else if (byte == '}' || byte == ']') {
            if (depth == 0) throw std::runtime_error("Invalid key catalog nesting");
            --depth;
        }
    }
    // The named owner must outlive both the subscript and its entire iteration.
    const Json::Value catalog = [&] {
        Json::Value value; std::string errors;
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false; builder["allowComments"] = false;
        builder["rejectDupKeys"] = true; builder["failIfExtra"] = true; builder["stackLimit"] = 64;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!reader->parse(bytes.data(), bytes.data() + bytes.size(), &value, &errors))
            throw std::runtime_error("Invalid key catalog JSON");
        return value;
    }();
    std::error_code error;
    if (std::filesystem::equivalent(input, output, error) && !error)
        throw std::runtime_error("Key catalog and header must be separate files");
    publish_header(output, key_header(validate_keys(catalog)));
}
}
