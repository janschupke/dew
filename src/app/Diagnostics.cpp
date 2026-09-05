#include "app/Diagnostics.h"

#if JUCE_MAC || JUCE_LINUX
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dew::diagnostics
{

namespace
{

constexpr auto logSubDirectory = "dew";
constexpr auto logPrefix = "dew_log_";
constexpr auto logSuffix = ".txt";

/** What a crash marker is called before it has been told, and after. The glob
    is how the next launch finds one, so the two must stay a pair. */
constexpr auto crashPrefix = "dew-crash-";
constexpr auto crashSuffix = ".txt";
constexpr auto reportedSuffix = ".reported";

std::unique_ptr<juce::FileLogger> logger;

/** The crash file's path, as bytes, decided before any crash.

    A fixed buffer and not a juce::String, because the only code that reads it
    runs in a signal handler. Building a path there would allocate, and an
    allocator is one of the things a crashed process most often no longer has.
*/
char crashPath[1024] = { 0 };

#if JUCE_MAC || JUCE_LINUX

/** Written with write(2) rather than a stream, for the same reason.

    Everything this function calls is async-signal-safe: open, write, close, and
    backtrace_symbols_fd, which exists precisely because backtrace_symbols
    allocates and is therefore useless here. No juce::String, no Logger, no
    Settings - all three allocate, and one of them takes a lock.
*/
void handleCrash (void*)
{
    if (crashPath[0] == 0)
        return;

    const int fd = ::open (crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd < 0)
        return;

    static constexpr char
        header[] = "dew stopped unexpectedly.\n\n"
                   "This file says roughly where. The operating system writes a much fuller\n"
                   "report at the same moment - on macOS look in\n"
                   "~/Library/Logs/DiagnosticReports for a file whose name starts with dew.\n"
                   "The dew_log_ file beside this one says what dew was doing before it.\n\n"
                   "Backtrace:\n";

    // Unchecked on purpose. There is no answer to a failed write here, and a
    // handler that branched on one would be a handler doing more work than it
    // can safely do.
    const auto ignored = ::write (fd, header, sizeof (header) - 1);
    juce::ignoreUnused (ignored);

    void* frames[64];
    const int depth = ::backtrace (frames, 64);
    ::backtrace_symbols_fd (frames, depth, fd);

    ::close (fd);
}

#endif

} // namespace

juce::File logDirectory()
{
    if (logger != nullptr)
        return logger->getLogFile().getParentDirectory();

    return juce::FileLogger::getSystemLogFileFolder().getChildFile (logSubDirectory);
}

void begin (const juce::String& welcome)
{
    if (logger != nullptr)
        return;

    logger.reset (
        juce::FileLogger::createDateStampedLogger (logSubDirectory, logPrefix, logSuffix, welcome));

    juce::Logger::setCurrentLogger (logger.get());

    // Named for the launch rather than for the crash, because the name has to
    // be decided while it is still safe to build one.
    const auto path = logDirectory()
                          .getChildFile (
                              crashPrefix
                              + juce::Time::getCurrentTime().formatted ("%Y-%m-%d-%H%M%S")
                              + crashSuffix)
                          .getFullPathName();

    path.copyToUTF8 (crashPath, (int) sizeof (crashPath));

#if JUCE_MAC || JUCE_LINUX
    juce::SystemStats::setApplicationCrashHandler (handleCrash);
#endif
}

void end()
{
    juce::Logger::setCurrentLogger (nullptr);
    logger.reset();
}

void log (const juce::String& message)
{
    if (logger != nullptr)
        logger->logMessage (juce::Time::getCurrentTime().toISO8601 (true) + "  " + message);
}

juce::File unreportedCrash()
{
    const auto directory = logDirectory();

    if (! directory.isDirectory())
        return {};

    // The newest, when there are several. Somebody who has crashed twice is
    // told about the crash they just had, not the one they had last week.
    juce::File newest;

    for (const auto& entry : juce::RangedDirectoryIterator (
             directory, false, juce::String (crashPrefix) + "*" + crashSuffix,
             juce::File::findFiles))
    {
        const auto file = entry.getFile();

        if (newest == juce::File() || file.getCreationTime() > newest.getCreationTime())
            newest = file;
    }

    return newest;
}

void markReported (const juce::File& crash)
{
    if (crash.existsAsFile())
        crash.moveFileTo (crash.getSiblingFile (crash.getFileName() + reportedSuffix));
}

} // namespace dew::diagnostics
