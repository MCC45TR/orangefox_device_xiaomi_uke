// SPDX-License-Identifier: GPL-3.0-or-later
#define main touch_supervisor_not_invoked_on_host
#include "touch-supervisor.cpp"
#undef main
#include "touch-gui-session.hpp"
#include <iostream>
namespace {
unsigned passed = 0;
void check(bool value, const char *description) {
    if (!value) {
        std::cerr << "FAIL " << description << '\n';
        std::exit(1);
    }
    ++passed;
}
std::vector<uke::touch::Mount> parse(std::string_view text) {
    auto result = uke::touch::mountinfo(text);
    check(result.has_value(), "fixture mountinfo parse");
    return *result;
}
std::vector<unsigned char> dm_fixture(unsigned count = 3) {
    std::vector<unsigned char> bytes(1024);
    dm_ioctl io{};
    io.version[0] = DM_VERSION_MAJOR;
    io.flags = DM_ACTIVE_PRESENT_FLAG;
    io.dev = makedev(253, 4);
    io.data_start = sizeof(io);
    io.target_count = count;
    std::strcpy(io.name, "odm_a");
    std::size_t offset = io.data_start;
    for (unsigned i = 0; i < count; ++i) {
        dm_target_spec t{};
        t.sector_start = i * 100;
        t.length = 100;
        std::strcpy(t.target_type, "linear");
        constexpr std::size_t width = 56;
        t.next = (i + 1) * width;
        std::memcpy(bytes.data() + offset, &t, sizeof(t));
        std::memcpy(bytes.data() + offset + sizeof(t), "8:0 2048", 9);
        offset += width;
    }
    io.data_size = offset;
    std::memcpy(bytes.data(), &io, sizeof(io));
    return bytes;
}
void controls(const std::string &root) {
    using namespace uke::touch;
    check(service_process_path(kService, kService), "exact external service identity");
    check(service_process_path(std::string(kService) + " (deleted)", kService),
          "deleted external service identity");
    check(!service_process_path(std::string(kService) + "-other", kService),
          "prefix process cannot match");
    check(!service_process_path("/system/bin/uke-touch-supervisor", kService),
          "manager is distinct from OEM service");
    Readiness ready;
    check(!ready.frame(true), "no early launch");
    ready.graphics(false, true);
    ready.resources(true);
    check(!ready.frame(true), "graphics failure");
    ready.graphics(true, false);
    check(!ready.frame(true), "scan required");
    ready.graphics(true, true);
    ready.resources(false);
    check(!ready.frame(true), "resources required");
    ready.resources(true);
    check(!ready.frame(false), "awake frame required");
    check(ready.frame(true), "first valid frame starts");
    for (unsigned i = 0; i < 20; ++i) {
        ready.resources(false);
        ready.resources(true);
        check(!ready.frame(true), "theme/page cannot restart");
    }
    Readiness stopped;
    stopped.graphics(true, true);
    stopped.resources(true);
    stopped.stop();
    check(!stopped.frame(true), "stop before first page");
    Lifecycle life;
    check(!life.exec_ok(), "exec without owner rejected");
    check(life.prepared(), "prepare once");
    check(!life.prepared(), "preparing cannot repeat");
    check(!life.own(1), "pid1 forbidden");
    check(life.own(321), "owned pid admission");
    check(!life.own(654), "owner replacement rejected");
    check(!life.may_signal(654), "stale pid not owned");
    check(life.exec_ok(), "successful exec state");
    life.terminal(State::failed);
    check(!life.prepared() && !life.exec_ok(), "terminal failure cannot respawn");
    check(!life.reaped(654) && life.reaped(321) && !life.may_signal(321),
          "only exact child is reaped");
    check(slot("_a") && slot("_b") && !slot("a") && !slot("_c"), "slot contract");
    check(cache_link("/data/cache") && !cache_link("data/cache") && !cache_link("/persist/cache"),
          "cache link exact");
    auto mounts = parse("1 0 0:1 / / rw - rootfs rootfs rw\n2 1 253:4 / /odm ro,nosuid,nodev - "
                        "ext4 /dev/root ro,norecovery\n");
    check(mount_policy(mounts, false, false) == Code::ok, "read-only block alias");
    mounts[1].options = "rw";
    check(mount_policy(mounts, false, false) == Code::mount_invalid, "rw block alias rejected");
    mounts[1].visible = false;
    check(mount_policy(mounts, false, false) == Code::ok, "covered mount excluded by kernel ID");
    mounts[0].propagation = true;
    check(mount_policy(mounts, false, false) == Code::namespace_shared,
          "recursive propagation gate");
    check(!mountinfo("1 0 0:1 / / rw - tmpfs\n"), "truncated mountinfo");
    check(!mountinfo("1 0 0:1 / / rw - tmpfs x rw\n1 0 0:2 / /x rw - tmpfs x rw\n"),
          "duplicate mount ID");
    auto escaped = parse("3 1 0:2 / /space\\040path rw - tmpfs tmpfs rw\n");
    check(escaped[0].path == "/space path", "mountinfo escapes");
    check(!unescape("/bad\\777"), "unknown escape rejected");
    auto overlays =
        parse("1 0 0:1 / / rw - rootfs rootfs rw\n2 1 0:2 / /data rw,nosuid,nodev,noexec - tmpfs "
              "ram rw,size=16384k\n3 1 0:3 / /metadata rw,nosuid,nodev,noexec - tmpfs ram "
              "rw,size=16384k\n4 1 0:4 / /mnt rw,nosuid,nodev,noexec - tmpfs ram rw,size=16384k\n5 "
              "1 0:5 / /dev/socket rw,nosuid,nodev,noexec - tmpfs ram rw,size=16384k\n"
              "9 1 0:9 / /persist rw,nosuid,nodev,noexec - tmpfs ram rw,size=16384k\n"
              "10 1 0:10 / /sys/fs/pstore rw,nosuid,nodev,noexec - tmpfs ram rw,size=16384k\n"
              "11 1 0:11 / /dev/block rw,nosuid,nodev,noexec - tmpfs ram rw,size=16384k\n");
    check(mount_policy(overlays, true, false) == Code::ok, "bounded overlays");
    overlays[1].super = "rw,size=32768k";
    check(mount_policy(overlays, true, false) == Code::overlay_failed, "RAM cap mutation rejected");
    overlays[1].super = "rw,size=16384k";
    overlays[1].options = "rw,nosuid,nodev";
    check(mount_policy(overlays, true, false) == Code::overlay_failed, "noexec flag required");
    overlays[1].options = "rw,nosuid,nodev,noexec";
    auto nested = parse("6 2 253:5 / /data/nested rw - ext4 /dev/mapper/alias rw\n");
    overlays.push_back(nested[0]);
    check(mount_policy(overlays, true, false) == Code::mount_invalid, "writable nested submount");
    overlays.back().options = "ro";
    overlays.back().super = "ro";
    check(mount_policy(overlays, true, false) == Code::persistent_alias,
          "even read-only protected persistent submount rejected");
    overlays.back().visible = false;
    check(mount_policy(overlays, true, false) == Code::ok, "hidden nested submount");
    auto fuse = parse("8 1 0:9 / /alias rw - fuse.sshfs user rw\n");
    check(mount_policy(fuse, false, false) == Code::mount_invalid, "user-backed alias");
    fuse[0].path = "/persist";
    fuse[0].fs = "ext4";
    fuse[0].options = "ro";
    fuse[0].super = "ro";
    check(mount_policy(fuse, false, false) == Code::ok, "read-only persist before isolation");
    check(mount_policy(fuse, true, false) == Code::persistent_alias, "uncovered persist rejected");
    auto pstore = parse("12 1 0:12 / /sys/fs/pstore rw - pstore pstore rw\n");
    check(mount_policy(pstore, false, false) == Code::ok, "pstore allowed only before overlay");
    check(mount_policy(pstore, true, false) != Code::ok, "pstore hidden before OEM execution");
    std::vector<unsigned char> bytes(2048);
    check(signature(bytes) == Filesystem::unknown, "unknown signature");
    bytes[1080] = 0x53;
    bytes[1081] = 0xef;
    check(signature(bytes) == Filesystem::ext4, "ext4 probe");
    Mount ro = mounts[1];
    ro.visible = true;
    ro.options = "ro,nosuid,nodev";
    ro.super = "ro,norecovery";
    check(ro_provider_mount(ro, 253, 4, Filesystem::ext4), "ext4 no replay");
    ro.super = "ro";
    check(!ro_provider_mount(ro, 253, 4, Filesystem::ext4), "ext4 replay mutation rejected");
    check(!ro_provider_mount(ro, 253, 5, Filesystem::ext4), "wrong mounted device");
    bytes[1024] = 0xe2;
    bytes[1025] = 0xe1;
    bytes[1026] = 0xf5;
    bytes[1027] = 0xe0;
    check(signature(bytes) == Filesystem::unknown, "ambiguous signature");
    bytes[1080] = 0;
    bytes[1081] = 0;
    check(signature(bytes) == Filesystem::erofs, "erofs probe");
    bytes.resize(1081);
    check(signature(bytes) == Filesystem::unknown, "short probe");
    auto table = dm_fixture();
    auto back = linear_reply(table, 253, 4, "odm_a");
    check(back && back->size() == 3 && back->back().start == 2048,
          "multi-target STATUS next offset");
    check(!linear_reply(table, 253, 5, "odm_a") && !linear_reply(table, 253, 4, "vendor_a"),
          "wrong mapping identity");
    dm_ioctl header{};
    std::memcpy(&header, table.data(), sizeof(header));
    header.flags |= DM_INACTIVE_PRESENT_FLAG;
    std::memcpy(table.data(), &header, sizeof(header));
    check(!linear_reply(table, 253, 4, "odm_a"), "pending inactive table");
    table = dm_fixture();
    dm_target_spec target{};
    std::memcpy(&target, table.data() + sizeof(dm_ioctl), sizeof(target));
    std::strcpy(target.target_type, "snapshot");
    std::memcpy(table.data() + sizeof(dm_ioctl), &target, sizeof(target));
    check(!linear_reply(table, 253, 4, "odm_a"), "snapshot target blocked");
    std::strcpy(target.target_type, "user");
    std::memcpy(table.data() + sizeof(dm_ioctl), &target, sizeof(target));
    check(!linear_reply(table, 253, 4, "odm_a"), "dm-user blocked");
    table = dm_fixture();
    table.resize(sizeof(dm_ioctl) + 10);
    check(!linear_reply(table, 253, 4, "odm_a"), "truncated mapper reply");
    auto selected =
        providers("linux-vdso.so.1 => [vdso] (0xabc)\nlibc.so => /system/lib64/libc.so (0x123)\n");
    check(selected == std::set<std::string>{"/system/lib64/libc.so"},
          "reviewed linker grammar and VDSO");
    for (const auto text :
         {"not a list\n", "foo => /system/lib64/../libc.so (0x12)\n", "evil => [vdso] (0x12)\n"}) {
        bool refused = false;
        try {
            providers(text);
        } catch (Code) {
            refused = true;
        }
        check(refused, "unexpected linker path/output");
    }
    profile_gate();
    check(std::size(kInstalledFiles) == 21 && kImageFiles.size() == 18 &&
          kInputModules.size() == 2, "exact installed and image input inventory");
    check(firmware_lookup("/vendor/firmware,/vendor/other,") ==
          "/odm/firmware,/vendor/firmware,/vendor/other,", "preserve inherited firmware order");
    check(firmware_lookup("/odm/firmware,/vendor/firmware,") ==
          "/odm/firmware,/vendor/firmware,", "firmware prefix is idempotent");
    for (auto path : {"", "/vendor/../persist", "/vendor/firmware\n", "relative",
                      "/odm/firmware,/odm/firmware", "/vendor/firmware,,/vendor/other"})
        check(!firmware_lookup(path), "invalid firmware lookup refuses");
    check(!firmware_lookup("/" + std::string(250, 'a')), "firmware append cannot truncate");
    LogBudget budget;
    check(budget.consume(kLogCap - 1) == kLogCap - 1 && budget.consume(4096) == 1 &&
              budget.consume(4096) == 0 && budget.drained > budget.retained &&
              budget.retained == kLogCap,
          "cap continues draining");
    Status s;
    s.state = State::running;
    s.drained = 100;
    s.retained = 100;
    check(valid_status(s), "valid fixed status");
    s.retained = kLogCap + 1;
    check(!valid_status(s), "status cap mutation");
    // Child controls below start ONLY the disposable host fixture, never HAL.
    int owner_raw[2], status_raw[2];
    check(::pipe2(owner_raw, O_CLOEXEC) == 0 && ::pipe2(status_raw, O_CLOEXEC) == 0,
          "control pipes");
    int owner_read = ::fcntl(owner_raw[0], F_DUPFD_CLOEXEC, 20),
        owner_write = ::fcntl(owner_raw[1], F_DUPFD_CLOEXEC, 20),
        status_read = ::fcntl(status_raw[0], F_DUPFD_CLOEXEC, 20),
        status_write = ::fcntl(status_raw[1], F_DUPFD_CLOEXEC, 20);
    for (int fd : {owner_raw[0], owner_raw[1], status_raw[0], status_raw[1]})
        ::close(fd);
    check(::dup2(owner_read, kOwnerFd) == kOwnerFd && ::dup2(status_write, kStatusFd) == kStatusFd,
          "fixed controls");
    ::close(owner_read);
    ::close(status_write);
    {
        Child child;
        const std::string path = root + "/fixture-hang";
        child.start(path);
        check(child.confirm_exec(now_ms() + 2000), "actual successful exec");
        const int pid = child.life.owned_pid;
        check(child.group && ::getpgid(pid) == pid, "owned process group");
        check(child.cleanup() && child.life.owned_pid == -1, "TERM cleanup and exact reap");
        int exit = 0;
        check(::waitpid(pid, &exit, WNOHANG) < 0 && errno == ECHILD, "child actually reaped");
    }
    {
        Child child;
        const std::string path = root + "/missing-executable";
        child.start(path);
        bool failed = false;
        try {
            child.confirm_exec(now_ms() + 2000);
        } catch (Code code) {
            failed = code == Code::exec_failed;
        }
        check(failed && child.cleanup(), "exec failure pipe and cleanup");
    }
    {
        Child child;
        const std::string path = root + "/fixture-stubborn";
        child.start(path);
        check(child.confirm_exec(now_ms() + 2000), "stubborn exec");
        ::poll(nullptr, 0, 50);
        const auto start = now_ms();
        check(child.cleanup() && now_ms() - start >= 1900 && now_ms() - start < 3500,
              "TERM deadline then KILL and reap");
    }
    {
        Child child;
        const std::string path = root + "/fixture-exit";
        child.start(path);
        check(child.confirm_exec(now_ms() + 2000), "exit child exec receipt");
        for (unsigned i = 0; i < 100 && !child.exited(); ++i)
            ::poll(nullptr, 0, 10);
        check(child.exited() && child.cleanup(), "normal child exit terminal cleanup");
    }
    {
        Child child;
        const std::string path = root + "/fixture-flood";
        child.start(path);
        check(child.confirm_exec(now_ms() + 2000), "flood exec");
        LogBudget log_budget;
        Fd log(::open((root + "/host.private.log").c_str(),
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
        check(log.get() >= 0, "bounded host log fixture");
        const auto deadline = now_ms() + 5000;
        while (!child.exited() && now_ms() < deadline) {
            drain(child, log, log_budget);
            ::poll(nullptr, 0, 1);
        }
        drain(child, log, log_budget);
        check(child.exited() && log_budget.drained == 2 * kLogCap &&
                  log_budget.retained == kLogCap && child.cleanup(),
              "2MiB stdout drained with 1MiB cap and no pipe blockage");
    }
    {
        Child child;
        const std::string path = root + "/fixture-hang";
        child.start(path);
        check(child.confirm_exec(now_ms() + 2000), "owner HUP fixture");
        ::close(owner_write);
        owner_write = -1;
        check(!owner_alive() && child.cleanup(), "GUI owner HUP and child cleanup");
    }
    for (int fd : {kOwnerFd, kStatusFd, status_read})
        ::close(fd);
    if (owner_write >= 0)
        ::close(owner_write);
    // GUI holder's missing fixed manager on the host propagates once-only fail.
    graphics_ready(true, true);
    resources_ready(true);
    start_once_after_frame(true);
#ifdef UKE_TOUCH_GUI_HOST_CONTROLS
    const auto gui_deadline = now_ms() + 2000;
    while (collected_status().state == State::preparing && now_ms() < gui_deadline) {
        poll_status();
        ::poll(nullptr, 0, 10);
    }
    check(collected_status().state == State::running, "GUI actual posix_spawn and status pipe");
    resources_ready(false);
    resources_ready(true);
    start_once_after_frame(true);
    poll_status();
    check(collected_status().state == State::running, "GUI page and theme retain session");
    stop();
    check(collected_status().state == State::stopped, "GUI owner close and final exact reap");
#else
    check(collected_status().state == State::failed &&
              collected_status().code == Code::spawn_failed,
          "GUI spawn failure propagated");
    start_once_after_frame(true);
    check(collected_status().code == Code::spawn_failed, "GUI failure cannot respawn");
    stop();
    check(collected_status().state == State::failed, "GUI teardown preserves failure");
#endif
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2)
        return 64;
    ::signal(SIGPIPE, SIG_IGN);
    controls(argv[1]);
    std::cout << "PASS " << passed << " native host controls; no tablet/OEM/namespace activation\n";
}
