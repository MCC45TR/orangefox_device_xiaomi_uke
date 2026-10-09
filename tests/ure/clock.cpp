// SPDX-License-Identifier: GPL-3.0-or-later
#include "ure-clock.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>

static std::array<unsigned char, 8> encode(std::uint64_t value) {
    std::array<unsigned char, 8> bytes{};
    for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(value >> (i * 8));
    return bytes;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    using namespace ure::clock;
    const auto offset = encode(1780000000123ULL); // Synthetic, not an owner record.
    const auto first = compose("1000", offset);
    assert(first.state == State::observed && first.milliseconds == 1780001000123ULL);
    assert(compose("1000", offset).milliseconds == first.milliseconds);
    assert(compose("1001", offset).milliseconds == first.milliseconds + 1000);
    for (auto malformed : {"", "-1", "+1", "12x", "1\n", " 1", "99999999999", "4102444801"})
        assert(compose(malformed, offset).state == State::invalid);
    for (auto value : {std::uint64_t(0), std::numeric_limits<std::uint64_t>::max(), std::uint64_t(4102444800000ULL)})
        assert(compose("0", encode(value)).state == State::invalid);
    assert(compose("0", encode(1577836800000ULL)).state == State::observed);
    assert(compose("0", encode(1577836799999ULL)).state == State::invalid);
    assert(compose("0", encode(4102444799999ULL)).state == State::observed);
    assert(compose("1", encode(4102444799999ULL)).state == State::invalid);

    const std::string root = argv[1], path = root + "/time/ats_2";
    assert(::mkdir((root + "/time").c_str(), 0700) == 0);
    ure::telemetry::detail::Fd directory(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    assert(directory.get() >= 0);
    std::array<unsigned char, 8> read{};
    assert(!read_offset(directory.get(), read));
    auto write = [&](std::size_t length) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (std::size_t i = 0; i < length; ++i) out.put(static_cast<char>(offset[i % 8]));
        assert(out.good());
    };
    for (std::size_t length : {0, 1, 7, 9, 64, 4096}) { write(length); assert(!read_offset(directory.get(), read)); }
    write(8);
    assert(read_offset(directory.get(), read) && read == offset);
    assert(::rename(path.c_str(), (root + "/offset").c_str()) == 0);
    assert(::symlink("../offset", path.c_str()) == 0);
    assert(!read_offset(directory.get(), read));
    assert(::unlink(path.c_str()) == 0);
    assert(::rmdir((root + "/time").c_str()) == 0);
    assert(::symlink(".", (root + "/time").c_str()) == 0);
    assert(!read_offset(directory.get(), read));
    std::cout << "UTC counter/ATS2 bounds, endian, repeat, exact-length and no-symlink controls passed.\n";
}
