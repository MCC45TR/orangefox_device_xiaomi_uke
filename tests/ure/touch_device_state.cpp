// SPDX-License-Identifier: Apache-2.0
// Host-only controls around exact minuitwrp parser and input discovery code.
#include "events-hooks.cpp"

[[noreturn]] static void fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}
static void require(bool value, const char* message) { if (!value) fail(message); }

struct Contact {
    ev device{};
    unsigned down = 0, up = 0, keyDown = 0, keyUp = 0;
    int lastPosition = 0, lastKey = 0;
    Contact() {
        device.touch = true;
        device.p.xi.maximum = device.mt_p.xi.maximum = gr_fb_width() - 1;
        device.p.yi.maximum = device.mt_p.yi.maximum = gr_fb_height() - 1;
    }
    int send(unsigned type, unsigned code, int value) {
        input_event event{};
        event.type = static_cast<__u16>(type);
        event.code = static_cast<__u16>(code);
        event.value = value;
        const int result = vk_modify(&device, &event);
        if (result != 0) return result;
        if (event.type == EV_ABS) {
            if (event.code == 0) ++up; else ++down;
            lastPosition = event.value;
        }
        if (event.type == EV_KEY) {
            if (event.value) ++keyDown; else ++keyUp;
            lastKey = event.code;
        }
        return result;
    }
    void sync() { send(EV_SYN, SYN_REPORT, 0); }
    void position(int x = 600, int y = 900) {
        send(EV_ABS, ABS_MT_POSITION_X, x);
        send(EV_ABS, ABS_MT_POSITION_Y, y);
    }
    void start(int id = 1, int x = 600, int y = 900) {
        send(EV_ABS, ABS_MT_TRACKING_ID, id);
        send(EV_ABS, ABS_MT_PRESSURE, 1);
        send(EV_ABS, ABS_MT_TOUCH_MAJOR, 8);
        position(x, y);
        sync();
    }
    void release() {
        send(EV_ABS, ABS_MT_TRACKING_ID, -1);
        send(EV_ABS, ABS_MT_PRESSURE, 0);
        send(EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
        sync();
    }
};

static void initialEmpty() {
    Contact pen;
    // Initial empty devices can synchronize without any coordinates or ID.
    for (int i = 0; i < 4; ++i) pen.sync();
    for (const unsigned code : {ABS_MT_PRESSURE, ABS_MT_TOUCH_MAJOR, ABS_MT_TRACKING_ID}) {
        pen.send(EV_ABS, code, code == ABS_MT_TRACKING_ID ? -1 : 0);
        pen.sync();
        pen.sync();
    }
    require(pen.up == 0 && pen.down == 0 && pen.keyUp == 0, "initial empty touch device emitted an up edge");
    std::puts("Empty touch slots and idle pen metadata emit no contact edges.");
}

static void interference() {
    Contact screen, pen, virtualTouch;
    screen.start();
    require(screen.down == 1, "touchscreen did not start");
    for (auto* idle : {&pen, &virtualTouch}) {
        idle->send(EV_ABS, ABS_MT_PRESSURE, 0); idle->sync();
        idle->send(EV_ABS, ABS_MT_TOUCH_MAJOR, 0); idle->sync();
        idle->send(EV_ABS, ABS_MT_TRACKING_ID, -1); idle->sync();
        for (int i = 0; i < 4; ++i) idle->sync();
        require(idle->up == 0 && idle->down == 0 && idle->keyUp == 0, "idle touch device released another device's contact");
    }
    screen.position(620, 930); screen.sync();
    require(screen.down == 2 && screen.up == 0, "idle device consumed touchscreen state");
    screen.release();
    require(screen.up == 1 && screen.lastPosition == ((620 << 16) | 930), "touchscreen release lost its own coordinates");
    for (int i = 0; i < 4; ++i) screen.sync();
    require(screen.up == 1 && screen.down == 2, "released contact produced duplicate edges");
    std::puts("Idle pen and virtual-touch releases preserve the touchscreen contact and release coordinates.");
}

static void interleaved() {
    Contact first, second;
    // A second device's release must not replace a partially buffered frame.
    first.position();
    second.send(EV_ABS, ABS_MT_PRESSURE, 0); second.sync();
    first.sync();
    require(first.down == 1 && second.up == 0, "interleaved frame lost its device ownership");
    second.start(2, 1200, 300);
    require(second.down == 1 && second.up == 0, "second device contact was consumed");
    first.send(EV_ABS, ABS_MT_PRESSURE, 0); first.sync();
    require(first.up == 1 && first.lastPosition == ((600 << 16) | 900), "interleaved release used another device's coordinates");
    second.release();
    require(second.up == 1 && second.lastPosition == ((1200 << 16) | 300), "second release lost its own coordinates");
    std::puts("Interleaved evdev parser frames retain separate coordinates and pending releases.");
}

static void compatibility() {
    Contact tracked;
    tracked.start(); tracked.release();
    require(tracked.up == 1, "tracking-ID release lost after trailing zeros");
    // A learned tracking-ID release protocol must not disable legacy release
    // detection on any other device, whether already active or newly seen.
    for (const std::string mode : {"single", "type-a", "pressure", "major"}) {
        Contact legacy;
        if (mode == "single") {
            legacy.send(EV_ABS, ABS_X, 600); legacy.send(EV_ABS, ABS_Y, 900);
        } else {
            legacy.position();
            if (mode == "type-a") legacy.send(EV_SYN, SYN_MT_REPORT, 0);
        }
        legacy.sync();
        if (mode == "pressure") legacy.send(EV_ABS, ABS_MT_PRESSURE, 0);
        if (mode == "major") legacy.send(EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
        legacy.sync();
        require(legacy.down == 1 && legacy.up == 1, "another device's tracking mode disabled legacy release");
    }
    std::puts("Single-touch, type-A, pressure and major releases survive another device learning tracking-ID lifts.");
}

static void virtualKeys() {
    Contact first, second, screen;
    virtualkey firstKey{KEY_BACK, 100, 200, 80, 80};
    virtualkey secondKey{KEY_HOME, 300, 400, 80, 80};
    first.device.vks = &firstKey; first.device.vk_count = 1;
    second.device.vks = &secondKey; second.device.vk_count = 1;
    first.start(1, 100, 200);
    second.start(2, 300, 400);
    screen.start(3);
    require(first.keyDown == 1 && second.keyDown == 1 && screen.down == 1, "virtual-key discard state escaped its device");
    first.release();
    require(first.keyUp == 1 && first.lastKey == KEY_BACK, "virtual-key release used another device's scancode");
    screen.position(650, 950); screen.sync();
    require(screen.down == 2 && screen.up == 0, "virtual-key release disturbed screen contact");
    second.release(); screen.release();
    require(second.keyUp == 1 && second.lastKey == KEY_HOME && screen.up == 1, "virtual-key ownership changed at lift");
    std::puts("Virtual-key discard and key-up scancodes are isolated from other devices.");
}

static void dropped() {
    Contact first, second;
    first.start(); second.start(2, 1200, 300); second.release(); second.start(3);
    require(second.send(EV_SYN, SYN_DROPPED, 0) == 0 && rescan_input, "dropped-event rescan/cancellation contract changed");
    // vk_modify resets only the lost stream. ev_get retains its existing full
    // rescan and GUI cancellation contract; concurrent GUI ownership is separate.
    first.release();
    require(first.up == 1, "dropped stream reset another parser's active contact");
    const unsigned oldUp = second.up;
    second.send(EV_ABS, ABS_MT_PRESSURE, 0); second.sync();
    require(second.up == oldUp, "dropped stream retained a phantom active contact");
    second.send(EV_ABS, ABS_X, 600); second.send(EV_ABS, ABS_Y, 900); second.sync(); second.sync();
    require(second.up == oldUp + 1, "dropped stream retained learned tracking-ID release mode");
    std::puts("SYN_DROPPED clears local contact/protocol state and preserves the existing rescan signal.");
}

static void rescan() {
    devices = 2; relative_mouse = false; absolute_touch = true;
    ev_init(); require(ev_count == 2, "fake touch discovery failed");
    const auto packet = [](ev& device, unsigned type, unsigned code, int value) {
        input_event event{}; event.type = static_cast<__u16>(type); event.code = static_cast<__u16>(code); event.value = value;
        return vk_modify(&device, &event);
    };
    // Learn a different mode in each slot and leave the second contact active.
    packet(evs[0], EV_ABS, ABS_MT_TRACKING_ID, -1); packet(evs[0], EV_SYN, SYN_REPORT, 0);
    packet(evs[1], EV_ABS, ABS_MT_POSITION_X, 600); packet(evs[1], EV_ABS, ABS_MT_POSITION_Y, 900); packet(evs[1], EV_SYN, SYN_REPORT, 0);
    ++modified; ++now_seconds;
    input_event event{};
    require(ev_get(&event, 0) == 0 && event.type == EV_SYN && event.code == SYN_DROPPED, "hotplug did not retain GUI cancellation");
    require(ev_count == 2 && closes == 2, "hotplug did not rebuild device slots");
    for (unsigned i = 0; i < ev_count; ++i) {
        require(packet(evs[i], EV_SYN, SYN_REPORT, 0) != 0, "rescanned empty slot inherited a contact");
        require(packet(evs[i], EV_SYN, SYN_REPORT, 0) != 0, "rescanned empty slot emitted a phantom up");
        packet(evs[i], EV_ABS, ABS_MT_POSITION_X, 600); packet(evs[i], EV_ABS, ABS_MT_POSITION_Y, 900);
        require(packet(evs[i], EV_SYN, SYN_REPORT, 0) == 0, "rescanned slot did not start contact");
        event = {}; event.type = EV_SYN; event.code = SYN_REPORT;
        require(vk_modify(&evs[i], &event) == 0 && event.type == EV_ABS && event.code == 0, "rescanned slot retained old release protocol");
    }
    ev_exit();
    std::puts("Actual ev_init/ev_get hotplug clears all reused slots, including tracking mode and contact defaults.");
}

int main(int argc, char** argv) {
    require(argc == 2, "scenario required");
    const std::string mode = argv[1];
    if (mode == "empty") initialEmpty();
    else if (mode == "interference") interference();
    else if (mode == "interleaved") interleaved();
    else if (mode == "compatibility") compatibility();
    else if (mode == "virtual-keys") virtualKeys();
    else if (mode == "dropped") dropped();
    else if (mode == "rescan") rescan();
    else fail("unknown scenario");
}
