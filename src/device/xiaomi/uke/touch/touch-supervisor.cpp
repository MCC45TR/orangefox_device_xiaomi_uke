// SPDX-License-Identifier: GPL-3.0-or-later
// Own the exact installed THP service for one OrangeFox process; no input bridge.
#include "touch-dm-policy.hpp"
#include "touch-policy.hpp"
#include "touch-profile.hpp"
#include "touch-startup-policy.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/dm-ioctl.h>
#include <linux/stat.h>
#include <memory>
#include <openssl/evp.h>
#include <poll.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace uke::touch {
namespace {
volatile sig_atomic_t interrupted = 0;
State published_state = State::idle;
void signal_handler(int signal) { interrupted = signal; }
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() {
        if (value >= 0)
            ::close(value);
    }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd &&other) noexcept : value(other.value) { other.value = -1; }
    Fd &operator=(Fd &&other) noexcept {
        if (this != &other) {
            if (value >= 0)
                ::close(value);
            value = other.value;
            other.value = -1;
        }
        return *this;
    }
    int get() const { return value; }
    int release() {
        const int out = value;
        value = -1;
        return out;
    }
};
void require(bool ok, Code code) {
    if (!ok)
        throw code;
}
std::uint64_t now_ms() {
    timespec t{};
    require(::clock_gettime(CLOCK_MONOTONIC, &t) == 0, Code::io_failed);
    return static_cast<std::uint64_t>(t.tv_sec) * 1000 + static_cast<unsigned>(t.tv_nsec) / 1000000;
}
bool owner_alive() {
    pollfd p{kOwnerFd, POLLIN | POLLHUP | POLLERR, 0};
    const int rc = ::poll(&p, 1, 0);
    return rc == 0 || (rc > 0 && !(p.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)));
}
void preparing_check(std::uint64_t deadline) {
    require(interrupted != SIGALRM && now_ms() < deadline, Code::startup_timeout);
    require(interrupted == 0, Code::signal_stop);
    require(owner_alive(), Code::owner_gone);
}
void nonblock(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    require(flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, Code::io_failed);
}
void cloexec(int fd) { require(::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, Code::io_failed); }
void require_no_external_service(std::string_view service) {
    const auto close_directory = [](DIR *directory) { ::closedir(directory); };
    std::unique_ptr<DIR, decltype(close_directory)> processes(::opendir("/proc"), close_directory);
    require(processes != nullptr, Code::io_failed);
    unsigned visited = 0;
    while (auto *entry = ::readdir(processes.get())) {
        unsigned pid = 0;
        if (!number(entry->d_name, pid) || pid <= 1 || pid == static_cast<unsigned>(::getpid()))
            continue;
        require(++visited <= 32768, Code::io_failed);
        const auto path = "/proc/" + std::to_string(pid) + "/exe";
        std::array<char, 1024> target{};
        const auto size = ::readlink(path.c_str(), target.data(), target.size());
        if (size < 0 && (errno == ENOENT || errno == ESRCH))
            continue;
        require(size >= 0 && static_cast<std::size_t>(size) < target.size(), Code::io_failed);
        require(!service_process_path(std::string_view(target.data(), size), service),
                Code::duplicate_session);
    }
}
void status(State state, Code code, const LogBudget &budget = {}) {
    const Status s{kStatusMagic, 1, state, code, budget.drained, budget.retained};
    published_state = state;
    // Atomic <= PIPE_BUF message; a GUI that is not collecting cannot block us.
    const ssize_t n = ::write(kStatusFd, &s, sizeof(s));
    require(n == static_cast<ssize_t>(sizeof(s)), Code::owner_gone);
}
Fd open_path(std::string_view path, int flags, mode_t mode = 0) {
    require(!path.empty() && path.front() == '/' && path.size() < 512 && path.back() != '/',
            Code::io_failed);
    Fd dir(::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC));
    require(dir.get() >= 0, Code::io_failed);
    std::size_t begin = 1;
    while (true) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(begin, end == std::string_view::npos ? end : end - begin);
        require(!part.empty() && part != "." && part != "..", Code::io_failed);
        const std::string name(part);
        if (end == std::string_view::npos) {
            Fd file(::openat(dir.get(), name.c_str(), flags | O_NOFOLLOW | O_CLOEXEC, mode));
            require(file.get() >= 0, Code::io_failed);
            return file;
        }
        Fd next(::openat(dir.get(), name.c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        require(next.get() >= 0, Code::io_failed);
        dir = std::move(next);
        begin = end + 1;
    }
}
// Proc/sys expose kernel-maintained symlinks; use these only for fixed metadata.
std::string read_kernel(const std::string &path, std::size_t cap) {
    Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    require(fd.get() >= 0, Code::io_failed);
    std::string result;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto n = ::read(fd.get(), buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR) {
            require(!interrupted, Code::signal_stop);
            continue;
        }
        require(n >= 0, Code::io_failed);
        if (n == 0)
            break;
        require(result.size() + static_cast<std::size_t>(n) <= cap, Code::io_failed);
        result.append(buffer.data(), static_cast<std::size_t>(n));
    }
    return result;
}
std::vector<Mount> current_mounts() {
    const auto parsed = mountinfo(read_kernel("/proc/self/mountinfo", kMountInfoCap));
    require(parsed.has_value(), Code::mount_invalid);
    auto mounts = *parsed;
    for (auto &m : mounts) {
        struct statx visible{};
        // Compare kernel mount IDs to exclude mounts covered by an overlay.
        const long rc = ::syscall(SYS_statx, AT_FDCWD, m.path.c_str(),
                                  AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, STATX_MNT_ID, &visible);
        if (rc != 0 && (errno == ENOENT || errno == ENOTDIR)) {
            m.visible = false;
            continue;
        }
        require(rc == 0 && (visible.stx_mask & STATX_MNT_ID), Code::mount_invalid);
        m.visible = visible.stx_mnt_id == m.id;
    }
    return mounts;
}
const Mount &visible_mount(const std::vector<Mount> &mounts, std::string_view path) {
    const Mount *found = nullptr;
    for (const auto &m : mounts)
        if (m.visible && m.path == path) {
            require(found == nullptr, Code::mount_invalid);
            found = &m;
        }
    require(found != nullptr, Code::mount_invalid);
    return *found;
}
void directory(std::string_view path) {
    auto fd = open_path(path, O_RDONLY | O_DIRECTORY);
    struct stat s{};
    require(::fstat(fd.get(), &s) == 0 && S_ISDIR(s.st_mode) && s.st_uid == 0 &&
                !(s.st_mode & 0022),
            Code::mount_invalid);
}
void make_directory(const char *path, mode_t mode) {
    require(::mkdir(path, mode) == 0 || errno == EEXIST, Code::io_failed);
    directory(path);
    require(::chmod(path, mode) == 0, Code::io_failed);
}
void close_unrelated() {
    DIR *directory = ::opendir("/proc/self/fd");
    require(directory != nullptr, Code::owner_invalid);
    const int keep = ::dirfd(directory);
    while (auto *entry = ::readdir(directory)) {
        unsigned fd = 0;
        if (!number(entry->d_name, fd) || fd <= 4 || fd == static_cast<unsigned>(keep))
            continue;
        ::close(static_cast<int>(fd));
    }
    ::closedir(directory);
    Fd null(::open("/dev/null", O_RDWR | O_CLOEXEC | O_NOFOLLOW));
    require(null.get() >= 0, Code::owner_invalid);
    struct stat s{};
    require(::fstat(null.get(), &s) == 0 && S_ISCHR(s.st_mode) && s.st_rdev == makedev(1, 3),
            Code::owner_invalid);
    for (int fd = 0; fd < 3; ++fd)
        require(::dup2(null.get(), fd) == fd, Code::owner_invalid);
}
void validate_owner() {
    require(::getuid() == 0 && ::geteuid() == 0 && ::getppid() > 1, Code::owner_invalid);
    for (const int fd : {kOwnerFd, kStatusFd}) {
        struct stat s{};
        const int flags = ::fcntl(fd, F_GETFL);
        require(::fstat(fd, &s) == 0 && S_ISFIFO(s.st_mode) && flags >= 0, Code::owner_invalid);
        require((flags & O_ACCMODE) == (fd == kOwnerFd ? O_RDONLY : O_WRONLY), Code::owner_invalid);
        cloexec(fd);
    }
    nonblock(kStatusFd);
    require(owner_alive(), Code::owner_gone);
    const pid_t parent = ::getppid();
    require(::prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && ::getppid() == parent, Code::owner_gone);
    close_unrelated();
}
std::vector<FilePin> pins() {
    std::vector<FilePin> out(std::begin(kInstalledFiles), std::end(kInstalledFiles));
    out.insert(out.end(), kImageFiles.begin(), kImageFiles.end());
    return out;
}
void profile_gate() {
    require(kKnownDlopenAndAssetInventoryReviewed && kImageRuntimeClosureReviewed,
            Code::profile_incomplete);
    const auto files = pins();
    require(files.size() <= 64, Code::profile_incomplete);
    std::set<std::string_view> paths, elfs;
    bool linker = false, config = false;
    for (const auto &f : files) {
        require(f.path.size() > 1 && f.path.front() == '/' && paths.insert(f.path).second &&
                    f.bytes > 0 && f.bytes <= 64 * 1024 * 1024 && f.sha256.size() == 64,
                Code::profile_incomplete);
        for (const auto c : f.sha256)
            require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), Code::profile_incomplete);
        if (f.kind == FileKind::library || f.kind == FileKind::executable)
            elfs.insert(f.path);
        if (f.kind == FileKind::linker) {
            require(f.path == kLinker, Code::profile_incomplete);
            linker = true;
        }
        if (f.kind == FileKind::linker_config)
            config = true;
    }
    require(linker && config &&
                elfs ==
                    std::set<std::string_view>(kExpectedElfPaths.begin(), kExpectedElfPaths.end()),
            Code::profile_incomplete);
    require(kInputStartupProfileReviewed, Code::image_provider_unapproved);
}
Fd lock_session() {
    auto tmp = open_path("/tmp", O_RDONLY | O_DIRECTORY);
    struct statfs fs{};
    struct stat st{};
    require(::fstatfs(tmp.get(), &fs) == 0 &&
                (static_cast<unsigned long>(fs.f_type) == 0x01021994UL ||
                 static_cast<unsigned long>(fs.f_type) == 0x858458f6UL),
            Code::persistent_alias);
    require(::fstat(tmp.get(), &st) == 0 && st.st_uid == 0, Code::owner_invalid);
    make_directory("/tmp/uke-touch", 0700);
    Fd lock = open_path("/tmp/uke-touch/session.lock", O_RDWR | O_CREAT, 0600);
    require(::fstat(lock.get(), &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
                st.st_nlink == 1 && (st.st_mode & 0777) == 0600,
            Code::owner_invalid);
    require(::flock(lock.get(), LOCK_EX | LOCK_NB) == 0, Code::duplicate_session);
    return lock;
}
void private_namespace() {
    struct stat before{}, after{}, init{};
    require(::stat("/proc/self/ns/mnt", &before) == 0, Code::namespace_failed);
    require(::unshare(CLONE_NEWNS) == 0, Code::namespace_failed);
    require(::stat("/proc/self/ns/mnt", &after) == 0 && ::stat("/proc/1/ns/mnt", &init) == 0 &&
                (before.st_ino != after.st_ino || before.st_dev != after.st_dev) &&
                (init.st_ino != after.st_ino || init.st_dev != after.st_dev),
            Code::namespace_failed);
    require(::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0,
            Code::namespace_failed);
    for (const auto &m : current_mounts())
        require(!m.propagation, Code::namespace_shared);
}
std::string slot_suffix() {
#ifdef __ANDROID__
    std::array<char, PROP_VALUE_MAX> value{};
    const int len = __system_property_get("ro.boot.slot_suffix", value.data());
    require(len > 0 && slot(std::string_view(value.data(), static_cast<std::size_t>(len))),
            Code::mapping_identity);
    return value.data();
#else
    // Host compilation does not invent a tablet slot/property backend.
    throw Code::mapping_identity;
#endif
}
struct Mapping {
    unsigned major = 0, minor = 0;
    Fd fd;
    Filesystem fs = Filesystem::unknown;
    std::string source;
};
void linear_table(unsigned major, unsigned minor, const std::string &expected, unsigned depth = 0) {
    require(depth < 4, Code::snapshot_unresolved);
    const std::string prefix =
        "/sys/dev/block/" + std::to_string(major) + ":" + std::to_string(minor);
    struct stat sys{};
    if (::stat((prefix + "/dm").c_str(), &sys) != 0) {
        require(expected.empty() && errno == ENOENT && ::stat(prefix.c_str(), &sys) == 0,
                Code::mapping_identity);
        return;
    }
    std::string name = read_kernel(prefix + "/dm/name", 256);
    if (!name.empty() && name.back() == '\n')
        name.pop_back();
    require(!name.empty() && (expected.empty() || name == expected), Code::mapping_identity);
    Fd control(::open("/dev/device-mapper", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    require(control.get() >= 0, Code::snapshot_unresolved);
    struct stat control_stat{};
    unsigned control_major = 0, control_minor = 0;
    std::string control_number = read_kernel("/sys/class/misc/device-mapper/dev", 64);
    if (!control_number.empty() && control_number.back() == '\n')
        control_number.pop_back();
    require(::fstat(control.get(), &control_stat) == 0 && S_ISCHR(control_stat.st_mode) &&
                control_stat.st_uid == 0 && device(control_number, control_major, control_minor) &&
                control_stat.st_rdev == makedev(control_major, control_minor),
            Code::mapping_identity);
    alignas(dm_ioctl) std::array<unsigned char, 65536> bytes{};
    auto *io = reinterpret_cast<dm_ioctl *>(bytes.data());
    io->version[0] = DM_VERSION_MAJOR;
    // Like pinned libdm, request the stable 4.0 interface. The installed
    // kernel can predate these build headers; it returns its actual version.
    io->version[1] = 0;
    io->version[2] = 0;
    io->data_size = bytes.size();
    io->data_start = sizeof(dm_ioctl);
    io->flags = DM_STATUS_TABLE_FLAG | DM_NOFLUSH_FLAG;
    require(name.size() < sizeof(io->name), Code::mapping_identity);
    std::memcpy(io->name, name.c_str(), name.size() + 1);
    require(::ioctl(control.get(), DM_TABLE_STATUS, io) == 0, Code::snapshot_unresolved);
    const auto backing = linear_reply(bytes, major, minor, name);
    require(backing.has_value(), Code::snapshot_unresolved);
    for (const auto &block : *backing)
        linear_table(block.major, block.minor, "", depth + 1);
}
Mapping mapping(const std::string &name) {
    // Mapping creation remains owned by normal recovery partition setup.
    const std::string alias = "/dev/block/mapper/" + name;
    std::array<char, PATH_MAX> resolved{};
    require(::realpath(alias.c_str(), resolved.data()) != nullptr, Code::mapping_missing);
    const std::string source = resolved.data();
    require(source.starts_with("/dev/block/") && source.find("..") == std::string::npos,
            Code::mapping_identity);
    Mapping m;
    m.source = source;
    m.fd = open_path(source, O_RDONLY | O_NONBLOCK);
    struct stat s{};
    require(::fstat(m.fd.get(), &s) == 0 && S_ISBLK(s.st_mode), Code::mapping_identity);
    m.major = major(s.st_rdev);
    m.minor = minor(s.st_rdev);
    linear_table(m.major, m.minor, name);
    std::vector<unsigned char> bytes(2048);
    require(::pread(m.fd.get(), bytes.data(), bytes.size(), 0) ==
                static_cast<ssize_t>(bytes.size()),
            Code::filesystem_unknown);
    m.fs = signature(bytes);
    require(m.fs != Filesystem::unknown, Code::filesystem_unknown);
    return m;
}
void provider_mount(const char *path, const Mapping &m) {
    directory(path);
    auto mounts = current_mounts();
    const Mount *old = nullptr;
    for (const auto &candidate : mounts)
        if (candidate.visible && candidate.path == path) {
            require(old == nullptr, Code::mount_invalid);
            old = &candidate;
        }
    if (old) {
        require(ro_provider_mount(*old, m.major, m.minor, m.fs), Code::mount_invalid);
        return;
    }
    const char *type = m.fs == Filesystem::ext4 ? "ext4" : "erofs";
    const char *options = m.fs == Filesystem::ext4 ? "noload" : nullptr;
    // Source FD is verified and held open; this avoids a symlink replacement.
    const std::string source = "/proc/self/fd/" + std::to_string(m.fd.get());
    require(::mount(source.c_str(), path, type, MS_RDONLY | MS_NOSUID | MS_NODEV, options) == 0,
            Code::mount_invalid);
    mounts = current_mounts();
    require(ro_provider_mount(visible_mount(mounts, path), m.major, m.minor, m.fs),
            Code::mount_invalid);
}
void pmsg() {
    struct stat before{}, null{};
    require(::lstat("/dev/pmsg0", &before) == 0 && S_ISCHR(before.st_mode) &&
                ::lstat("/dev/null", &null) == 0 && S_ISCHR(null.st_mode) &&
                null.st_rdev == makedev(1, 3),
            Code::pmsg_failed);
    require(::mount("/dev/null", "/dev/pmsg0", nullptr, MS_BIND, nullptr) == 0, Code::pmsg_failed);
    struct stat after{};
    require(::lstat("/dev/pmsg0", &after) == 0 && S_ISCHR(after.st_mode) &&
                after.st_rdev == null.st_rdev,
            Code::pmsg_failed);
}
void overlay(const char *target) {
    directory(target);
    require(::mount("uke-touch-ram", target, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
                    "mode=0755,size=16m") == 0,
            Code::overlay_failed);
}
bool overlays() {
    for (const char *target : {"/data", "/metadata", "/mnt", "/dev/socket", "/persist",
                               "/sys/fs/pstore", "/dev/block"})
        overlay(target);
    // Mapping discovery has finished. Hide the raw DM control from OEM code.
    struct stat control{}, null{};
    require(::lstat("/dev/device-mapper", &control) == 0 && S_ISCHR(control.st_mode) &&
                ::lstat("/dev/null", &null) == 0 && null.st_rdev == makedev(1, 3),
            Code::persistent_alias);
    require(::mount("/dev/null", "/dev/device-mapper", nullptr, MS_BIND, nullptr) == 0,
            Code::persistent_alias);
    require(::lstat("/dev/device-mapper", &control) == 0 && control.st_rdev == null.st_rdev,
            Code::persistent_alias);
    struct stat cache{};
    require(::lstat("/cache", &cache) == 0, Code::overlay_failed);
    bool real_cache = !S_ISLNK(cache.st_mode);
    if (!real_cache) {
        std::array<char, 128> link{};
        const auto n = ::readlink("/cache", link.data(), link.size());
        require(n > 0 && n < static_cast<ssize_t>(link.size()) &&
                    cache_link(std::string_view(link.data(), static_cast<std::size_t>(n))),
                Code::persistent_alias);
        make_directory("/data/cache", 0755);
    } else
        overlay("/cache");
    for (const char *path :
         {"/data/vendor", "/data/vendor/bsplog", "/mnt/vendor", "/mnt/vendor/persist"})
        make_directory(path, 0755);
    for (const char *path :
         {"/data/vendor/touch", "/data/vendor/touch_state", "/data/vendor/mqsas_common",
          "/data/vendor/bsplog/touch", "/mnt/vendor/persist/touch"})
        make_directory(path, 0700);
    require(mount_policy(current_mounts(), true, real_cache) == Code::ok, Code::persistent_alias);
    return real_cache;
}
void elf_identity(int fd, const FilePin &pin) {
    Elf64_Ehdr eh{};
    require(
        ::pread(fd, &eh, sizeof(eh), 0) == static_cast<ssize_t>(sizeof(eh)) &&
            std::memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 && eh.e_ident[EI_CLASS] == ELFCLASS64 &&
            eh.e_ident[EI_DATA] == ELFDATA2LSB && eh.e_ident[EI_VERSION] == EV_CURRENT &&
            eh.e_machine == EM_AARCH64 && eh.e_version == EV_CURRENT && eh.e_type == ET_DYN &&
            eh.e_ehsize == sizeof(eh) && eh.e_phentsize == sizeof(Elf64_Phdr) && eh.e_phnum > 0 &&
            eh.e_phnum <= 128 && eh.e_phoff <= pin.bytes &&
            static_cast<std::uint64_t>(eh.e_phnum) * sizeof(Elf64_Phdr) <= pin.bytes - eh.e_phoff,
        Code::elf_invalid);
    bool interpreter = false;
    for (unsigned i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr ph{};
        require(::pread(fd, &ph, sizeof(ph), static_cast<off_t>(eh.e_phoff + i * sizeof(ph))) ==
                        static_cast<ssize_t>(sizeof(ph)) &&
                    ph.p_offset <= pin.bytes && ph.p_filesz <= pin.bytes - ph.p_offset,
                Code::elf_invalid);
        if (ph.p_type != PT_INTERP)
            continue;
        require(!interpreter && ph.p_filesz > 1 && ph.p_filesz <= 128, Code::elf_invalid);
        std::array<char, 128> name{};
        require(::pread(fd, name.data(), ph.p_filesz, static_cast<off_t>(ph.p_offset)) ==
                        static_cast<ssize_t>(ph.p_filesz) &&
                    name[ph.p_filesz - 1] == 0 &&
                    std::string_view(name.data(), ph.p_filesz - 1) == kLinker,
                Code::elf_invalid);
        interpreter = true;
    }
    require(pin.kind != FileKind::executable || interpreter, Code::elf_invalid);
    require(pin.kind != FileKind::linker || !interpreter, Code::elf_invalid);
}
Fd verify_pin(const FilePin &pin, std::uint64_t deadline) {
    preparing_check(deadline);
    Fd file = open_path(pin.path, O_RDONLY);
    struct stat st{};
    require(::fstat(file.get(), &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
                !(st.st_mode & 0022) && st.st_nlink == 1 &&
                static_cast<std::uint64_t>(st.st_size) == pin.bytes,
            Code::hash_mismatch);
    if (pin.kind == FileKind::executable || pin.kind == FileKind::library ||
        pin.kind == FileKind::linker)
        elf_identity(file.get(), pin);
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(ctx != nullptr, Code::io_failed);
    std::array<unsigned char, 65536> buffer{};
    std::uint64_t count = 0;
    bool ok = EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) == 1;
    while (ok) {
        preparing_check(deadline);
        const auto n = ::read(file.get(), buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0)
            break;
        count += static_cast<std::uint64_t>(n);
        if (count > pin.bytes) {
            ok = false;
            break;
        }
        ok = EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<std::size_t>(n)) == 1;
    }
    std::array<unsigned char, 32> digest{};
    unsigned len = 0;
    ok = ok && count == pin.bytes && EVP_DigestFinal_ex(ctx.get(), digest.data(), &len) == 1 &&
         len == digest.size();
    require(ok, Code::hash_mismatch);
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : digest) {
        text.push_back(hex[byte >> 4]);
        text.push_back(hex[byte & 15]);
    }
    require(text == pin.sha256, Code::hash_mismatch);
    return file;
}

void require_init_namespace() {
    struct stat own{}, init{};
    require(::stat("/proc/self/ns/mnt", &own) == 0 && ::stat("/proc/1/ns/mnt", &init) == 0 &&
                own.st_ino == init.st_ino && own.st_dev == init.st_dev,
            Code::namespace_failed);
}
void prepare_input_modules(std::uint64_t deadline, const std::string &suffix) {
    utsname kernel{};
    require(::uname(&kernel) == 0 && std::string_view(kernel.release) == kKernelRelease,
            Code::image_provider_unapproved);
    require_init_namespace();
    // Firmware lookup runs in init's namespace, not in the HAL sandbox.
    auto odm = mapping("odm" + suffix);
    auto dlkm = mapping("vendor_dlkm" + suffix);
    provider_mount("/odm", odm);
    provider_mount("/vendor_dlkm", dlkm);
    for (const auto &pin : kInstalledFiles)
        if (pin.path.starts_with("/odm/firmware/novatek_nt36532_"))
            verify_pin(pin, deadline);
    std::array<Fd, 2> modules;
    for (std::size_t i = 0; i < modules.size(); ++i)
        modules[i] = verify_pin(kInputModules[i], deadline);
    auto inherited = read_kernel("/sys/module/firmware_class/parameters/path", 512);
    if (!inherited.empty() && inherited.back() == '\n')
        inherited.pop_back();
    const auto replacement = firmware_lookup(inherited);
    require(replacement.has_value(), Code::image_provider_unapproved);
    if (*replacement != inherited) {
        Fd path = open_path("/sys/module/firmware_class/parameters/path", O_WRONLY);
        require(::write(path.get(), replacement->data(), replacement->size()) ==
                    static_cast<ssize_t>(replacement->size()), Code::io_failed);
        auto observed = read_kernel("/sys/module/firmware_class/parameters/path", 512);
        if (!observed.empty() && observed.back() == '\n')
            observed.pop_back();
        require(observed == *replacement, Code::io_failed);
    }
    constexpr std::array<const char *, 2> names = {"xiaomi_touch", "nt36532_touch"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        preparing_check(deadline);
        struct stat loaded{};
        const std::string path = "/sys/module/" + std::string(names[i]);
        if (::stat(path.c_str(), &loaded) == 0) {
            require(S_ISDIR(loaded.st_mode), Code::image_provider_unapproved);
            continue;
        }
        require(errno == ENOENT, Code::io_failed);
        // No dependency autoload, storage module or ABI override.
        require(::syscall(SYS_finit_module, modules[i].get(), "", 0) == 0, Code::io_failed);
        require(::stat(path.c_str(), &loaded) == 0 && S_ISDIR(loaded.st_mode), Code::io_failed);
    }
}
struct Child {
    Lifecycle life;
    Fd output, exec_error;
    bool group = false;
    Child() { require(life.prepared(), Code::exec_failed); }
    ~Child() { cleanup(); }
    Child(const Child &) = delete;
    Child &operator=(const Child &) = delete;
    bool exited() const noexcept {
        if (life.owned_pid <= 1)
            return true;
        siginfo_t info{};
        const int rc =
            ::waitid(P_PID, static_cast<id_t>(life.owned_pid), &info, WEXITED | WNOHANG | WNOWAIT);
        return rc == 0 && info.si_pid == life.owned_pid;
    }
    void signal(int sig) noexcept {
        const int pid = life.owned_pid;
        if (!life.may_signal(pid))
            return;
        if (group)
            ::kill(-pid, sig);
        ::kill(pid, sig);
    }
    bool cleanup() noexcept {
        const int pid = life.owned_pid;
        if (!life.may_signal(pid))
            return true;
        signal(SIGTERM);
        timespec start{};
        if (::clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            signal(SIGKILL);
            return reap(pid);
        }
        while (!exited()) {
            timespec now{};
            if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0)
                break;
            const auto elapsed =
                (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
            if (elapsed >= static_cast<long>(kGraceMs))
                break;
            // Keep draining during TERM grace, never block on diagnostics.
            std::array<char, 4096> bytes{};
            if (output.get() >= 0)
                for (unsigned n = 0; n < 16; ++n)
                    if (::read(output.get(), bytes.data(), bytes.size()) <= 0)
                        break;
            pollfd fd{output.get(), POLLIN, 0};
            ::poll(&fd, output.get() >= 0 ? 1 : 0, 20);
        }
        // Signal before reap: the child PID remains reserved, including zombies.
        signal(SIGKILL);
        return reap(pid);
    }
    bool reap(int pid) noexcept {
        int status = 0;
        pid_t rc;
        do {
            rc = ::waitpid(pid, &status, 0);
        } while (rc < 0 && errno == EINTR);
        if (rc == pid) {
            life.reaped(pid);
            return true;
        }
        // Never signal an unowned or already reaped PID after this point.
        life.owned_pid = -1;
        return false;
    }
    void start(std::string_view executable, std::string_view list_root = {}) {
        int out[2]{-1, -1}, err[2]{-1, -1};
        require(::pipe2(out, O_CLOEXEC) == 0, Code::exec_failed);
        Fd read_out(out[0]), write_out(out[1]);
        require(::pipe2(err, O_CLOEXEC) == 0, Code::exec_failed);
        Fd read_err(err[0]), write_err(err[1]);
        const pid_t parent = ::getpid();
        const pid_t pid = ::fork();
        require(pid >= 0, Code::exec_failed);
        if (pid == 0) {
            ::alarm(0);
            ::signal(SIGTERM, SIG_DFL);
            ::signal(SIGINT, SIG_DFL);
            ::signal(SIGHUP, SIG_DFL);
            ::signal(SIGALRM, SIG_DFL);
            ::signal(SIGPIPE, SIG_DFL);
            auto child_fail = [&]() {
                const int error = errno ? errno : EIO;
                ::write(write_err.get(), &error, sizeof(error));
                ::_exit(127);
            };
            if (::setpgid(0, 0) != 0 || ::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 ||
                ::getppid() != parent || ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
                child_fail();
            if (::dup2(write_out.get(), STDOUT_FILENO) < 0 ||
                ::dup2(write_out.get(), STDERR_FILENO) < 0)
                child_fail();
            DIR *entries = ::opendir("/proc/self/fd");
            if (!entries)
                child_fail();
            const int dfd = ::dirfd(entries);
            while (auto *entry = ::readdir(entries)) {
                unsigned fd = 0;
                if (number(entry->d_name, fd) && fd > 2 && fd != static_cast<unsigned>(dfd) &&
                    fd != static_cast<unsigned>(write_err.get()))
                    ::close(static_cast<int>(fd));
            }
            ::closedir(entries);
            std::array<char *, 4> env{};
            for (std::size_t i = 0; i < env.size(); ++i)
                env[i] = const_cast<char *>(kEnvironment[i]);
            char *service_args[] = {const_cast<char *>(executable.data()), nullptr};
            char *linker_args[] = {const_cast<char *>(executable.data()),
                                   const_cast<char *>("--list"),
                                   const_cast<char *>(list_root.data()), nullptr};
            ::execve(executable.data(), list_root.empty() ? service_args : linker_args, env.data());
            child_fail();
        }
        require(life.own(pid), Code::exec_failed);
        const int pg = ::setpgid(pid, pid);
        group = pg == 0 || (errno == EACCES && ::getpgid(pid) == pid);
        require(group, Code::exec_failed);
        output = std::move(read_out);
        exec_error = std::move(read_err);
        nonblock(output.get());
        nonblock(exec_error.get());
    }
    bool confirm_exec(std::uint64_t deadline) {
        while (true) {
            preparing_check(deadline);
            int error = 0;
            const auto n = ::read(exec_error.get(), &error, sizeof(error));
            if (n == 0) {
                exec_error = Fd{};
                return life.exec_ok();
            }
            if (n > 0)
                throw Code::exec_failed;
            require(errno == EAGAIN || errno == EINTR, Code::exec_failed);
            pollfd fds[] = {{kOwnerFd, POLLIN | POLLHUP, 0},
                            {exec_error.get(), POLLIN | POLLHUP, 0}};
            require(::poll(fds, 2, 20) >= 0 || errno == EINTR, Code::io_failed);
        }
    }
};
std::set<std::string> providers(const std::string &output) {
    std::set<std::string> paths;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty())
            continue;
        // Android --list uses "SONAME => /path (0xaddress)"; accept only that
        // path-bearing grammar. An unexpected diagnostic is a refusal.
        std::istringstream words(line);
        std::string soname, arrow, path, address, extra;
        require(static_cast<bool>(words >> soname >> arrow >> path >> address) &&
                    !(words >> extra) && arrow == "=>" &&
                    (path.starts_with('/') || (soname == "linux-vdso.so.1" && path == "[vdso]")) &&
                    path.size() < 512 && address.size() >= 5 && address.starts_with("(0x") &&
                    address.back() == ')',
                Code::linker_failed);
        for (std::size_t i = 3; i + 1 < address.size(); ++i)
            require((address[i] >= '0' && address[i] <= '9') ||
                        (address[i] >= 'a' && address[i] <= 'f') ||
                        (address[i] >= 'A' && address[i] <= 'F'),
                    Code::linker_failed);
        if (path == "[vdso]")
            continue;
        require(path.find("/../") == std::string::npos && path.find("/./") == std::string::npos &&
                    path.find("//") == std::string::npos && paths.insert(path).second,
                Code::provider_changed);
    }
    require(!paths.empty() && paths.size() <= 32, Code::linker_failed);
    return paths;
}
std::set<std::string> linker_list(std::string_view root, std::uint64_t deadline) {
    Child child;
    child.start(kLinker, root);
    require(child.confirm_exec(deadline), Code::linker_failed);
    std::string output;
    std::array<char, 4096> bytes{};
    bool eof = false;
    while (!eof || !child.exited()) {
        preparing_check(deadline);
        for (unsigned calls = 0; calls < 16 && !eof; ++calls) {
            const auto n = ::read(child.output.get(), bytes.data(), bytes.size());
            if (n == 0) {
                eof = true;
                break;
            }
            if (n < 0) {
                require(errno == EAGAIN || errno == EINTR, Code::linker_failed);
                break;
            }
            require(output.size() + static_cast<std::size_t>(n) <= kListCap, Code::linker_failed);
            output.append(bytes.data(), static_cast<std::size_t>(n));
        }
        pollfd fds[] = {{kOwnerFd, POLLIN | POLLHUP, 0}, {child.output.get(), POLLIN | POLLHUP, 0}};
        require(::poll(fds, 2, 20) >= 0 || errno == EINTR, Code::io_failed);
    }
    int exit = 0;
    const int pid = child.life.owned_pid;
    require(::waitpid(pid, &exit, 0) == pid, Code::linker_failed);
    child.life.reaped(pid);
    require(WIFEXITED(exit) && WEXITSTATUS(exit) == 0, Code::linker_failed);
    return providers(output);
}
void verify_linker(std::uint64_t deadline) {
    std::set<std::string> selected;
    for (const auto root : kRoots) {
        auto listed = linker_list(root, deadline);
        selected.insert(listed.begin(), listed.end());
    }
    // Linker's own path can occur in --list, but it has its own compiled pin.
    selected.erase(std::string(kLinker));
    for (const auto root : kRoots)
        selected.insert(std::string(root));
    const std::set<std::string> expected(kExpectedElfPaths.begin(), kExpectedElfPaths.end());
    require(selected == expected, Code::provider_changed);
    // All roots/providers/linker/config/assets have already been hashed; repeat
    // listed provider hashes immediately before launch to detect substitution.
    for (const auto &pin : pins())
        if (selected.contains(std::string(pin.path)))
            verify_pin(pin, deadline);
}
void drain(Child &child, Fd &log, LogBudget &budget) {
    std::array<char, 4096> bytes{};
    for (unsigned calls = 0; calls < 32; ++calls) {
        const auto n = ::read(child.output.get(), bytes.data(), bytes.size());
        if (n <= 0) {
            require(n == 0 || errno == EAGAIN || errno == EINTR, Code::io_failed);
            break;
        }
        const auto keep = budget.consume(static_cast<std::size_t>(n));
        if (keep > 0) {
            const auto saved = log.get() >= 0 ? ::write(log.get(), bytes.data(), keep) : -1;
            const auto stored = saved > 0 ? static_cast<std::size_t>(saved) : 0;
            budget.retained -= keep - stored;
            if (saved != static_cast<ssize_t>(keep))
                log = Fd{};
        }
    }
}
int run() {
    validate_owner();
    status(State::preparing, Code::ok);
    profile_gate();
    Fd lock = lock_session();
    require_no_external_service(kService);
    const auto deadline = now_ms() + kStartupMs;
    ::alarm(kStartupMs / 1000);
    const std::string suffix = slot_suffix();
    require_init_namespace();
    const auto initial = mount_policy(current_mounts(), false, false, MountPhase::init_preparation);
    require(initial == Code::ok, initial);
    prepare_input_modules(deadline, suffix);
    private_namespace();
    require(mount_policy(current_mounts(), false, false) == Code::ok, Code::mount_invalid);
    const auto mapping_deadline = std::min(deadline, now_ms() + kMappingMs);
    std::optional<Mapping> odm, vendor;
    while (!odm || !vendor) {
        preparing_check(deadline);
        try {
            if (!odm)
                odm = mapping("odm" + suffix);
            if (!vendor)
                vendor = mapping("vendor" + suffix);
        } catch (Code code) {
            if (code != Code::mapping_missing)
                throw;
            require(now_ms() < mapping_deadline, Code::mapping_missing);
            pollfd owner{kOwnerFd, POLLIN | POLLHUP, 0};
            ::poll(&owner, 1, 50);
        }
    }
    provider_mount("/odm", *odm);
    provider_mount("/vendor", *vendor);
    pmsg();
    overlays();
    struct stat node{};
    require(::lstat("/dev/xiaomi-touch", &node) == 0 && S_ISCHR(node.st_mode) &&
                ::access("/sys/class/touch/touch_dev/abnormal_event", R_OK) == 0,
            Code::mapping_missing);
    for (const auto &pin : pins())
        verify_pin(pin, deadline);
    verify_linker(deadline);
    preparing_check(deadline);
    // Diagnostics are optional, containment is mandatory. A failed RAM log
    // still drains the OEM pipe and never falls back to any persistent sink.
    Fd log;
    try {
        log = open_path("/tmp/uke-touch/hal.private.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        struct stat st{};
        require(::fstat(log.get(), &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
                    st.st_nlink == 1 && (st.st_mode & 0777) == 0600,
                Code::io_failed);
    } catch (Code) {
        log = Fd{};
    }
    Child child;
    require_no_external_service(kService);
    child.start(kService);
    require(child.confirm_exec(deadline), Code::exec_failed);
    ::alarm(0);
    require(!child.exited(), Code::child_exited);
    LogBudget budget;
    status(State::running, Code::ok, budget);
    auto last = now_ms();
    Code reason = Code::ok;
    State terminal = State::stopped;
    while (true) {
        drain(child, log, budget);
        if (!owner_alive()) {
            reason = Code::owner_gone;
            break;
        }
        if (interrupted) {
            reason = Code::signal_stop;
            break;
        }
        if (child.exited()) {
            reason = Code::child_exited;
            terminal = State::failed;
            break;
        }
        const auto now = now_ms();
        if (now - last >= 1000) {
            status(State::running, Code::ok, budget);
            last = now;
        }
        pollfd fds[] = {{kOwnerFd, POLLIN | POLLHUP, 0}, {child.output.get(), POLLIN | POLLHUP, 0}};
        require(::poll(fds, 2, 100) >= 0 || errno == EINTR, Code::io_failed);
    }
    const bool cleaned = child.cleanup();
    if (!cleaned) {
        reason = Code::cleanup_failed;
        terminal = State::failed;
    }
    try {
        status(terminal, reason, budget);
    } catch (Code) {
    }
    return cleaned ? 0 : 71;
}
} // namespace
} // namespace uke::touch
int main(int argc, char **) {
    using namespace uke::touch;
    if (argc != 1)
        return 64;
    ::signal(SIGPIPE, SIG_IGN);
    struct sigaction action{};
    action.sa_handler = signal_handler;
    ::sigemptyset(&action.sa_mask);
    for (const int signal : {SIGTERM, SIGINT, SIGHUP, SIGALRM})
        if (::sigaction(signal, &action, nullptr) != 0)
            return 70;
    try {
        return run();
    } catch (Code code) {
        try {
            status(published_state == State::running ? State::failed : State::refused, code);
        } catch (Code) {
        }
        return 70;
    } catch (...) {
        try {
            status(State::failed, Code::io_failed);
        } catch (Code) {
        }
        return 71;
    }
}
