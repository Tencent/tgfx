// === [DIAG-INTEL] 独立诊断日志文件，复现完后整文件可删除。===
// 所有 DIAG_LOG(...) 输出到 ~/Documents/cocraft_diag_intel.log，
// 同时也尝试 stderr。线程安全（mutex），首行打印环境信息。
#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>

#if defined(__APPLE__)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace tgfx {
namespace diag_intel {

inline std::string DefaultLogPath() {
#if defined(__APPLE__)
  const char* home = getenv("HOME");
  if (home == nullptr) {
    if (struct passwd* pw = getpwuid(getuid())) {
      home = pw->pw_dir;
    }
  }
  if (home != nullptr) {
    return std::string(home) + "/Documents/cocraft_diag_intel.log";
  }
#endif
  return "/tmp/cocraft_diag_intel.log";
}

inline FILE* GetLogFile() {
  static std::mutex init_mutex;
  static FILE* fp = nullptr;
  static bool tried = false;
  std::lock_guard<std::mutex> lock(init_mutex);
  if (!tried) {
    tried = true;
    auto path = DefaultLogPath();
    fp = std::fopen(path.c_str(), "a");
    if (fp != nullptr) {
      std::setvbuf(fp, nullptr, _IOLBF, 0);  // 行缓冲，崩溃也能保住前面内容
      std::time_t now = std::time(nullptr);
      char ts[64] = {0};
      std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
      std::fprintf(fp, "\n====== [DIAG-INTEL] session start @ %s ======\n", ts);
      std::fprintf(fp, "[DIAG-INTEL] log path = %s\n", path.c_str());
#if defined(__aarch64__) || defined(__arm64__)
      std::fprintf(fp, "[DIAG-INTEL] cpu arch = arm64\n");
#elif defined(__x86_64__)
      std::fprintf(fp, "[DIAG-INTEL] cpu arch = x86_64 (Intel)\n");
#else
      std::fprintf(fp, "[DIAG-INTEL] cpu arch = unknown\n");
#endif
      std::fflush(fp);
      std::fprintf(stderr, "[DIAG-INTEL] writing diagnostics to: %s\n", path.c_str());
    } else {
      std::fprintf(stderr, "[DIAG-INTEL] !!! failed to open %s, falling back to stderr\n",
                   path.c_str());
    }
  }
  return fp;
}

inline void Logf(const char* fmt, ...) {
  static std::mutex write_mutex;
  std::lock_guard<std::mutex> lock(write_mutex);
  FILE* fp = GetLogFile();

  // 取毫秒级时间戳
  using namespace std::chrono;
  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  std::time_t t = system_clock::to_time_t(now);
  char ts[32] = {0};
  std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t));

  // 写文件
  if (fp != nullptr) {
    std::fprintf(fp, "[%s.%03lld] ", ts, static_cast<long long>(ms));
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(fp, fmt, ap);
    va_end(ap);
    std::fputc('\n', fp);
  }
  // 同步打 stderr
  std::fprintf(stderr, "[DIAG-INTEL %s.%03lld] ", ts, static_cast<long long>(ms));
  va_list ap2;
  va_start(ap2, fmt);
  std::vfprintf(stderr, fmt, ap2);
  va_end(ap2);
  std::fputc('\n', stderr);
}

}  // namespace diag_intel
}  // namespace tgfx

#define DIAG_LOG(...) ::tgfx::diag_intel::Logf(__VA_ARGS__)
// === [DIAG-INTEL END] ===
