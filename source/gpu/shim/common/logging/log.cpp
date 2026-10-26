// port: see log.h.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "common/logging/log.h"

namespace Common::Log {
static Level threshold() {
    static const Level level = [] {
        const char* v = std::getenv("THEORDER_GPU_LOG");
        if (!v) return Level::warn;
        static const char* names[] = {"trace", "debug", "info", "warning", "error", "critical"};
        for (int i = 0; i < 6; ++i)
            if (!std::strcmp(v, names[i])) return Level(i);
        return Level::warn;
    }();
    return level;
}
bool ShouldLog(Level level) { return level >= threshold(); }
void Write(std::string_view log_class, Level level, const char* file, int line, const char* function,
           std::string&& message) {
    static const char* names[] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};
    const char* base = std::strrchr(file, '/');
    // One fwrite per line: Windows' CRT prints to an unbuffered stream that is a pipe or a file
    // one character per system call (~80-180 us a line instead of ~2).
    const std::string text = fmt::format("GPU [{}] <{}> {}:{} {}: {}\n", log_class,
                                         names[int(level)], base ? base + 1 : file, line, function,
                                         message);
    // stdout's lock as well (as gow3_pc 0d49242): the runtime's printf lines share the pipe and
    // hold it for a whole line, so the two no longer cut into each other. FILE locks are
    // recursive, and nothing here prints.
#ifdef _WIN32
    _lock_file(stdout);
#else
    flockfile(stdout);
#endif
    std::fwrite(text.data(), 1, text.size(), stderr);
#ifdef _WIN32
    _unlock_file(stdout);
#else
    funlockfile(stdout);
#endif
}
} // namespace Common::Log
