// SPDX-License-Identifier: GPL-3.0-or-later
#include "ure-telemetry.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using namespace ure::telemetry;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static std::string fixture_root;
static CpuCache fixture_cache;
static std::chrono::steady_clock::time_point fixture_time;
namespace ure::telemetry {
static Battery fixture_observe_battery() { return observe_battery(fixture_root.c_str()); }
static Temperature fixture_cpu_snapshot() { return fixture_cache.get(fixture_root.c_str(), fixture_time); }
}
struct DataManager {
    static inline std::map<std::string, std::string> values;
    static std::string GetStrValue(const char* key) { return values[key]; }
    static int GetIntValue(const char* key) { return std::atoi(values[key].c_str()); }
    static void SetValue(const char* key, const std::string& value) { values[key] = value; }
};
struct EndIteration {};
namespace std::this_thread {
template<class Rep, class Period> void sleep_for_test(const std::chrono::duration<Rep, Period>&) { throw EndIteration{}; }
}
static struct PartitionFixture { void Check_UsbOtg_Status() {} } PartitionManager;
static void monitor_once() {
#define observe_battery fixture_observe_battery
#define sleep_for sleep_for_test
#include "telemetry-monitor.inc"
#undef sleep_for
#undef observe_battery
    try { monitorBatteryInBackground(); } catch (const EndIteration&) {}
}
static int cpu_magic(const std::string& varName, std::string& value) {
    if (false) {}
#define cpu_snapshot fixture_cpu_snapshot
#include "telemetry-cpu.inc"
#undef cpu_snapshot
    return -1;
}

struct ImageResource { void* GetResource() { return this; } } image, charge;
struct FontResource { void* GetResource() { return this; } } font;
struct Color { int red = 1, green = 2, blue = 3, alpha = 255; };
static std::string rendered;
static int fills, charge_blits, last_height;
static void gr_color(int, int, int, int) {}
static void gr_fill(int, int, int, int height) { ++fills; last_height = height; check(height > 0 && height <= 18, "fill escaped battery bounds"); }
static void gr_blit(void* resource, int, int, int, int, int, int) { if (resource == &charge) ++charge_blits; }
namespace twrpTruetype { static int gr_ttf_measureEx(const char* value, void*) { return static_cast<int>(std::strlen(value)); } }
constexpr int TOP_LEFT = 0;
static void gr_textEx_scaleW(int, int, const char* value, void*, int, int, bool) { rendered = value; }
struct GUIBattery {
    Color mColor, mColorLow;
    int mDX = 30, mDY = 30, mDW = 32, mDH = 18, mCX = 0, mCY = 0, mCW = 8, mCH = 8;
    int mFontHeight = 12, mPadding = 2, mRenderW = 32, mRenderH = 18, mRenderX = 80, mRenderY = 30;
    bool mStateMode = false;
    ImageResource *mCharge = &charge, *mImg = &image, *mLowImg = &image, *mImg100 = &image, *mImg75 = &image;
    ImageResource *mImg50 = &image, *mImg25 = &image, *mImg15 = &image, *mImgc15 = &image, *mImg5 = &image;
    FontResource* mFont = &font;
    bool isConditionTrue() const { return true; }
    int Render();
};
#include "telemetry-render.inc"

static void write_value(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc); out << value;
    check(static_cast<bool>(out), "fixture write failed");
}
static void reset_tree(const fs::path& root) {
    fs::remove_all(root); fs::create_directories(root / "class/power_supply"); fs::create_directories(root / "class/thermal");
}
static fs::path battery(const fs::path& root, const std::string& capacity = "77\n", const std::string& status = "Charging\n") {
    const auto target = root / "devices/power/battery";
    write_value(target / "type", "Battery\n"); write_value(target / "capacity", capacity); write_value(target / "status", status);
    fs::create_directory_symlink("../../devices/power/battery", root / "class/power_supply/pack");
    return target;
}
static fs::path thermal(const fs::path& root, int zone, const std::string& type, const std::string& value) {
    const auto target = root / "devices/thermal" / std::to_string(zone);
    write_value(target / "type", type + "\n"); write_value(target / "temp", value);
    fs::create_directory_symlink("../../devices/thermal/" + std::to_string(zone), root / "class/thermal" / ("thermal_zone" + std::to_string(zone)));
    return target;
}
static void render_check(const char* capacity, int available, int charging, bool expected, int mode = 1, bool states = false) {
    DataManager::values = {{"tw_battery", capacity}, {"tw_battery_charge", std::string(capacity) + "%+"}, {"ure_battery_available", std::to_string(available)}, {"charging_now", std::to_string(charging)}, {"enable_battery", std::to_string(mode)}};
    fills = charge_blits = last_height = 0; rendered.clear(); GUIBattery widget; widget.mStateMode = states;
    check(widget.Render() == 0, "real battery renderer refused fixture");
    if (!expected) check(rendered == "--" && fills == 0 && charge_blits == 0, "unavailable render showed percent fill or charging");
    else { check(rendered.find('%') != std::string::npos, "valid renderer omitted percent"); check(charge_blits == (mode == 1 && charging == 1 ? 1 : 0), "charging icon differs"); }
}

static void suite(const fs::path& root, const fs::path& outside) {
    fixture_root = root.string(); reset_tree(root);
    check(!observe_battery(fixture_root.c_str()).available(), "missing battery accepted");
    monitor_once(); check(DataManager::values["tw_battery"] == "--" && DataManager::values["charging_now"] == "0", "initial missing data fabricated");
    auto pack = battery(root);
    auto sample = observe_battery(fixture_root.c_str()); check(sample.available() && sample.capacity == 77 && sample.charging(), "contained class alias failed");
    monitor_once(); check(DataManager::values["tw_battery_charge"] == "77%+", "real monitor did not publish direct reading");
    for (const auto& text : {"", "7", "-1\n", "101\n", "+1\n", "1junk\n", " 1\n", "1\n\n", "999999999999999999999\n", "0x10\n", "1.5\n", "1\r\n"}) {
        write_value(pack / "capacity", text); check(!observe_battery(fixture_root.c_str()).available(), "malformed capacity accepted");
    }
    write_value(pack / "capacity", std::string("7\0x", 3)); check(!observe_battery(fixture_root.c_str()).available(), "NUL capacity accepted");
    write_value(pack / "capacity", std::string(65, '1')); check(!observe_battery(fixture_root.c_str()).available(), "oversized capacity accepted");
    for (const auto* text : {"0\n", "100\n"}) { write_value(pack / "capacity", text); check(observe_battery(fixture_root.c_str()).available(), "capacity boundary refused"); }
    write_value(pack / "capacity", "42\n");
    for (const auto* status : {"Discharging\n", "Not charging\n", "Full\n", "Unknown\n", "C\n", "charging\n", "Charging junk\n", ""}) {
        write_value(pack / "status", status); sample = observe_battery(fixture_root.c_str()); check(sample.available() && !sample.charging(), "status fabricated charging");
    }
    fs::remove(pack / "status"); sample = observe_battery(fixture_root.c_str()); check(sample.available() && !sample.charging(), "missing status fabricated charging");
    fs::remove(pack / "capacity"); monitor_once();
    check(DataManager::values["tw_battery"] == "--" && DataManager::values["tw_battery_charge"] == "--" && DataManager::values["charging_now"] == "0", "disappearance retained stale battery");
    write_value(pack / "capacity", "55\n"); write_value(pack / "status", "Charging\n"); monitor_once(); check(DataManager::values["tw_battery"] == "55", "late battery failed to recover");
    fs::remove(root / "class/power_supply/pack"); monitor_once(); check(DataManager::values["tw_battery"] == "--" && DataManager::values["charging_now"] == "0", "removed provider retained stale battery");
    fs::create_directory_symlink("../../devices/power/battery", root / "class/power_supply/pack"); monitor_once(); check(DataManager::values["tw_battery"] == "55", "provider hotplug failed");
    fs::permissions(pack / "capacity", fs::perms::none); if (::geteuid() != 0) check(!observe_battery(fixture_root.c_str()).available(), "denied capacity accepted"); fs::permissions(pack / "capacity", fs::perms::owner_read | fs::perms::owner_write);
    fs::remove(pack / "capacity"); fs::create_symlink(pack / "status", pack / "capacity"); check(!observe_battery(fixture_root.c_str()).available(), "attribute symlink accepted");
    fs::remove(pack / "capacity"); check(::mkfifo((pack / "capacity").c_str(), 0600) == 0, "FIFO setup failed"); check(!observe_battery(fixture_root.c_str()).available(), "nonregular attribute accepted");
    reset_tree(root); fs::create_directories(outside); write_value(outside / "type", "Battery\n"); write_value(outside / "capacity", "88\n"); write_value(outside / "status", "Charging\n");
    fs::create_directory_symlink(outside, root / "class/power_supply/escape"); check(!observe_battery(fixture_root.c_str()).available(), "outside-root class alias accepted");
    fs::remove(root / "class/power_supply/escape"); fs::create_directory_symlink("loop", root / "class/power_supply/loop"); check(!observe_battery(fixture_root.c_str()).available(), "looping alias accepted");
    reset_tree(root); pack = battery(root); write_value(pack / "type", "USB\n"); check(!observe_battery(fixture_root.c_str()).available(), "nonbattery provider accepted");
    reset_tree(root); pack = battery(root); write_value(root / "class/power_supply/second/type", "Battery\n"); check(observe_battery(fixture_root.c_str()).state == State::ambiguous, "ambiguous batteries accepted");
    reset_tree(root); for (int i = 0; i < 129; ++i) fs::create_directory(root / "class/power_supply" / ("entry" + std::to_string(i))); check(observe_battery(fixture_root.c_str()).state == State::budget, "enumeration budget ignored");

    reset_tree(root); thermal(root, 0, "aoss-0", "99000\n"); check(!observe_cpu(fixture_root.c_str()).available(), "aoss zone mislabeled CPU");
    auto cpu = thermal(root, 47, "cpu_therm", "42123\n"); auto temperature = observe_cpu(fixture_root.c_str()); check(temperature.available() && temperature.millidegrees == 42123 && temperature_text(temperature) == "42", "CPU type/unit selection failed");
    write_value(cpu / "temp", "-12500\n"); temperature = observe_cpu(fixture_root.c_str()); check(temperature.available() && temperature.millidegrees == -12500 && temperature_text(temperature) == "-12", "signed millidegrees lost");
    write_value(cpu / "temp", "42\n"); check(temperature_text(observe_cpu(fixture_root.c_str())) == "0", "temperature guessed units");
    for (const auto& text : {"", "45000", "-274000\n", "250001\n", "-100001\n", "+45000\n", "45000junk\n", "0x100\n", "45000\n\n", "99999999999999999999"}) {
        write_value(cpu / "temp", text); check(!observe_cpu(fixture_root.c_str()).available(), "malformed temperature accepted");
    }
    write_value(cpu / "temp", std::string(65, '4')); check(!observe_cpu(fixture_root.c_str()).available(), "oversized temperature accepted");
    fs::remove(cpu / "temp"); fs::create_symlink(outside / "capacity", cpu / "temp"); check(!observe_cpu(fixture_root.c_str()).available(), "thermal attribute symlink accepted");
    reset_tree(root); thermal(root, 9, "cpuss-2", "39000\n"); thermal(root, 1, "cpuss-0", "48000\n"); thermal(root, 5, "cpuss-fake", "190000\n"); check(observe_cpu(fixture_root.c_str()).millidegrees == 48000, "CPU fallback selection depends on index/order");
    auto preferred = thermal(root, 99, "cpu_therm", "41000\n"); check(observe_cpu(fixture_root.c_str()).millidegrees == 41000, "CPU channel priority lost");
    write_value(preferred / "temp", "bad\n"); check(!observe_cpu(fixture_root.c_str()).available(), "bad selected CPU silently fell back");
    thermal(root, 100, "cpu_therm", "44000\n"); check(observe_cpu(fixture_root.c_str()).state == State::ambiguous, "ambiguous CPU channels accepted");
    reset_tree(root); CpuCache cache; const auto t0 = std::chrono::steady_clock::time_point{};
    check(!cache.get(fixture_root.c_str(), t0).available(), "empty cache fabricated temperature"); cpu = thermal(root, 8, "cpu_therm", "45000\n");
    check(!cache.get(fixture_root.c_str(), t0 + std::chrono::seconds(4)).available(), "unavailable state not cached"); check(cache.get(fixture_root.c_str(), t0 + std::chrono::seconds(5)).millidegrees == 45000, "late thermal provider not recovered");
    fs::remove(cpu / "temp"); check(cache.get(fixture_root.c_str(), t0 + std::chrono::seconds(9)).available(), "five-second cache refreshed early"); check(!cache.get(fixture_root.c_str(), t0 + std::chrono::seconds(10)).available(), "expired temperature retained stale value");
    write_value(cpu / "temp", "37000\n"); check(cache.get(fixture_root.c_str(), t0 + std::chrono::seconds(15)).millidegrees == 37000, "thermal reappearance failed");
    fixture_time = t0; std::string value; check(cpu_magic("tw_cpu_temp", value) == 0 && value == "37", "real CPU data branch failed"); check(cpu_magic("ure_cpu_temp_available", value) == 0 && value == "1", "real CPU availability branch failed");
    fs::remove(cpu / "temp"); fixture_time += std::chrono::seconds(5); check(cpu_magic("tw_cpu_temp", value) == 0 && value == "--", "real CPU branch retained stale value"); check(cpu_magic("ure_cpu_temp_available", value) == 0 && value == "0", "CPU missing state lost");

    for (const auto* invalid : {"--", "-1", "101", "junk", "2147483647"}) for (const int mode : {0, 1}) for (const bool states : {false, true}) render_check(invalid, 1, 1, false, mode, states);
    render_check("77", 0, 1, false); render_check("77", 1, 1, true); check(fills == 1 && last_height == 13, "real dynamic fill differs");
    render_check("0", 1, 0, true); check(fills == 0, "empty battery has phantom fill"); render_check("100", 1, 0, true); check(last_height == 18, "full fill exceeds bounds");
    render_check("77", 1, 1, true, 1, true); check(fills == 0, "state icon drew dynamic fill");
    check(fill_height(18, -100) == 0 && fill_height(18, 1000) == 18 && fill_height(-1, 50) == 0 && fill_height(std::numeric_limits<int>::max(), 100) == std::numeric_limits<int>::max(), "fill clamp/overflow contract failed");
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "fixture root argument required"); const fs::path root(argv[1]);
        suite(root, root.parent_path() / "outside");
        std::cout << "Bounded telemetry and exact GUI/monitor/CPU source controls passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
