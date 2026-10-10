#include "boot_state.hpp"
#include <cassert>
#include <iostream>
#include <string>
int main() {
    const std::string orange = "androidboot.verifiedbootstate = \"orange\"\n";
    int checks = 0;
    auto expect = [&](bool actual, bool expected) { assert(actual == expected); ++checks; };
    using ure::measured_uke_unlocked;
    expect(measured_uke_unlocked("uke", "", "", "orange", orange), true);
    expect(measured_uke_unlocked("uke", "unlocked", "0", "orange", orange), true);
    expect(measured_uke_unlocked("uke", "unlocked", "0", "", ""), true);
    expect(measured_uke_unlocked("nabu", "", "", "orange", orange), false);
    expect(measured_uke_unlocked("uke", "locked", "", "orange", orange), false);
    expect(measured_uke_unlocked("uke", "", "1", "orange", orange), false);
    expect(measured_uke_unlocked("uke", "", "", "green", orange), false);
    expect(measured_uke_unlocked("uke", "", "", "red", orange), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", ""), false);
    expect(measured_uke_unlocked("uke", "", "", "", orange), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", orange + orange), false);
    expect(measured_uke_unlocked("uke", "unlocked", "0", "orange", "androidboot.verifiedbootstate = \"green\""), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", orange + "androidboot.flash.locked = \"1\""), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", orange + "androidboot.vbmeta.device_state = \"locked\""), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", "androidboot.verifiedbootstate = \"orange\",\"green\""), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", "androidboot.verifiedbootstate"), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", orange + std::string(256 * 1024, 'x')), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", orange + std::string(1, '\0')), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", " \tandroidboot.verifiedbootstate\t=  \"orange\"\r\n"), true);
    expect(measured_uke_unlocked("uke", "unlocked", "", "orange", ""), false);
    expect(measured_uke_unlocked("uke", "", "0", "orange", ""), false);
    expect(measured_uke_unlocked("uke", "unlocked", "0", "green", ""), false);
    expect(measured_uke_unlocked("uke", "", "", "orange", orange + std::string(4097, 'x')), false);
    std::cout << checks << " measured-boot-state controls passed; no storage or decryption acceptance.\n";
}
