// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch-gui-session.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace uke::touch {
namespace {
#if defined(UKE_TOUCH_GUI_HOST_CONTROLS) && !defined(__ANDROID__)
constexpr const char *kManager = "./host-check/fixture-manager";
#else
constexpr const char *kManager = "/system/bin/uke-touch-supervisor";
#endif
std::uint64_t milliseconds() noexcept {
    timespec t{};
    if (::clock_gettime(CLOCK_MONOTONIC, &t) != 0)
        return 0;
    return static_cast<std::uint64_t>(t.tv_sec) * 1000 + static_cast<unsigned>(t.tv_nsec) / 1000000;
}
void close_fd(int &fd) noexcept {
    if (fd >= 0)
        ::close(fd);
    fd = -1;
}
bool pipe_above_targets(int (&fds)[2]) noexcept {
    int raw[2];
    if (::pipe2(raw, O_CLOEXEC) != 0)
        return false;
    fds[0] = ::fcntl(raw[0], F_DUPFD_CLOEXEC, 10);
    fds[1] = ::fcntl(raw[1], F_DUPFD_CLOEXEC, 10);
    ::close(raw[0]);
    ::close(raw[1]);
    if (fds[0] < 0 || fds[1] < 0) {
        close_fd(fds[0]);
        close_fd(fds[1]);
        return false;
    }
    return true;
}
struct Session {
    std::mutex mutex;
    Readiness readiness;
    Status status{};
    pid_t pid = -1;
    int owner = -1, report = -1;
    bool stopping = false;
    std::array<unsigned char, sizeof(Status)> bytes{};
    std::size_t used = 0;
    std::uint64_t started = 0, stop_requested = 0;
    unsigned stop_stage = 0;
    ~Session() { shutdown(); }
    void fail(Code code) noexcept {
        status.state = State::failed;
        status.code = code;
    }
    bool reaped() noexcept {
        if (pid <= 1)
            return true;
        int exit = 0;
        const auto rc = ::waitpid(pid, &exit, WNOHANG);
        if (rc == pid || (rc < 0 && errno == ECHILD)) {
            pid = -1;
            close_fd(owner);
            close_fd(report);
            if (!stopping && (status.state == State::running || status.state == State::preparing))
                fail(Code::child_exited);
            return true;
        }
        return false;
    }
    bool wait_until(std::uint64_t deadline) noexcept {
        while (!reaped() && milliseconds() < deadline) {
            pollfd fd{report, POLLIN | POLLHUP, 0};
            ::poll(&fd, report >= 0 ? 1 : 0, 20);
            collect();
        }
        return reaped();
    }
    void shutdown() noexcept {
        readiness.stop();
        stopping = true;
        close_fd(owner);
        if (pid > 1 && !wait_until(milliseconds() + kGraceMs + 500)) {
            // Only signal the still-unreaped PID returned by our own spawn.
            ::kill(pid, SIGTERM);
            if (!wait_until(milliseconds() + 250)) {
                ::kill(pid, SIGKILL);
                if (!wait_until(milliseconds() + 1000)) {
                    fail(Code::cleanup_failed);
                    return;
                }
            }
        }
        close_fd(report);
        if (status.state == State::preparing || status.state == State::running ||
            status.state == State::idle)
            status.state = State::stopped;
    }
    void launch() noexcept {
        int alive[2]{-1, -1}, reporting[2]{-1, -1};
        if (!pipe_above_targets(alive) || !pipe_above_targets(reporting)) {
            close_fd(alive[0]);
            close_fd(alive[1]);
            close_fd(reporting[0]);
            close_fd(reporting[1]);
            fail(Code::spawn_failed);
            return;
        }
        posix_spawn_file_actions_t files;
        posix_spawnattr_t attr;
        int rc = ::posix_spawn_file_actions_init(&files);
        if (rc != 0) {
            for (int *fd : {&alive[0], &alive[1], &reporting[0], &reporting[1]})
                close_fd(*fd);
            fail(Code::spawn_failed);
            return;
        }
        const int ar = ::posix_spawnattr_init(&attr);
        if (ar != 0) {
            ::posix_spawn_file_actions_destroy(&files);
            for (int *fd : {&alive[0], &alive[1], &reporting[0], &reporting[1]})
                close_fd(*fd);
            fail(Code::spawn_failed);
            return;
        }
        rc = ::posix_spawn_file_actions_adddup2(&files, alive[0], kOwnerFd);
        if (rc == 0)
            rc = ::posix_spawn_file_actions_adddup2(&files, reporting[1], kStatusFd);
        // Close all known descriptors, including pipe ends, in the spawn child.
        // The standalone manager closes again before any mount or OEM exec,
        // covering a descriptor opened concurrently by another recovery thread.
        DIR *entries = ::opendir("/proc/self/fd");
        if (!entries)
            rc = EIO;
        if (entries) {
            while (auto *entry = ::readdir(entries)) {
                unsigned fd = 0;
                if (!number(entry->d_name, fd) || fd <= 4 ||
                    fd == static_cast<unsigned>(::dirfd(entries)))
                    continue;
                if (rc == 0)
                    rc = ::posix_spawn_file_actions_addclose(&files, static_cast<int>(fd));
            }
            ::closedir(entries);
        }
        sigset_t mask, defaults;
        ::sigemptyset(&mask);
        ::sigemptyset(&defaults);
        for (const int signal : {SIGTERM, SIGINT, SIGHUP, SIGALRM, SIGPIPE})
            ::sigaddset(&defaults, signal);
        if (rc == 0)
            rc = ::posix_spawnattr_setsigmask(&attr, &mask);
        if (rc == 0)
            rc = ::posix_spawnattr_setsigdefault(&attr, &defaults);
        if (rc == 0)
            rc = ::posix_spawnattr_setpgroup(&attr, 0);
        if (rc == 0)
            rc = ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF |
                                                       POSIX_SPAWN_SETPGROUP);
        char *args[] = {const_cast<char *>(kManager), nullptr};
        char *environment[] = {const_cast<char *>("PATH=/system/bin:/sbin"),
                               const_cast<char *>("TMPDIR=/tmp"), nullptr};
        pid_t child = -1;
        if (rc == 0)
            rc = ::posix_spawn(&child, kManager, &files, &attr, args, environment);
        ::posix_spawn_file_actions_destroy(&files);
        ::posix_spawnattr_destroy(&attr);
        close_fd(alive[0]);
        close_fd(reporting[1]);
        if (rc != 0 || child <= 1) {
            close_fd(alive[1]);
            close_fd(reporting[0]);
            fail(Code::spawn_failed);
            return;
        }
        pid = child;
        owner = alive[1];
        report = reporting[0];
        started = milliseconds();
        status.state = State::preparing;
        const int flags = ::fcntl(report, F_GETFL);
        if (flags < 0 || ::fcntl(report, F_SETFL, flags | O_NONBLOCK) != 0) {
            fail(Code::io_failed);
            shutdown();
        }
    }
    void collect() noexcept {
        if (report < 0)
            return;
        for (unsigned packets = 0; packets < 16; ++packets) {
            const auto n = ::read(report, bytes.data() + used, bytes.size() - used);
            if (n == 0) {
                if (used)
                    fail(Code::status_invalid);
                close_fd(report);
                break;
            }
            if (n < 0) {
                if (errno != EAGAIN && errno != EINTR) {
                    fail(Code::io_failed);
                    close_fd(report);
                }
                break;
            }
            used += static_cast<std::size_t>(n);
            if (used != bytes.size())
                break;
            Status next{};
            std::memcpy(&next, bytes.data(), sizeof(next));
            used = 0;
            const bool terminal = status.state == State::stopped ||
                                  status.state == State::refused || status.state == State::failed;
            if (!valid_status(next) || next.state == State::idle ||
                (terminal && next.state != status.state) ||
                (status.state == State::running && next.state == State::preparing) ||
                next.drained < status.drained || next.retained < status.retained) {
                fail(Code::status_invalid);
                close_fd(owner);
                close_fd(report);
                break;
            }
            status = next;
        }
    }
};
Session session;
} // namespace
void graphics_ready(bool ok, bool scan) noexcept {
    std::lock_guard<std::mutex> lock(session.mutex);
    session.readiness.graphics(ok, scan);
}
void resources_ready(bool loaded) noexcept {
    std::lock_guard<std::mutex> lock(session.mutex);
    session.readiness.resources(loaded);
}
void start_once_after_frame(bool awake) noexcept {
    std::lock_guard<std::mutex> lock(session.mutex);
    if (session.readiness.frame(awake))
        session.launch();
}
void poll_status() noexcept {
    std::unique_lock<std::mutex> lock(session.mutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    session.collect();
    session.reaped();
    if (session.pid > 1 && session.status.state == State::preparing &&
        milliseconds() - session.started > kStartupMs + 1000) {
        session.fail(Code::startup_timeout);
        close_fd(session.owner);
        session.stop_requested = milliseconds();
        session.stop_stage = 1;
    }
    if (session.pid > 1 && session.stop_stage == 1 &&
        milliseconds() - session.stop_requested > kGraceMs + 500) {
        ::kill(session.pid, SIGTERM);
        session.stop_stage = 2;
        session.stop_requested = milliseconds();
    }
    if (session.pid > 1 && session.stop_stage == 2 &&
        milliseconds() - session.stop_requested > 250) {
        ::kill(session.pid, SIGKILL);
        session.stop_stage = 3;
        session.stop_requested = milliseconds();
    }
    if (session.pid > 1 && session.stop_stage == 3 &&
        milliseconds() - session.stop_requested > 1000)
        session.fail(Code::cleanup_failed);
}
Status collected_status() noexcept {
    std::lock_guard<std::mutex> lock(session.mutex);
    return session.status;
}
bool take_initial_panel_sync(bool awake) noexcept {
    std::lock_guard<std::mutex> lock(session.mutex);
    return session.readiness.initial_panel_sync(session.status.state, awake, milliseconds());
}
void stop() noexcept {
    std::lock_guard<std::mutex> lock(session.mutex);
    session.shutdown();
}
} // namespace uke::touch
