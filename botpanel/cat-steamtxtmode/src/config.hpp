#ifndef CAT_STEAMTXTMODE_CONFIG_HPP
#define CAT_STEAMTXTMODE_CONFIG_HPP

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cat_stm
{

struct config
{
  bool disabled = false;
  bool no_vsync = true;
  bool no_audio = true;
  bool no_present = false;
  bool patches = false;
  bool log = true;
  int present_fps = 0;
  bool webhelper_trim = true;
  bool webhelper_single = false;
};

inline config& settings()
{
  static config instance;
  return instance;
}

inline bool env_flag(const char* name, bool fallback)
{
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0')
  {
    return fallback;
  }
  return !(std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0);
}

inline int env_int(const char* name, int fallback)
{
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0')
  {
    return fallback;
  }
  return std::atoi(value);
}

inline void log_line(const char* fmt, ...)
{
  if (!settings().log)
  {
    return;
  }
  std::fprintf(stderr, "[steamtxtmode] ");
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

}

#endif
