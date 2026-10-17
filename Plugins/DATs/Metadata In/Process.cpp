#include "Metadata.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace Metadata {
void Context::check() const {
    if (cancelled()) throw Cancelled{};
    if (Clock::now() >= deadline) throw std::runtime_error("Read timeout exceeded");
}
namespace {
struct FD {
    int value = -1;
    ~FD() { if (value >= 0) close(value); }
    void reset() { if (value >= 0) close(value); value = -1; }
};
struct Child {
    pid_t pid = -1;
    ~Child() {
        if (pid <= 0) return;
        kill(-pid, SIGKILL);
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
};
}
ProcessResult runProcess(const std::vector<std::string>& args, const Context& ctx) {
    ctx.check();
    if (args.empty()) throw std::runtime_error("Empty reader command");
    int descriptors[2];
    if (pipe(descriptors)) throw std::runtime_error("Cannot open reader output pipe");
    FD outRead{descriptors[0]}, outWrite{descriptors[1]};
    if (pipe(descriptors)) throw std::runtime_error("Cannot open reader error pipe");
    FD errRead{descriptors[0]}, errWrite{descriptors[1]};
    for (int fd : {outRead.value, outWrite.value, errRead.value, errWrite.value})
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    // Deliberately exclude Perl injection options, FFREPORT and caller loader settings.
    char envPath[] = "PATH=/usr/bin:/bin";
    char envLocale[] = "LC_ALL=C";
    char* env[] = {envPath, envLocale, nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attrs;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, outWrite.value, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errWrite.value, STDERR_FILENO);
    posix_spawnattr_init(&attrs);
    posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT);
    posix_spawnattr_setpgroup(&attrs, 0);
    Child child;
    int error = posix_spawn(&child.pid, args[0].c_str(), &actions, &attrs, argv.data(), env);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attrs);
    if (error) { child.pid = -1; throw std::runtime_error("Cannot launch reader: " + std::string(strerror(error))); }
    outWrite.reset(); errWrite.reset();
    fcntl(outRead.value, F_SETFL, O_NONBLOCK);
    fcntl(errRead.value, F_SETFL, O_NONBLOCK);
    ProcessResult result;
    int status = 0;
    bool exited = false;
    while (!exited || outRead.value >= 0 || errRead.value >= 0) {
        ctx.check();
        struct pollfd fds[] = {{outRead.value, POLLIN, 0}, {errRead.value, POLLIN, 0}};
        poll(fds, 2, 20);
        for (int i = 0; i < 2; ++i) {
            FD& fd = i == 0 ? outRead : errRead;
            if (fd.value < 0) continue;
            std::string& target = i == 0 ? result.out : result.err;
            // Bound each drain, so an endless writer cannot starve cancellation checks.
            for (int drain = 0; drain < 16; ++drain) {
                char buffer[8192];
                ssize_t n = read(fd.value, buffer, sizeof buffer);
                if (n > 0) {
                    if (target.size() + size_t(n) > (i == 0 ? 16u * 1024 * 1024 : 64u * 1024))
                        throw std::runtime_error("Reader output exceeded capture limit");
                    target.append(buffer, size_t(n));
                } else if (n == 0) { fd.reset(); break; }
                else if (errno == EINTR) continue;
                else if (errno == EAGAIN) break;
                else throw std::runtime_error("Cannot read reader output");
            }
        }
        if (!exited) {
            pid_t done = waitpid(child.pid, &status, WNOHANG);
            if (done == child.pid) exited = true;
            else if (done < 0 && errno != EINTR) throw std::runtime_error("Cannot wait for reader");
        }
    }
    child.pid = -1;
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}
}
