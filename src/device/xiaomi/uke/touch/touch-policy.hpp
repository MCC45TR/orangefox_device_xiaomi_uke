// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace uke::touch {
inline constexpr int kOwnerFd = 3, kStatusFd = 4;
inline constexpr std::uint32_t kStatusMagic = 0x554b5431;
inline constexpr std::size_t kMountInfoCap = 1024 * 1024, kListCap = 64 * 1024;
inline constexpr std::size_t kLogCap = 1024 * 1024;
inline constexpr std::uint64_t kOverlayBytes = 16 * 1024 * 1024;
inline constexpr std::uint64_t kStartupMs = 30000, kMappingMs = 10000, kGraceMs = 2000;
enum class State : std::uint32_t { idle, preparing, running, stopped, refused, failed };
enum class Code : std::uint32_t {
    ok,
    owner_invalid,
    owner_gone,
    duplicate_session,
    profile_incomplete,
    image_provider_unapproved,
    namespace_failed,
    namespace_shared,
    mapping_missing,
    mapping_identity,
    snapshot_unresolved,
    mount_invalid,
    filesystem_unknown,
    filesystem_replay,
    overlay_failed,
    persistent_alias,
    pmsg_failed,
    hash_mismatch,
    elf_invalid,
    linker_failed,
    provider_changed,
    exec_failed,
    child_exited,
    startup_timeout,
    signal_stop,
    io_failed,
    cleanup_failed,
    spawn_failed,
    status_invalid
};
struct Status {
    std::uint32_t magic = kStatusMagic, version = 1;
    State state = State::idle;
    Code code = Code::ok;
    std::uint64_t drained = 0, retained = 0;
};
static_assert(sizeof(Status) == 32);
inline bool valid_status(const Status &s) {
    return s.magic == kStatusMagic && s.version == 1 && s.state <= State::failed &&
           s.code <= Code::status_invalid && s.retained <= kLogCap && s.retained <= s.drained;
}
// Readiness belongs to one recovery process, never to a page or theme.
class Readiness {
  public:
    void graphics(bool ok, bool scan_done) noexcept {
        graphics_ = ok;
        scan_ = scan_done;
    }
    void resources(bool ok) noexcept { resources_ = ok; }
    bool frame(bool awake) noexcept {
        if (attempted_ || stopped_ || !graphics_ || !scan_ || !resources_ || !awake)
            return false;
        attempted_ = true;
        return true;
    }
    void stop() noexcept { stopped_ = true; }
    bool attempted() const noexcept { return attempted_; }

  private:
    bool graphics_ = false, scan_ = false, resources_ = false, attempted_ = false, stopped_ = false;
};
struct Lifecycle {
    State state = State::idle;
    int owned_pid = -1;
    bool prepared() noexcept {
        if (state != State::idle)
            return false;
        state = State::preparing;
        return true;
    }
    bool own(int pid) noexcept {
        if (state != State::preparing || pid <= 1 || owned_pid != -1)
            return false;
        owned_pid = pid;
        return true;
    }
    bool exec_ok() noexcept {
        if (state != State::preparing || owned_pid <= 1)
            return false;
        state = State::running;
        return true;
    }
    void terminal(State next) noexcept {
        if (next == State::stopped || next == State::refused || next == State::failed)
            state = next;
    }
    bool may_signal(int pid) const noexcept { return owned_pid > 1 && pid == owned_pid; }
    bool reaped(int pid) noexcept {
        if (!may_signal(pid))
            return false;
        owned_pid = -1;
        return true;
    }
};
struct LogBudget {
    std::uint64_t drained = 0, retained = 0;
    std::size_t consume(std::size_t bytes) noexcept {
        const auto room = kLogCap - retained;
        const auto keep = std::min<std::uint64_t>(room, bytes);
        drained = bytes > UINT64_MAX - drained ? UINT64_MAX : drained + bytes;
        retained += keep;
        return static_cast<std::size_t>(keep);
    }
};
inline bool number(std::string_view value, unsigned &result) {
    if (value.empty())
        return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
inline bool service_process_path(std::string_view path, std::string_view service) {
    return path == service ||
           (path.size() == service.size() + 10 && path.starts_with(service) &&
            path.substr(service.size()) == " (deleted)");
}
inline bool device(std::string_view text, unsigned &major, unsigned &minor) {
    const auto pos = text.find(':');
    return pos != std::string_view::npos && number(text.substr(0, pos), major) &&
           number(text.substr(pos + 1), minor);
}
inline bool token(std::string_view list, std::string_view wanted) {
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto end = list.find(',', start);
        if (list.substr(start, end == std::string_view::npos ? end : end - start) == wanted)
            return true;
        if (end == std::string_view::npos)
            return false;
        start = end + 1;
    }
    return false;
}
inline std::optional<std::string> unescape(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            if (value[i] == '\0')
                return {};
            result.push_back(value[i]);
            continue;
        }
        if (i + 3 >= value.size())
            return {};
        const auto escape = value.substr(i + 1, 3);
        if (escape == "040")
            result.push_back(' ');
        else if (escape == "011")
            result.push_back('\t');
        else if (escape == "012")
            result.push_back('\n');
        else if (escape == "134")
            result.push_back('\\');
        else
            return {};
        i += 3;
    }
    return result;
}
struct Mount {
    unsigned id = 0, parent = 0, major = 0, minor = 0;
    std::string root, path, options, fs, source, super;
    bool propagation = false, visible = true;
    bool readonly() const { return token(options, "ro") && token(super, "ro"); }
};
inline std::optional<std::vector<Mount>> mountinfo(std::string_view text) {
    if (text.empty() || text.size() > kMountInfoCap)
        return {};
    std::istringstream lines{std::string(text)};
    std::vector<Mount> result;
    std::set<unsigned> ids;
    std::string line;
    while (std::getline(lines, line)) {
        if (result.size() >= 4096 || line.size() > 8192)
            return {};
        std::istringstream stream(line);
        std::vector<std::string> fields;
        for (std::string field; stream >> field;)
            fields.push_back(std::move(field));
        if (fields.size() < 10)
            return {};
        const auto separator = std::find(fields.begin() + 6, fields.end(), "-");
        if (separator == fields.end() || fields.end() - separator != 4)
            return {};
        Mount m;
        if (!number(fields[0], m.id) || m.id == 0 || !ids.insert(m.id).second ||
            !number(fields[1], m.parent) || !device(fields[2], m.major, m.minor))
            return {};
        const auto root = unescape(fields[3]), path = unescape(fields[4]),
                   source = unescape(*(separator + 2));
        if (!root || !path || !source || path->empty() || path->front() != '/' || root->empty() ||
            root->front() != '/')
            return {};
        m.root = *root;
        m.path = *path;
        m.source = *source;
        m.options = fields[5];
        m.fs = *(separator + 1);
        m.super = *(separator + 3);
        for (auto it = fields.begin() + 6; it != separator; ++it)
            if (it->starts_with("shared:") || it->starts_with("master:") ||
                it->starts_with("propagate_from:"))
                m.propagation = true;
        result.push_back(std::move(m));
    }
    if (result.empty())
        return {};
    return result;
}
inline bool beneath(std::string_view path, std::string_view root) {
    return path == root ||
           (path.size() > root.size() && path.starts_with(root) && path[root.size()] == '/');
}
inline bool ramfs(std::string_view fs) { return fs == "tmpfs" || fs == "ramfs" || fs == "rootfs"; }
inline bool kernel_api(std::string_view fs) {
    return fs == "proc" || fs == "sysfs" || fs == "devtmpfs" || fs == "devpts" ||
           fs == "selinuxfs" || fs == "binder" || fs == "binderfs" || fs == "cgroup" ||
           fs == "cgroup2" || fs == "configfs" || fs == "functionfs";
}
enum class MountPhase { isolated, init_preparation };
// Overlaying a root-owned, full, read-only calibration partition does not
// require changing its OEM directory mode. This exception never admits a
// writable mount, subdirectory bind, different device or different pathname.
inline bool readonly_persist_overlay(const Mount& mount, unsigned device_major,
                                    unsigned device_minor, unsigned owner, unsigned mode) {
    return owner == 0 && (mode & 0170000) == 0040000 && mount.visible &&
           mount.path == "/persist" && mount.root == "/" && mount.fs == "ext4" &&
           mount.major != 0 && mount.major == device_major && mount.minor == device_minor &&
           mount.readonly() && token(mount.options,"nosuid") && token(mount.options,"nodev");
}
inline Code mount_policy(const std::vector<Mount> &mounts, bool final, bool real_cache,
                         MountPhase phase = MountPhase::isolated) {
    // Init owns inherited shared mounts. Only the verified init-namespace
    // preparation may see them; an OEM execution view must always be private.
    if (final && phase != MountPhase::isolated)
        return Code::namespace_shared;
    const std::array<std::string_view, 8> protected_paths = {"/data", "/metadata", "/mnt",
        "/dev/socket", "/cache", "/persist", "/sys/fs/pstore", "/dev/block"};
    for (const auto &m : mounts) {
        if (!m.visible)
            continue;
        if (m.propagation && phase != MountPhase::init_preparation)
            return Code::namespace_shared;
        // Device numbers, not source spelling, decide block-backed policy.
        if (m.major != 0 && !m.readonly())
            return Code::mount_invalid;
        if (!m.readonly() && !ramfs(m.fs) && !kernel_api(m.fs) &&
            !(!final && m.path == "/sys/fs/pstore" && m.fs == "pstore"))
            return Code::mount_invalid;
        if (final && beneath(m.path, "/persist") && !ramfs(m.fs))
            return Code::persistent_alias;
        if (final)
            for (const auto path : protected_paths) {
                if (path == "/cache" && !real_cache)
                    continue;
                if (beneath(m.path, path) && (!ramfs(m.fs) || m.major != 0))
                    return Code::persistent_alias;
            }
    }
    if (final)
        for (const auto path : protected_paths) {
            if (path == "/cache" && !real_cache)
                continue;
            // A stack of old mounts at one mountpoint is ambiguous: refuse it.
            const Mount *found = nullptr;
            for (const auto &m : mounts)
                if (m.visible && m.path == path) {
                    if (found)
                        return Code::overlay_failed;
                    found = &m;
                }
            if (!found || found->fs != "tmpfs" || found->root != "/" ||
                !token(found->options, "nosuid") || !token(found->options, "nodev") ||
                !token(found->options, "noexec") || !token(found->super, "size=16384k") ||
                token(found->options, "ro"))
                return Code::overlay_failed;
        }
    return Code::ok;
}
enum class Filesystem { unknown, ext4, erofs };
inline Filesystem signature(const std::vector<unsigned char> &bytes) {
    if (bytes.size() < 2048)
        return Filesystem::unknown;
    const bool ext4 = bytes[1080] == 0x53 && bytes[1081] == 0xef;
    const bool erofs =
        bytes[1024] == 0xe2 && bytes[1025] == 0xe1 && bytes[1026] == 0xf5 && bytes[1027] == 0xe0;
    if (ext4 == erofs)
        return Filesystem::unknown;
    return ext4 ? Filesystem::ext4 : Filesystem::erofs;
}
inline bool ro_provider_mount(const Mount &m, unsigned major, unsigned minor, Filesystem fs) {
    const bool type =
        (fs == Filesystem::ext4 && m.fs == "ext4") || (fs == Filesystem::erofs && m.fs == "erofs");
    return type && m.major == major && m.minor == minor && m.root == "/" && m.readonly() &&
           token(m.options, "nosuid") && token(m.options, "nodev") &&
           (fs != Filesystem::ext4 || token(m.super, "norecovery") || token(m.super, "noload"));
}
inline bool slot(std::string_view suffix) { return suffix == "_a" || suffix == "_b"; }
inline bool cache_link(std::string_view link) { return link == "/data/cache"; }
} // namespace uke::touch
