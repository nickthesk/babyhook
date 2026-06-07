#include "config.hpp"

#include <atomic>
#include <cstring>
#include <ctime>

#include <dlfcn.h>

namespace cat_stm
{
void apply_steam_patches();
}

namespace
{

using namespace cat_stm;

using gl_proc = void (*)();
using dlopen_fn = void* (*)(const char*, int);
using dlmopen_fn = void* (*)(long, const char*, int);

template <typename Fn>
Fn next_symbol(Fn& slot, const char* name)
{
  if (slot == nullptr)
  {
    slot = reinterpret_cast<Fn>(::dlsym(RTLD_NEXT, name));
  }
  return slot;
}

std::atomic<long long> g_last_present_ns{ 0 };

long long monotonic_ns()
{
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

bool should_present()
{
  const config& cfg = settings();
  if (cfg.disabled)
  {
    return true;
  }
  if (cfg.no_present)
  {
    return false;
  }
  if (cfg.present_fps > 0)
  {
    const long long min_interval = 1000000000LL / cfg.present_fps;
    const long long now = monotonic_ns();
    const long long last = g_last_present_ns.load(std::memory_order_relaxed);
    if (now - last < min_interval)
    {
      return false;
    }
    g_last_present_ns.store(now, std::memory_order_relaxed);
  }
  return true;
}

int effective_swap_interval(int requested)
{
  const config& cfg = settings();
  return (!cfg.disabled && cfg.no_vsync) ? 0 : requested;
}

bool path_base_matches(const char* path, const char* name)
{
  if (path == nullptr)
  {
    return false;
  }
  const char* slash = std::strrchr(path, '/');
  const char* base = slash ? slash + 1 : path;
  return std::strcmp(base, name) == 0;
}

}

#define CAT_STM_EXPORT extern "C" __attribute__((visibility("default")))

CAT_STM_EXPORT void glXSwapBuffers(void* dpy, unsigned long drawable)
{
  static void (*real)(void*, unsigned long) = nullptr;
  if (settings().disabled || should_present())
  {
    if (next_symbol(real, "glXSwapBuffers") != nullptr)
    {
      real(dpy, drawable);
    }
  }
}

CAT_STM_EXPORT unsigned int eglSwapBuffers(void* dpy, void* surface)
{
  static unsigned int (*real)(void*, void*) = nullptr;
  if (settings().disabled || should_present())
  {
    if (next_symbol(real, "eglSwapBuffers") != nullptr)
    {
      return real(dpy, surface);
    }
  }
  return 1u;
}

CAT_STM_EXPORT void SDL_GL_SwapWindow(void* window)
{
  static void (*real)(void*) = nullptr;
  if (settings().disabled || should_present())
  {
    if (next_symbol(real, "SDL_GL_SwapWindow") != nullptr)
    {
      real(window);
    }
  }
}

CAT_STM_EXPORT unsigned int eglSwapInterval(void* dpy, int interval)
{
  static unsigned int (*real)(void*, int) = nullptr;
  if (next_symbol(real, "eglSwapInterval") == nullptr)
  {
    return 1u;
  }
  return real(dpy, effective_swap_interval(interval));
}

CAT_STM_EXPORT void glXSwapIntervalEXT(void* dpy, unsigned long drawable, int interval)
{
  static void (*real)(void*, unsigned long, int) = nullptr;
  if (next_symbol(real, "glXSwapIntervalEXT") != nullptr)
  {
    real(dpy, drawable, effective_swap_interval(interval));
  }
}

CAT_STM_EXPORT int glXSwapIntervalSGI(int interval)
{
  static int (*real)(int) = nullptr;
  if (next_symbol(real, "glXSwapIntervalSGI") == nullptr)
  {
    return 0;
  }
  return real(effective_swap_interval(interval));
}

CAT_STM_EXPORT int glXSwapIntervalMESA(unsigned int interval)
{
  static int (*real)(unsigned int) = nullptr;
  if (next_symbol(real, "glXSwapIntervalMESA") == nullptr)
  {
    return 0;
  }
  return real(static_cast<unsigned int>(effective_swap_interval(static_cast<int>(interval))));
}

CAT_STM_EXPORT int SDL_GL_SetSwapInterval(int interval)
{
  static int (*real)(int) = nullptr;
  if (next_symbol(real, "SDL_GL_SetSwapInterval") == nullptr)
  {
    return 0;
  }
  return real(effective_swap_interval(interval));
}

namespace
{
gl_proc proc_override(const char* name)
{
  if (name == nullptr || settings().disabled)
  {
    return nullptr;
  }
  struct entry { const char* name; gl_proc fn; };
  static const entry table[] = {
    { "glXSwapBuffers", reinterpret_cast<gl_proc>(&glXSwapBuffers) },
    { "eglSwapBuffers", reinterpret_cast<gl_proc>(&eglSwapBuffers) },
    { "eglSwapInterval", reinterpret_cast<gl_proc>(&eglSwapInterval) },
    { "glXSwapIntervalEXT", reinterpret_cast<gl_proc>(&glXSwapIntervalEXT) },
    { "glXSwapIntervalSGI", reinterpret_cast<gl_proc>(&glXSwapIntervalSGI) },
    { "glXSwapIntervalMESA", reinterpret_cast<gl_proc>(&glXSwapIntervalMESA) },
  };
  for (const entry& item : table)
  {
    if (std::strcmp(item.name, name) == 0)
    {
      return item.fn;
    }
  }
  return nullptr;
}
}

CAT_STM_EXPORT gl_proc eglGetProcAddress(const char* name)
{
  static gl_proc (*real)(const char*) = nullptr;
  if (gl_proc override = proc_override(name))
  {
    return override;
  }
  return next_symbol(real, "eglGetProcAddress") != nullptr ? real(name) : nullptr;
}

CAT_STM_EXPORT gl_proc glXGetProcAddressARB(const unsigned char* name)
{
  static gl_proc (*real)(const unsigned char*) = nullptr;
  if (gl_proc override = proc_override(reinterpret_cast<const char*>(name)))
  {
    return override;
  }
  return next_symbol(real, "glXGetProcAddressARB") != nullptr ? real(name) : nullptr;
}

CAT_STM_EXPORT gl_proc glXGetProcAddress(const unsigned char* name)
{
  static gl_proc (*real)(const unsigned char*) = nullptr;
  if (gl_proc override = proc_override(reinterpret_cast<const char*>(name)))
  {
    return override;
  }
  return next_symbol(real, "glXGetProcAddress") != nullptr ? real(name) : nullptr;
}

CAT_STM_EXPORT void* dlopen(const char* filename, int flags)
{
  static dlopen_fn real = nullptr;
  void* result = next_symbol(real, "dlopen") != nullptr ? real(filename, flags) : nullptr;
  if (result != nullptr && !settings().disabled && settings().patches && path_base_matches(filename, "libcef.so"))
  {
    apply_steam_patches();
  }
  return result;
}

CAT_STM_EXPORT void* dlmopen(long namespace_id, const char* filename, int flags)
{
  static dlmopen_fn real = nullptr;
  void* result = next_symbol(real, "dlmopen") != nullptr ? real(namespace_id, filename, flags) : nullptr;
  if (result != nullptr && !settings().disabled && settings().patches && path_base_matches(filename, "libcef.so"))
  {
    apply_steam_patches();
  }
  return result;
}

CAT_STM_EXPORT void* alcOpenDevice(const char* device_name)
{
  static void* (*real)(const char*) = nullptr;
  if (!settings().disabled && settings().no_audio)
  {
    return nullptr;
  }
  return next_symbol(real, "alcOpenDevice") != nullptr ? real(device_name) : nullptr;
}

CAT_STM_EXPORT int snd_pcm_open(void** pcm, const char* name, int stream, int mode)
{
  static int (*real)(void**, const char*, int, int) = nullptr;
  if (!settings().disabled && settings().no_audio)
  {
    return -2;
  }
  return next_symbol(real, "snd_pcm_open") != nullptr ? real(pcm, name, stream, mode) : -2;
}

namespace
{
__attribute__((constructor)) void cat_steamtxtmode_init()
{
  config& cfg = settings();
  cfg.log = env_flag("CAT_STM_LOG", true);
  cfg.disabled = env_flag("CAT_STM_DISABLE", false);
  cfg.no_vsync = env_flag("CAT_STM_NO_VSYNC", true);
  cfg.no_audio = env_flag("CAT_STM_NO_AUDIO", true);
  cfg.no_present = env_flag("CAT_STM_NO_PRESENT", true);
  cfg.patches = env_flag("CAT_STM_PATCHES", true);
  cfg.present_fps = env_int("CAT_STM_PRESENT_FPS", 0);
  cfg.webhelper_trim = env_flag("CAT_STM_WEBHELPER_TRIM", true);
  cfg.webhelper_single = env_flag("CAT_STM_WEBHELPER_SINGLE", false);

  if (cfg.disabled)
  {
    log_line("disabled via CAT_STM_DISABLE");
    return;
  }

  log_line("loaded (%d-bit): trim=%d single=%d no_vsync=%d no_audio=%d no_present=%d patches=%d",
           static_cast<int>(sizeof(void*) * 8), cfg.webhelper_trim, cfg.webhelper_single,
           cfg.no_vsync, cfg.no_audio, cfg.no_present, cfg.patches);

  if (cfg.patches)
  {
    apply_steam_patches();
  }
}
}
