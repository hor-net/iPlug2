#pragma once

// Debug-only, UI-thread lifecycle evidence. Never log message bodies, licensing
// data or URLs. Each host process has a separate file so cold openings can be
// diagnosed even when the host redirects stdout/stderr or is launched by Finder.
#include <cstdarg>
#include <cstdio>
#include <chrono>
#if defined(OS_MAC) && defined(_DEBUG)
#include <unistd.h>
#include <fcntl.h>
#endif

namespace iplug {
inline void TraceWebView(const void* pOwner, const char* event, const char* format = "", ...)
{
#if defined(OS_MAC) && defined(_DEBUG)
  char path[128];
  std::snprintf(path, sizeof(path), "/tmp/iplug-webview-%d.log", getpid());
  const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return;
  FILE* file = ::fdopen(fd, "a");
  if (!file) { ::close(fd); return; }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  std::fprintf(file, "%lld owner=%p %s ", static_cast<long long>(ms), pOwner, event);
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  std::fclose(file);
#else
  (void)pOwner;
  (void)event;
  (void)format;
#endif
}
}
