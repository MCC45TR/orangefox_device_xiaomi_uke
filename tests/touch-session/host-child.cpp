// SPDX-License-Identifier: GPL-3.0-or-later
// Disposable host-only child. This never links, loads, or launches OEM code.
#include "touch-policy.hpp"
#include <array>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
static void done(int) { ::_exit(0); }
int main(int, char **argv) {
    const std::string_view path(argv[0]);
    if (path.ends_with("fixture-manager")) {
        using namespace uke::touch;
        for (int fd = 5; fd < 128; ++fd)
            if (::fcntl(fd, F_GETFD) >= 0)
                return 90;
        struct stat a{}, b{};
        if (::fstat(kOwnerFd, &a) != 0 || ::fstat(kStatusFd, &b) != 0 || !S_ISFIFO(a.st_mode) ||
            !S_ISFIFO(b.st_mode))
            return 91;
        Status status;
        status.state = State::running;
        if (::write(kStatusFd, &status, sizeof(status)) != sizeof(status))
            return 92;
        pollfd owner{kOwnerFd, POLLIN | POLLHUP, 0};
        while (::poll(&owner, 1, 100) >= 0) {
            if (owner.revents)
                return 0;
        }
        return 93;
    }
    if (path.ends_with("fixture-exit"))
        return 0;
    if (path.ends_with("fixture-stubborn"))
        ::signal(SIGTERM, SIG_IGN);
    else
        ::signal(SIGTERM, done);
    if (path.ends_with("fixture-flood")) {
        std::array<char, 4096> bytes{};
        bytes.fill('x');
        for (unsigned i = 0; i < 512; ++i)
            if (::write(1, bytes.data(), bytes.size()) < 0)
                return 2;
        return 0;
    }
    while (true)
        ::pause();
}
