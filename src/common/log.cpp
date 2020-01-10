#include "log.h"
#include "assert.h"
#include "string.h"
#include "timer.h"
#include <mutex>
#include <vector>

#if defined(WIN32)
#include "windows_headers.h"
#elif defined(__ANDROID__)
#include <android/log.h>
#else
#include <unistd.h>
#endif

namespace Log {

struct RegisteredCallback
{
  CallbackFunctionType Function;
  void* Parameter;
};

std::vector<RegisteredCallback> s_callbacks;
static std::mutex s_callback_mutex;

static LOGLEVEL s_filter_level = LOGLEVEL_TRACE;

static Common::Timer::Value s_startTimeStamp = Common::Timer::GetValue();

static bool s_consoleOutputEnabled = false;
static String s_consoleOutputChannelFilter;
static LOGLEVEL s_consoleOutputLevelFilter = LOGLEVEL_TRACE;

static bool s_debugOutputEnabled = false;
static String s_debugOutputChannelFilter;
static LOGLEVEL s_debugOutputLevelFilter = LOGLEVEL_TRACE;

void RegisterCallback(CallbackFunctionType callbackFunction, void* pUserParam)
{
  RegisteredCallback Callback;
  Callback.Function = callbackFunction;
  Callback.Parameter = pUserParam;

  std::lock_guard<std::mutex> guard(s_callback_mutex);
  s_callbacks.push_back(std::move(Callback));
}

void UnregisterCallback(CallbackFunctionType callbackFunction, void* pUserParam)
{
  std::lock_guard<std::mutex> guard(s_callback_mutex);

  for (auto iter = s_callbacks.begin(); iter != s_callbacks.end(); ++iter)
  {
    if (iter->Function == callbackFunction && iter->Parameter == pUserParam)
    {
      s_callbacks.erase(iter);
      break;
    }
  }
}

static void ExecuteCallbacks(const char* channelName, const char* functionName, LOGLEVEL level, const char* message)
{
  std::lock_guard<std::mutex> guard(s_callback_mutex);
  for (RegisteredCallback& callback : s_callbacks)
    callback.Function(callback.Parameter, channelName, functionName, level, message);
}

static void FormatLogMessageForDisplay(const char* channelName, const char* functionName, LOGLEVEL level,
                                       const char* message, void (*printCallback)(const char*, void*),
                                       void* pCallbackUserData)
{
  static const char levelCharacters[LOGLEVEL_COUNT] = {'X', 'E', 'W', 'P', 'S', 'I', 'D', 'R', 'B', 'T'};

  // find time since start of process
  float messageTime =
    static_cast<float>(Common::Timer::ConvertValueToSeconds(Common::Timer::GetValue() - s_startTimeStamp));

  // write prefix
#ifndef Y_BUILD_CONFIG_SHIPPING
  char prefix[256];
  if (level <= LOGLEVEL_PERF)
    std::snprintf(prefix, countof(prefix), "[%10.4f] %c(%s): ", messageTime, levelCharacters[level], functionName);
  else
    std::snprintf(prefix, countof(prefix), "[%10.4f] %c/%s: ", messageTime, levelCharacters[level], channelName);

  printCallback(prefix, pCallbackUserData);
#else
  char prefix[256];
  std::snprintf(prefix, countof(prefix), "[%10.4f] %c/%s: ", messageTime, levelCharacters[level], channelName);
  printCallback(prefix, pCallbackUserData);
#endif

  // write message
  printCallback(message, pCallbackUserData);
}

#if defined(WIN32)

static void ConsoleOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName,
                                     LOGLEVEL level, const char* message)
{
  if (!s_consoleOutputEnabled || level > s_consoleOutputLevelFilter ||
      s_consoleOutputChannelFilter.Find(channelName) >= 0)
    return;

  if (level > LOGLEVEL_COUNT)
    level = LOGLEVEL_TRACE;

  HANDLE hConsole = GetStdHandle((level <= LOGLEVEL_WARNING) ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  if (hConsole != INVALID_HANDLE_VALUE)
  {
    static const WORD levelColors[LOGLEVEL_COUNT] = {
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN,                        // NONE
      FOREGROUND_RED | FOREGROUND_INTENSITY,                                      // ERROR
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,                   // WARNING
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,                    // PERF
      FOREGROUND_GREEN | FOREGROUND_INTENSITY,                                    // SUCCESS
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // INFO
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN,                        // DEV
      FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY,                  // PROFILE
      FOREGROUND_GREEN,                                                           // DEBUG
      FOREGROUND_BLUE,                                                            // TRACE
    };

    CONSOLE_SCREEN_BUFFER_INFO oldConsoleScreenBufferInfo;
    GetConsoleScreenBufferInfo(hConsole, &oldConsoleScreenBufferInfo);
    SetConsoleTextAttribute(hConsole, levelColors[level]);

    // write message in the formatted way
    FormatLogMessageForDisplay(
      channelName, functionName, level, message,
      [](const char* text, void* hConsole) {
        DWORD written;
        WriteConsoleA(static_cast<HANDLE>(hConsole), text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
      },
      (void*)hConsole);

    // write newline
    DWORD written;
    WriteConsoleA(hConsole, "\r\n", 2, &written, nullptr);

    // restore color
    SetConsoleTextAttribute(hConsole, oldConsoleScreenBufferInfo.wAttributes);
  }
}

static void DebugOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName, LOGLEVEL level,
                                   const char* message)
{
  if (!s_debugOutputEnabled || level > s_debugOutputLevelFilter || s_debugOutputChannelFilter.Find(channelName) >= 0)
    return;

  FormatLogMessageForDisplay(channelName, functionName, level, message,
                             [](const char* text, void*) { OutputDebugStringA(text); }, nullptr);

  OutputDebugStringA("\n");
}

#elif defined(__ANDROID__)

static void ConsoleOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName,
                                     LOGLEVEL level, const char* message)
{
}

static void DebugOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName, LOGLEVEL level,
                                   const char* message)
{
  if (!s_debugOutputEnabled || level > s_debugOutputLevelFilter || s_debugOutputChannelFilter.Find(functionName) >= 0)
    return;

  static const int logPriority[LOGLEVEL_COUNT] = {
    ANDROID_LOG_INFO,  // NONE
    ANDROID_LOG_ERROR, // ERROR
    ANDROID_LOG_WARN,  // WARNING
    ANDROID_LOG_INFO,  // PERF
    ANDROID_LOG_INFO,  // SUCCESS
    ANDROID_LOG_INFO,  // INFO
    ANDROID_LOG_DEBUG, // DEV
    ANDROID_LOG_DEBUG, // PROFILE
    ANDROID_LOG_DEBUG, // DEBUG
    ANDROID_LOG_DEBUG, // TRACE
  };

  __android_log_write(logPriority[level], channelName, message);
}

#else

static void ConsoleOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName,
                                     LOGLEVEL level, const char* message)
{
  if (!s_consoleOutputEnabled || level > s_consoleOutputLevelFilter ||
      s_consoleOutputChannelFilter.Find(channelName) >= 0)
    return;

  static const char* colorCodes[LOGLEVEL_COUNT] = {
    "\033[0m",    // NONE
    "\033[1;31m", // ERROR
    "\033[1;33m", // WARNING
    "\033[1;35m", // PERF
    "\033[1;32m", // SUCCESS
    "\033[1;37m", // INFO
    "\033[0;37m", // DEV
    "\033[1;36m", // PROFILE
    "\033[0;32m", // DEBUG
    "\033[0;34m", // TRACE
  };

  int outputFd = (level <= LOGLEVEL_WARNING) ? STDERR_FILENO : STDOUT_FILENO;

  write(outputFd, colorCodes[level], std::strlen(colorCodes[level]));

  Log::FormatLogMessageForDisplay(
    channelName, functionName, level, message,
    [](const char* text, void* outputFd) { write((int)(intptr_t)outputFd, text, std::strlen(text)); },
    (void*)(intptr_t)outputFd);

  write(outputFd, colorCodes[0], std::strlen(colorCodes[0]));
  write(outputFd, "\n", 1);
}

static void DebugOutputLogCallback(void* pUserParam, const char* channelName, const char* functionName, LOGLEVEL level,
                                   const char* message)
{
}

#endif

void SetConsoleOutputParams(bool Enabled, const char* ChannelFilter, LOGLEVEL LevelFilter)
{
  if (s_consoleOutputEnabled != Enabled)
  {
    s_consoleOutputEnabled = Enabled;
    if (Enabled)
      RegisterCallback(ConsoleOutputLogCallback, NULL);
    else
      UnregisterCallback(ConsoleOutputLogCallback, NULL);

#if defined(WIN32)
    // On windows, no console is allocated by default on a windows based application
    static bool consoleWasAllocated = false;
    if (Enabled)
    {
      if (GetConsoleWindow() == NULL)
      {
        DebugAssert(!consoleWasAllocated);
        consoleWasAllocated = true;
        AllocConsole();

        std::FILE* fp;
        freopen_s(&fp, "CONIN$", "r", stdin);
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
      }
    }
    else
    {
      if (consoleWasAllocated)
      {
        FreeConsole();
        consoleWasAllocated = false;
      }
    }
#endif
  }

  s_consoleOutputChannelFilter = (ChannelFilter != NULL) ? ChannelFilter : "";
  s_consoleOutputLevelFilter = LevelFilter;
}

void SetDebugOutputParams(bool enabled, const char* channelFilter /* = nullptr */,
                          LOGLEVEL levelFilter /* = LOGLEVEL_TRACE */)
{
  if (s_debugOutputEnabled != enabled)
  {
    s_debugOutputEnabled = enabled;
    if (enabled)
      RegisterCallback(DebugOutputLogCallback, nullptr);
    else
      UnregisterCallback(DebugOutputLogCallback, nullptr);
  }

  s_debugOutputChannelFilter = (channelFilter != nullptr) ? channelFilter : "";
  s_debugOutputLevelFilter = levelFilter;
}

void SetFilterLevel(LOGLEVEL level)
{
  DebugAssert(level < LOGLEVEL_COUNT);
  s_filter_level = level;
}

void Write(const char* channelName, const char* functionName, LOGLEVEL level, const char* message)
{
  if (level > s_filter_level)
    return;

  ExecuteCallbacks(channelName, functionName, level, message);
}

void Writef(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, ...)
{
  if (level > s_filter_level)
    return;

  va_list ap;
  va_start(ap, format);
  Writev(channelName, functionName, level, format, ap);
  va_end(ap);
}

void Writev(const char* channelName, const char* functionName, LOGLEVEL level, const char* format, va_list ap)
{
  if (level > s_filter_level)
    return;

  va_list apCopy;
  va_copy(apCopy, ap);

#ifdef WIN32
  u32 requiredSize = static_cast<u32>(_vscprintf(format, ap));
#else
  u32 requiredSize = std::vsnprintf(nullptr, 0, format, ap);
#endif
  if (requiredSize < 256)
  {
    char buffer[256];
    std::vsnprintf(buffer, countof(buffer), format, ap);
    ExecuteCallbacks(channelName, functionName, level, buffer);
  }
  else
  {
    char* buffer = new char[requiredSize + 1];
    std::vsnprintf(buffer, requiredSize + 1, format, ap);
    ExecuteCallbacks(channelName, functionName, level, buffer);
    delete[] buffer;
  }
}

} // namespace Log