// SPDX-License-Identifier: Apache-2.0
#include "uke.h"
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <openssl/evp.h>
#include <poll.h>
#include <regex>
#include <set>
#include <sstream>
#include <sys/random.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace ure {
void require(bool condition, const std::string& code, const std::string& message) {
    if (!condition) throw Error(code, message);
}
Fd::~Fd() { if (fd_ >= 0) ::close(fd_); }
Fd::Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) { if (fd_ >= 0) ::close(fd_); fd_ = other.fd_; other.fd_ = -1; }
    return *this;
}
std::vector<std::string> components(std::string_view path) {
    require(!path.empty() && path.size() <= 4096 && path.front() != '/', "invalid-path", "Use a bounded relative path");
    require(path.find('\0') == path.npos && path.find('\n') == path.npos, "invalid-path", "Invalid path characters");
    std::vector<std::string> result;
    std::istringstream stream{std::string(path)};
    std::string item;
    while (std::getline(stream, item, '/')) {
        require(!item.empty() && item != "..", "invalid-path", "Empty or parent path component rejected");
        if (item != ".") result.push_back(item);
    }
    require(result.size() <= 64, "invalid-path", "Too many path components");
    return result;
}
Root::Root(const fs::path& path) : fd_(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) {
    require(fd_.get() >= 0, "invalid-root", "Root must be an accessible real directory");
}
Root::Root(Fd directory) : fd_(std::move(directory)) {
    struct stat st{};
    require(fd() >= 0 && ::fstat(fd(), &st) == 0 && S_ISDIR(st.st_mode), "invalid-root", "Expected a retained directory descriptor");
}
Root private_directory(const fs::path& path, bool create) {
    Root parent(path.parent_path().empty() ? fs::path(".") : path.parent_path());
    return private_subdirectory(parent,path.filename().string(),create);
}
Root private_subdirectory(const Root& parent, const std::string& name, bool create) {
    require(!name.empty() && name != "." && name != ".." && components(name).size() == 1,
        "invalid-path", "A named private directory is required");
    if (create) {
        const int result = ::mkdirat(parent.fd(), name.c_str(), 0700);
        require(result == 0, errno == EEXIST ? "existing-journal" : "io-error", "Cannot exclusively create private directory");
        require(::fsync(parent.fd()) == 0, "io-error", "Cannot sync private directory creation");
    }
    auto directory = parent.open(name, O_RDONLY | O_DIRECTORY);
    struct stat st{};
    require(::fstat(directory.get(), &st) == 0 && st.st_uid == ::geteuid() && (st.st_mode & 07777) == 0700,
        "unsafe-journal", "Private directory must be owned by this user and have mode 0700");
    return Root(std::move(directory));
}
Fd Root::open(std::string_view relative, int flags, mode_t mode) const {
    auto parts = components(relative);
    Fd current(::fcntl(fd(), F_DUPFD_CLOEXEC, 0));
    require(current.get() >= 0, "io-error", "Cannot retain root descriptor");
    if (parts.empty()) return current;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        int opts = (i + 1 == parts.size()) ? flags : O_RDONLY | O_DIRECTORY;
        Fd next(::openat(current.get(), parts[i].c_str(), opts | O_NOFOLLOW | O_CLOEXEC, mode));
        require(next.get() >= 0, "path-unavailable", "Path is absent, inaccessible or a symlink");
        current = std::move(next);
    }
    return current;
}
Fd Root::open_resolved(std::string_view relative, int flags) const {
    components(relative);
    require((flags & O_ACCMODE)==O_RDONLY && !(flags & (O_CREAT|O_TRUNC|O_APPEND)) && (flags & O_TMPFILE)!=O_TMPFILE,
        "read-only-resolver", "Installed-system alias resolution accepts only read access");
    struct open_how how{};
    how.flags=static_cast<std::uint64_t>(flags|O_CLOEXEC);
    how.resolve=RESOLVE_IN_ROOT|RESOLVE_NO_MAGICLINKS;
    const std::string path(relative.empty() ? "." : relative);
    Fd result(static_cast<int>(::syscall(SYS_openat2,fd(),path.c_str(),&how,sizeof(how))));
    require(result.get()>=0, errno==ENOSYS ? "resolver-unavailable" : "path-unavailable",
        "Cannot resolve the installed-system path inside its selected root");
    return result;
}
std::string Root::read_resolved(std::string_view relative, std::size_t limit) const {
    auto file=open_resolved(relative,O_RDONLY|O_NONBLOCK); struct stat st{};
    require(::fstat(file.get(),&st)==0 && S_ISREG(st.st_mode) && st.st_size>=0 && static_cast<std::uint64_t>(st.st_size)<=limit,
        "invalid-file", "Expected a bounded regular installed-system file");
    return storage_read(file.get(),0,static_cast<std::size_t>(st.st_size));
}
bool Root::exists_resolved(std::string_view relative) const {
    try { auto fd=open_resolved(relative,O_RDONLY|O_NONBLOCK); return fd.get()>=0; }
    catch(const Error& e) { if(e.code=="path-unavailable")return false; throw; }
}
std::string Root::read(std::string_view relative, std::size_t limit) const {
    auto file = open(relative, O_RDONLY | O_NONBLOCK);
    struct stat st{};
    require(::fstat(file.get(), &st) == 0 && S_ISREG(st.st_mode), "invalid-file", "Expected a regular file");
    std::string output;
    std::array<char, 4096> chunk{};
    while (true) {
        const ssize_t n = ::read(file.get(), chunk.data(), chunk.size());
        if (n < 0 && errno == EINTR) continue;
        require(n >= 0, "io-error", "File read failed");
        if (n == 0) break;
        require(output.size() + static_cast<std::size_t>(n) <= limit, "size-limit", "File exceeds the read limit");
        output.append(chunk.data(), static_cast<std::size_t>(n));
    }
    return output;
}
bool Root::exists(std::string_view relative) const {
    try { auto file = open(relative, O_RDONLY | O_NONBLOCK); return file.get() >= 0; }
    catch (const Error& e) { if (e.code == "path-unavailable") return false; throw; }
}
struct stat Root::stat(std::string_view relative) const {
    auto file = open(relative, O_RDONLY | O_NONBLOCK);
    struct stat st{};
    require(::fstat(file.get(), &st) == 0, "io-error", "Cannot read file metadata");
    return st;
}
std::vector<std::string> Root::list(std::string_view relative, std::size_t limit) const {
    auto dir = open(relative, O_RDONLY | O_DIRECTORY);
    DIR* stream = ::fdopendir(::dup(dir.get()));
    require(stream != nullptr, "io-error", "Cannot list directory");
    std::unique_ptr<DIR, int(*)(DIR*)> guard(stream, ::closedir);
    std::vector<std::string> result;
    while (auto* entry = ::readdir(stream)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        require(result.size() < limit, "size-limit", "Directory listing exceeds limit");
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}
std::string Root::link(std::string_view relative) const {
    const auto parts = components(relative);
    require(!parts.empty(), "invalid-path", "A link name is required");
    const fs::path path{std::string(relative)};
    auto parent = open(path.parent_path().empty() ? "." : path.parent_path().string(), O_RDONLY | O_DIRECTORY);
    std::array<char, 4097> buffer{};
    const ssize_t n = ::readlinkat(parent.get(), parts.back().c_str(), buffer.data(), buffer.size() - 1);
    require(n >= 0 && n < 4096, "path-unavailable", "Link is unavailable or too long");
    return std::string(buffer.data(), static_cast<std::size_t>(n));
}
bool identifier(std::string_view text) {
    return !text.empty() && text.size() <= 128 && text.front() != '-' &&
        std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
}
bool uuid(std::string_view text) {
    if (text.size() != 36) return false;
    bool nonzero = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (text[i] != '-') return false; }
        else { if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false; nonzero = nonzero || text[i] != '0'; }
    }
    return nonzero;
}
bool hash_valid(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
static std::string digest_hex(const unsigned char* data, unsigned count) {
    std::ostringstream output;
    for (unsigned i = 0; i < count; ++i) output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(data[i]);
    return output.str();
}
std::string sha256(int fd) {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(ctx && EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) == 1, "hash-error", "Cannot initialize SHA-256");
    std::array<unsigned char, 65536> block{};
    off_t offset = 0;
    while (true) {
        const ssize_t n = ::pread(fd, block.data(), block.size(), offset);
        if (n < 0 && errno == EINTR) continue;
        require(n >= 0, "io-error", "Hash read failed");
        if (n == 0) break;
        require(EVP_DigestUpdate(ctx.get(), block.data(), static_cast<std::size_t>(n)) == 1, "hash-error", "SHA-256 update failed");
        require(offset <= INT64_MAX - n, "overflow", "File offset overflow");
        offset += n;
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned length = 0;
    require(EVP_DigestFinal_ex(ctx.get(), bytes.data(), &length) == 1, "hash-error", "SHA-256 failed");
    return digest_hex(bytes.data(), length);
}
std::string sha256(std::string_view bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned length = 0;
    require(EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr) == 1, "hash-error", "SHA-256 failed");
    return digest_hex(digest.data(), length);
}
std::string json(const Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, value) + "\n";
}
Value parse_json(std::string_view text) {
    require(text.size() <= 4 * 1024 * 1024, "size-limit", "JSON exceeds 4 MiB");
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"] = true; builder["failIfExtra"] = true;
    builder["allowComments"] = false; builder["allowTrailingCommas"] = false;
    builder["stackLimit"] = 32; builder["strictRoot"] = true;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Value result; std::string errors;
    require(reader->parse(text.data(), text.data() + text.size(), &result, &errors), "invalid-json", "Malformed, duplicated or excessive JSON");
    return result;
}
std::string bounded_read(const fs::path& path, std::size_t limit) {
    Root root(path.parent_path().empty() ? fs::path(".") : path.parent_path());
    return root.read(path.filename().string(), limit);
}
Value json_file(const fs::path& path) { return parse_json(bounded_read(path,4*1024*1024)); }
static void write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const ssize_t n = ::write(fd, data.data(), data.size());
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "io-error", "Write failed");
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}
void Root::atomic_save(std::string_view relative, std::string_view contents,
                       std::string_view expected_sha, bool preserve_metadata, std::size_t limit) const {
    require(limit <= 4 * 1024 * 1024 && contents.size() <= limit && hash_valid(expected_sha), "invalid-save", "Bounded content and expected SHA-256 required");
    const fs::path path{std::string(relative)};
    components(relative);
    auto parent = open(path.parent_path().empty() ? "." : path.parent_path().string(), O_RDONLY | O_DIRECTORY);
    Fd previous(::openat(parent.get(), path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    require(previous.get() >= 0, "path-unavailable", "Edit target must already exist");
    struct stat before{};
    require(::fstat(previous.get(), &before) == 0 && S_ISREG(before.st_mode) && before.st_nlink == 1, "invalid-target", "Edit target must be a regular file with one hard link");
    require(sha256(previous.get()) == expected_sha, "stale-plan", "File changed since plan creation");
    const std::string temporary = ".ure-save-" + operation_id();
    Fd output(::openat(parent.get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    require(output.get() >= 0, "io-error", "Cannot create atomic-save temporary file");
    try {
        write_all(output.get(), contents);
        if (preserve_metadata) {
            struct stat created{};
            require(::fstat(output.get(), &created) == 0, "io-error", "Cannot inspect temporary file");
            if (created.st_uid != before.st_uid || created.st_gid != before.st_gid)
                require(::fchown(output.get(), before.st_uid, before.st_gid) == 0, "metadata-error", "Cannot preserve file ownership");
            require(::fchmod(output.get(), before.st_mode & 07777) == 0, "metadata-error", "Cannot preserve file mode");
            ssize_t count = ::flistxattr(previous.get(), nullptr, 0);
            require(count >= 0 || errno == ENOTSUP, "metadata-error", "Cannot enumerate file attributes");
            if (count > 0) {
                require(count <= 65536, "size-limit", "Extended attributes exceed limit");
                std::vector<char> names(static_cast<std::size_t>(count));
                require(::flistxattr(previous.get(), names.data(), names.size()) == count, "metadata-error", "Attributes changed during save");
                for (std::size_t i = 0; i < names.size();) {
                    const std::string name(names.data() + i); i += name.size() + 1;
                    const ssize_t size = ::fgetxattr(previous.get(), name.c_str(), nullptr, 0);
                    require(size >= 0 && size <= 65536, "metadata-error", "Cannot preserve attribute");
                    std::vector<char> value(static_cast<std::size_t>(size));
                    require(::fgetxattr(previous.get(), name.c_str(), value.data(), value.size()) == size &&
                        ::fsetxattr(output.get(), name.c_str(), value.data(), value.size(), 0) == 0,
                        "metadata-error", "Cannot preserve attribute value");
                }
            }
        }
        require(::fsync(output.get()) == 0, "io-error", "Cannot sync edited file");
        struct stat current{};
        require(::fstatat(parent.get(), path.filename().c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            current.st_dev == before.st_dev && current.st_ino == before.st_ino && sha256(previous.get()) == expected_sha,
            "stale-plan", "Edit target was replaced or changed");
        require(::renameat(parent.get(), temporary.c_str(), parent.get(), path.filename().c_str()) == 0, "io-error", "Atomic rename failed");
        require(::fsync(parent.get()) == 0, "uncertain-save", "Directory sync failed after rename");
    } catch (...) { ::unlinkat(parent.get(), temporary.c_str(), 0); throw; }
}
void Root::save_record(const std::string& relative, const Value& value, bool replace) const {
    require(components(relative).size() == 1, "invalid-path", "Records must be named directly inside the retained directory");
    const std::string content = json(value);
    require(content.size() <= 4 * 1024 * 1024, "size-limit", "JSON record exceeds 4 MiB");
    if (replace && exists(relative)) {
        atomic_save(relative, content, sha256(read(relative, 4 * 1024 * 1024)), true, 4 * 1024 * 1024);
        return;
    }
    // A killed creator must leave either no record or one complete record.
    // O_EXCL directly at the final name would leave truncated journal JSON.
    const auto temporary = ".ure-record-" + operation_id();
    auto output = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    try {
        write_all(output.get(), content);
        require(::fsync(output.get()) == 0, "io-error", "Cannot sync initial JSON record");
        require(::syscall(SYS_renameat2, fd(), temporary.c_str(), fd(), relative.c_str(), RENAME_NOREPLACE) == 0,
            "io-error", "Cannot publish initial JSON record without replacing an existing record");
        require(::fsync(fd()) == 0, "uncertain-save", "Directory sync failed after initial record publication");
    } catch (...) { ::unlinkat(fd(), temporary.c_str(), 0); throw; }
}
void save_json(const fs::path& path, const Value& value, bool replace) {
    Root root(path.parent_path().empty() ? fs::path(".") : path.parent_path());
    root.save_record(path.filename().string(), value, replace);
}
std::uint64_t monotonic_ms() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::string utc() {
    const std::time_t now = std::time(nullptr); std::tm tm{}; ::gmtime_r(&now, &tm);
    std::array<char, 32> data{}; std::strftime(data.data(), data.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return data.data();
}
std::string operation_id() {
    std::array<unsigned char, 16> bytes{};
    require(::getrandom(bytes.data(), bytes.size(), 0) == static_cast<ssize_t>(bytes.size()), "random-error", "Cannot generate operation ID");
    return digest_hex(bytes.data(), static_cast<unsigned>(bytes.size()));
}
Value envelope(const Value& data) {
    Value result; result["schema"] = 1; result["operation_id"] = operation_id();
    result["result"] = "ok"; result["timestamp_utc"] = utc(); result["monotonic_ms"] = Json::UInt64(monotonic_ms());
    result["warnings"] = Value(Json::arrayValue); result["data"] = data;
    return result;
}
std::string redact(std::string text) {
    require(text.size() <= 2 * 1024 * 1024, "size-limit", "Report input exceeds limit");
    text = std::regex_replace(text, std::regex(R"((/(home|Users|var/home)/[^\s]+))"), "[private-path]");
    text = std::regex_replace(text, std::regex(R"(([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2})"), "[mac]");
    std::istringstream input(text); std::ostringstream output; std::string line;
    const std::regex sensitive(R"((password|passphrase|recovery.?key|serial(number|no)?|ssid|psk|authorized_keys|BEGIN .*PRIVATE KEY))", std::regex::icase);
    while (std::getline(input, line)) output << (std::regex_search(line, sensitive) ? "[sensitive line omitted]" : line) << '\n';
    return output.str();
}
static std::string tool_path(const std::string& name) {
    static const std::set<std::string> allowed{"btrfs", "cryptsetup", "e2fsck", "fsck.fat", "fsck.f2fs", "dump.f2fs", "dump.exfat", "fsck.exfat", "ntfsfix", "fsck.ntfs", "ntfsresize", "wimlib-imagex", "lpdump", "bootctl", "dmsetup", "dropbear", "dropbearkey", "ssh-keygen", "sftp-server", "journalctl", "systemctl", "ip", "wpa_cli", "blkid", "readelf", "avbctl", "mke2fs", "mkfs.fat", "mkfs.exfat", "mkfs.ntfs", "make_f2fs", "mkfs.f2fs", "mkfs.btrfs", "resize2fs", "resize.f2fs", "fatresize"};
    require(allowed.contains(name), "tool-rejected", "Tool is outside the reviewed allowlist");
#ifdef __ANDROID__
    const std::vector<std::string> paths{"/system/bin/", "/sbin/"};
#else
    const std::vector<std::string> paths{"/usr/bin/", "/usr/sbin/", "/bin/", "/sbin/"};
#endif
    for (const auto& base : paths) {
        struct stat info{};
        if (::stat((base + name).c_str(), &info) == 0 && S_ISREG(info.st_mode) && (info.st_mode & 0111) != 0)
            return base + name;
    }
    throw Error("missing-tool", "Required reviewed tool is not packaged: " + name);
}
bool tool_available(const std::string& name) {
    try { return !tool_path(name).empty(); } catch (const Error& e) { if (e.code == "missing-tool") return false; throw; }
}
ProcessResult run_tool(const std::string& name, const std::vector<std::string>& args, int timeout_seconds, std::string_view input,
                       const std::vector<int>& inherited_fds) {
    require(args.size() <= 64 && timeout_seconds > 0 && timeout_seconds <= 300 && input.size() <= 4096 && inherited_fds.size()<=8,
        "invalid-process", "Tool bounds exceeded");
    const auto executable = tool_path(name);
    std::vector<std::string> words{executable};
    for (const auto& arg : args) {
        require(arg.size() <= 4096 && arg.find('\0') == arg.npos, "invalid-argument", "Invalid tool argument");
        words.push_back(arg);
    }
    std::vector<char*> argv;
    for (auto& word : words) argv.push_back(word.data());
    argv.push_back(nullptr);
    int out_pipe[2]{}, in_pipe[2]{};
    require(::pipe2(out_pipe, O_CLOEXEC) == 0, "process-error", "Cannot create output pipe");
    Fd out_read(out_pipe[0]), out_write(out_pipe[1]);
    require(::pipe2(in_pipe, O_CLOEXEC) == 0, "process-error", "Cannot create input pipe");
    Fd in_read(in_pipe[0]), in_write(in_pipe[1]);
    // Fill at most PIPE_BUF before fork while the parent owns the read end.
    // This cannot deadlock or raise SIGPIPE if a tool declines its input.
    if (!input.empty()) write_all(in_write.get(), input);
    in_write = Fd();
    const pid_t pid = ::fork();
    require(pid >= 0, "process-error", "Cannot create tool process");
    if (pid == 0) {
        ::setpgid(0, 0);
        ::dup2(in_read.get(), STDIN_FILENO); ::dup2(out_write.get(), STDOUT_FILENO); ::dup2(out_write.get(), STDERR_FILENO);
        for(const int fd : inherited_fds) {
            if(fd<3 || ::fcntl(fd,F_SETFD,0)!=0)::_exit(126);
        }
        char locale[] = "LC_ALL=C"; char path[] = "PATH=/system/bin:/usr/bin:/usr/sbin:/sbin:/bin";
        char* environment[]{locale, path, nullptr};
        ::execve(executable.c_str(), argv.data(), environment); ::_exit(127);
    }
    ::setpgid(pid, pid);
    out_write = Fd(); in_read = Fd();
    ProcessResult result;
    const auto deadline = monotonic_ms() + static_cast<std::uint64_t>(timeout_seconds) * 1000;
    ::fcntl(out_read.get(), F_SETFL, O_NONBLOCK);
    bool exited = false, eof = false, output_limit = false; int status = 0;
    while (!exited || !eof) {
        std::array<char, 4096> buffer{};
        const ssize_t n = ::read(out_read.get(), buffer.data(), buffer.size());
        if (n > 0) {
            if (result.output.size() + static_cast<std::size_t>(n) > 2 * 1024 * 1024) output_limit = true;
            else result.output.append(buffer.data(), static_cast<std::size_t>(n));
        } else if (n == 0) eof = true;
        if (!exited) {
            const pid_t wait = ::waitpid(pid, &status, WNOHANG);
            if (wait == pid) exited = true;
        }
        if (monotonic_ms() >= deadline || output_limit) {
            ::kill(-pid, SIGKILL); ::kill(pid, SIGKILL);
            if (!exited) while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            exited = true; result.timed_out = true; break;
        }
        if (!exited || !eof) { pollfd poll_item{out_read.get(), POLLIN, 0}; ::poll(&poll_item, 1, 20); }
    }
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    require(!output_limit, "output-limit", "Tool output exceeded limit");
    return result;
}
} // namespace ure
