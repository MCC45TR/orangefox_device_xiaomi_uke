#include "text-decoder.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

static void require(bool ok) {
    if (!ok) { std::fputs("UTF-8 decoder contract failed\n", stderr); std::abort(); }
}
static void check(const std::string& bytes, unsigned int expected, int consumed) {
    // Allocate exactly the input size, without a readable C-string sentinel.
    auto input = std::make_unique<char[]>(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) input[i] = bytes[i];
    unsigned int scalar = 0;
    require(twrpTruetype::utf8_to_unicode(input.get(), input.get()+bytes.size(), &scalar) == consumed);
    require(scalar == expected);
}
int main() {
    unsigned int scalar = 0;
    char empty = 0;
    require(twrpTruetype::utf8_to_unicode(&empty, &empty, &scalar) == 0);
    require(twrpTruetype::utf8_to_unicode(nullptr, nullptr, &scalar) == 0);
    require(twrpTruetype::utf8_to_unicode(&empty, &empty+1, nullptr) == 0);
    check(std::string("\xf8\0", 2), 0xfffd, 1);
    for (const auto& bytes : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x80\x80\x80",
        "\xe0\x80\x80", "\xe2\x28\xa1", "\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\x80", "\xff"})
        check(bytes, 0xfffd, 1);
    for (unsigned int cp = 0; cp <= 0x10ffff; ++cp) {
        if (cp >= 0xd800 && cp <= 0xdfff) continue;
        std::string bytes;
        if (cp < 0x80) bytes += static_cast<char>(cp);
        else if (cp < 0x800) { bytes += static_cast<char>(0xc0 | cp>>6); bytes += static_cast<char>(0x80 | (cp&63)); }
        else if (cp < 0x10000) { bytes += static_cast<char>(0xe0 | cp>>12); bytes += static_cast<char>(0x80 | ((cp>>6)&63)); bytes += static_cast<char>(0x80 | (cp&63)); }
        else { bytes += static_cast<char>(0xf0 | cp>>18); bytes += static_cast<char>(0x80 | ((cp>>12)&63)); bytes += static_cast<char>(0x80 | ((cp>>6)&63)); bytes += static_cast<char>(0x80 | (cp&63)); }
        check(bytes, cp, static_cast<int>(bytes.size()));
        for (size_t n = 1; n < bytes.size(); ++n) check(bytes.substr(0,n), 0xfffd, 1);
    }
    for (unsigned int pair = 0; pair < 65536; ++pair) {
        std::array<char,2> bytes{static_cast<char>(pair>>8), static_cast<char>(pair&255)};
        const auto original = bytes;
        const int n = twrpTruetype::utf8_to_unicode(bytes.data(), bytes.data()+2, &scalar);
        require(n >= 1 && n <= 2 && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff));
        require(bytes == original);
    }
    std::puts("Production UTF-8 decoder: all Unicode scalars, truncations and byte pairs passed");
}
