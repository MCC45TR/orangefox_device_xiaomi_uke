// Host-only directory publication. No tablet, block device or recursive deletion.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct Descriptor {
    int fd = -1;
    ~Descriptor() { if (fd >= 0) close(fd); }
};

static bool split(const std::string& path, std::string& parent, std::string& name) {
    const auto slash = path.rfind('/');
    if (slash == std::string::npos || slash + 1 == path.size()) return false;
    parent = slash == 0 ? "/" : path.substr(0, slash);
    name = path.substr(slash + 1);
    return name != "." && name != "..";
}

int main(int argc, char** argv) {
    if (argc != 4 || (std::strcmp(argv[1], "--exchange") && std::strcmp(argv[1], "--new"))) return 2;
    std::string source_parent, source_name, destination_parent, destination_name;
    if (!split(argv[2], source_parent, source_name) || !split(argv[3], destination_parent, destination_name)) return 2;
    Descriptor a, b;
    a.fd = open(source_parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    b.fd = open(destination_parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat source{}, destination{};
    if (a.fd < 0 || b.fd < 0 || fstatat(a.fd, source_name.c_str(), &source, AT_SYMLINK_NOFOLLOW) < 0 ||
        !S_ISDIR(source.st_mode) || source.st_uid != getuid() || (source.st_mode & 0022)) {
        std::cerr << "Unowned, writable or indirect source directory refused.\n"; return 1;
    }
    const bool exchange = !std::strcmp(argv[1], "--exchange");
    const int present = fstatat(b.fd, destination_name.c_str(), &destination, AT_SYMLINK_NOFOLLOW);
    if (exchange) {
        if (present < 0 || !S_ISDIR(destination.st_mode) || destination.st_uid != getuid() ||
            (destination.st_mode & 0022) || source.st_dev != destination.st_dev) {
            std::cerr << "Unowned, writable, indirect or cross-filesystem destination refused.\n"; return 1;
        }
    } else if (present == 0 || errno != ENOENT) {
        std::cerr << "Existing or unreadable destination refused.\n"; return 1;
    }
    // Linux renameat2: RENAME_NOREPLACE=1, RENAME_EXCHANGE=2. Failure preserves
    // both paths; a successful exchange has no missing-output window.
    if (syscall(SYS_renameat2, a.fd, source_name.c_str(), b.fd, destination_name.c_str(), exchange ? 2U : 1U) < 0) {
        std::cerr << "Atomic directory publication failed: " << std::strerror(errno) << '\n'; return 1;
    }
    if (fsync(a.fd) < 0 || fsync(b.fd) < 0) {
        std::cerr << "Directory publication sync failed; completion must remain unaccepted.\n"; return 1;
    }
}
