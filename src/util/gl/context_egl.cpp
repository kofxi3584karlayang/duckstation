// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "context_egl.h"

#include "common/assert.h"
#include "common/dynamic_library.h"
#include "common/error.h"
#include "common/log.h"

#include <atomic>
#include <cstring>
#include <optional>
#include <vector>

Log_SetChannel(GL::Context);

static DynamicLibrary s_egl_library;
static std::atomic_uint32_t s_egl_refcount = 0;

static bool LoadEGL()
{
  // We're not going to be calling this from multiple threads concurrently.
  // So, not wrapping this in a mutex should be fine.
  if (s_egl_refcount.fetch_add(1, std::memory_order_acq_rel) == 0)
  {
    DebugAssert(!s_egl_library.IsOpen());

    const std::string egl_libname = DynamicLibrary::GetVersionedFilename("libEGL");
    Log_InfoFmt("Loading EGL from {}...", egl_libname);

    Error error;
    if (!s_egl_library.Open(egl_libname.c_str(), &error))
      Log_ErrorFmt("Failed to load EGL: {}", error.GetDescription());
  }

  return s_egl_library.IsOpen();
}

static void UnloadEGL()
{
  DebugAssert(s_egl_refcount.load(std::memory_order_acquire) > 0);
  if (s_egl_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1)
  {
    Log_InfoPrint("Unloading EGL.");
    s_egl_library.Close();
  }
}

static bool LoadGLADEGL(EGLDisplay display, Error* error)
{
  const int version =
    gladLoadEGL(display, [](const char* name) { return (GLADapiproc)s_egl_library.GetSymbolAddress(name); });
  if (version == 0)
  {
    Error::SetStringView(error, "Loading GLAD EGL functions failed");
    return false;
  }

  Log_DevFmt("GLAD EGL Version: {}.{}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
  return true;
}

static std::vector<EGLint> EGLAttribToInt(const EGLAttrib* attribs)
{
  std::vector<EGLint> int_attribs;
  if (attribs)
  {
    for (const EGLAttrib* attrib = attribs; *attrib != EGL_NONE;)
    {
      int_attribs.push_back(static_cast<EGLint>(*(attrib++))); // key
      int_attribs.push_back(static_cast<EGLint>(*(attrib++))); // value
    }
    int_attribs.push_back(EGL_NONE);
    int_attribs.push_back(0);
  }
  return int_attribs;
}

namespace GL {
ContextEGL::ContextEGL(const WindowInfo& wi) : Context(wi)
{
  LoadEGL();
}

ContextEGL::~ContextEGL()
{
  DestroySurface();
  DestroyContext();
  UnloadEGL();
}

std::unique_ptr<Context> ContextEGL::Create(const WindowInfo& wi, std::span<const Version> versions_to_try,
                                            Error* error)
{
  std::unique_ptr<ContextEGL> context = std::make_unique<ContextEGL>(wi);
  if (!context->Initialize(versions_to_try, error))
    return nullptr;

  return context;
}

bool ContextEGL::Initialize(std::span<const Version> versions_to_try, Error* error)
{
  if (!LoadGLADEGL(EGL_NO_DISPLAY, error))
    return false;

  m_display = GetPlatformDisplay(nullptr, error);
  if (m_display == EGL_NO_DISPLAY)
    return false;

  int egl_major, egl_minor;
  if (!eglInitialize(m_display, &egl_major, &egl_minor))
  {
    const int gerror = static_cast<int>(eglGetError());
    Error::SetStringFmt(error, "eglInitialize() failed: {} (0x{:X})", gerror, gerror);
    return false;
  }

  Log_DevFmt("eglInitialize() version: {}.{}", egl_major, egl_minor);

  // Re-initialize EGL/GLAD.
  if (!LoadGLADEGL(m_display, error))
    return false;

  if (!GLAD_EGL_KHR_surfaceless_context)
    Log_WarningPrint("EGL implementation does not support surfaceless contexts, emulating with pbuffers");

  for (const Version& cv : versions_to_try)
  {
    if (CreateContextAndSurface(cv, nullptr, true))
      return true;
  }

  Error::SetStringView(error, "Failed to create any context versions");
  return false;
}

EGLDisplay ContextEGL::GetPlatformDisplay(const EGLAttrib* attribs, Error* error)
{
  EGLDisplay dpy = TryGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, attribs);
  if (dpy == EGL_NO_DISPLAY)
    dpy = GetFallbackDisplay(error);

  return dpy;
}

EGLDisplay ContextEGL::TryGetPlatformDisplay(EGLenum platform, const EGLAttrib* attribs)
{
  const char* extensions_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (!extensions_str)
  {
    Log_ErrorPrint("No extensions supported.");
    return EGL_NO_DISPLAY;
  }

  EGLDisplay dpy = EGL_NO_DISPLAY;
  if (std::strstr(extensions_str, "EGL_KHR_platform_base"))
  {
    Log_DevPrint("Using EGL_KHR_platform_base.");

    PFNEGLGETPLATFORMDISPLAYPROC get_platform_display;
    if (s_egl_library.GetSymbol("eglGetPlatformDisplay", &get_platform_display))
    {
      dpy = get_platform_display(platform, m_wi.display_connection, attribs);
      if (dpy == EGL_NO_DISPLAY)
      {
        const EGLint err = eglGetError();
        Log_ErrorFmt("eglGetPlatformDisplay() failed: {} (0x{:X})", err, err);
      }
    }
    else
    {
      Log_WarningPrint("eglGetPlatformDisplay() was not found");
    }
  }
  else if (std::strstr(extensions_str, "EGL_EXT_platform_base"))
  {
    Log_DevPrint("Using EGL_EXT_platform_base.");

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display_ext =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display_ext)
    {
      const std::vector<EGLint> int_attribs = EGLAttribToInt(attribs);
      dpy = get_platform_display_ext(platform, m_wi.display_connection, attribs ? int_attribs.data() : nullptr);
      if (dpy == EGL_NO_DISPLAY)
      {
        const EGLint err = eglGetError();
        Log_ErrorFmt("eglGetPlatformDisplayEXT() failed: {} (0x{:X})", err, err);
      }
    }
    else
    {
      Log_WarningPrint("eglGetPlatformDisplayEXT() was not found");
    }
  }
  else
  {
    Log_WarningPrint("EGL_EXT_platform_base nor EGL_KHR_platform_base is supported.");
  }

  return dpy;
}

EGLDisplay ContextEGL::GetFallbackDisplay(Error* error)
{
  Log_WarningPrint("Using fallback eglGetDisplay() path.");
  EGLDisplay dpy = eglGetDisplay(m_wi.display_connection);
  if (dpy == EGL_NO_DISPLAY)
  {
    const EGLint err = eglGetError();
    Error::SetStringFmt(error, "eglGetDisplay() failed: {} (0x{:X})", err, err);
  }

  return dpy;
}

EGLSurface ContextEGL::CreatePlatformSurface(EGLConfig config, const EGLAttrib* attribs, Error* error)
{
  EGLSurface surface = EGL_NO_SURFACE;
  if (GLAD_EGL_VERSION_1_5)
  {
    surface = eglCreatePlatformWindowSurface(m_display, config, m_wi.window_handle, attribs);
    if (surface == EGL_NO_SURFACE)
    {
      const EGLint err = eglGetError();
      Error::SetStringFmt(error, "eglCreatePlatformWindowSurface() failed: {} (0x{:X})", err, err);
    }
  }
  if (surface == EGL_NO_SURFACE)
    surface = CreateFallbackSurface(config, attribs, m_wi.window_handle, error);

  return surface;
}

EGLSurface ContextEGL::CreateFallbackSurface(EGLConfig config, const EGLAttrib* attribs, void* win, Error* error)
{
  Log_WarningPrint("Using fallback eglCreateWindowSurface() path.");

  const std::vector<EGLint> int_attribs = EGLAttribToInt(attribs);

  EGLSurface surface =
    eglCreateWindowSurface(m_display, config, (EGLNativeWindowType)win, attribs ? int_attribs.data() : nullptr);
  if (surface == EGL_NO_SURFACE)
  {
    const EGLint err = eglGetError();
    Error::SetStringFmt(error, "eglCreateWindowSurface() failed: {} (0x{:X})", err, err);
  }

  return surface;
}

void* ContextEGL::GetProcAddress(const char* name)
{
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

bool ContextEGL::ChangeSurface(const WindowInfo& new_wi)
{
  const bool was_current = (eglGetCurrentContext() == m_context);
  if (was_current)
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (m_surface != EGL_NO_SURFACE)
  {
    eglDestroySurface(m_display, m_surface);
    m_surface = EGL_NO_SURFACE;
  }

  m_wi = new_wi;
  if (!CreateSurface())
    return false;

  if (was_current && !eglMakeCurrent(m_display, m_surface, m_surface, m_context))
  {
    Log_ErrorPrintf("Failed to make context current again after surface change");
    return false;
  }

  return true;
}

void ContextEGL::ResizeSurface(u32 new_surface_width /*= 0*/, u32 new_surface_height /*= 0*/)
{
  if (new_surface_width == 0 && new_surface_height == 0)
  {
    EGLint surface_width, surface_height;
    if (eglQuerySurface(m_display, m_surface, EGL_WIDTH, &surface_width) &&
        eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &surface_height))
    {
      m_wi.surface_width = static_cast<u32>(surface_width);
      m_wi.surface_height = static_cast<u32>(surface_height);
      return;
    }
    else
    {
      Log_ErrorPrintf("eglQuerySurface() failed: %d", eglGetError());
    }
  }

  m_wi.surface_width = new_surface_width;
  m_wi.surface_height = new_surface_height;
}

bool ContextEGL::SwapBuffers()
{
  return eglSwapBuffers(m_display, m_surface);
}

bool ContextEGL::IsCurrent()
{
  return m_context && eglGetCurrentContext() == m_context;
}

bool ContextEGL::MakeCurrent()
{
  if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context))
  {
    Log_ErrorPrintf("eglMakeCurrent() failed: %d", eglGetError());
    return false;
  }

  return true;
}

bool ContextEGL::DoneCurrent()
{
  return eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

bool ContextEGL::SetSwapInterval(s32 interval)
{
  return eglSwapInterval(m_display, interval);
}

std::unique_ptr<Context> ContextEGL::CreateSharedContext(const WindowInfo& wi, Error* error)
{
  std::unique_ptr<ContextEGL> context = std::make_unique<ContextEGL>(wi);
  context->m_display = m_display;

  if (!context->CreateContextAndSurface(m_version, m_context, false))
  {
    Error::SetStringView(error, "Failed to create context/surface");
    return nullptr;
  }

  return context;
}

bool ContextEGL::CreateSurface()
{
  if (m_wi.type == WindowInfo::Type::Surfaceless)
  {
    if (GLAD_EGL_KHR_surfaceless_context)
      return true;
    else
      return CreatePBufferSurface();
  }

  Error error;
  m_surface = CreatePlatformSurface(m_config, nullptr, &error);
  if (m_surface == EGL_NO_SURFACE)
  {
    Log_ErrorFmt("Failed to create platform surface: {}", error.GetDescription());
    return false;
  }

  // Some implementations may require the size to be queried at runtime.
  EGLint surface_width, surface_height;
  if (eglQuerySurface(m_display, m_surface, EGL_WIDTH, &surface_width) &&
      eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &surface_height))
  {
    m_wi.surface_width = static_cast<u32>(surface_width);
    m_wi.surface_height = static_cast<u32>(surface_height);
  }
  else
  {
    Log_ErrorPrintf("eglQuerySurface() failed: %d", eglGetError());
  }

  m_wi.surface_format = GetSurfaceTextureFormat();

  return true;
}

bool ContextEGL::CreatePBufferSurface()
{
  const u32 width = std::max<u32>(m_wi.surface_width, 1);
  const u32 height = std::max<u32>(m_wi.surface_height, 1);

  // TODO: Format
  EGLint attrib_list[] = {
    EGL_WIDTH, static_cast<EGLint>(width), EGL_HEIGHT, static_cast<EGLint>(height), EGL_NONE,
  };

  m_surface = eglCreatePbufferSurface(m_display, m_config, attrib_list);
  if (!m_surface)
  {
    Log_ErrorPrintf("eglCreatePbufferSurface() failed: %d", eglGetError());
    return false;
  }

  m_wi.surface_format = GetSurfaceTextureFormat();

  Log_DevPrintf("Created %ux%u pbuffer surface", width, height);
  return true;
}

bool ContextEGL::CheckConfigSurfaceFormat(EGLConfig config, GPUTexture::Format format)
{
  int red_size, green_size, blue_size, alpha_size;
  if (!eglGetConfigAttrib(m_display, config, EGL_RED_SIZE, &red_size) ||
      !eglGetConfigAttrib(m_display, config, EGL_GREEN_SIZE, &green_size) ||
      !eglGetConfigAttrib(m_display, config, EGL_BLUE_SIZE, &blue_size) ||
      !eglGetConfigAttrib(m_display, config, EGL_ALPHA_SIZE, &alpha_size))
  {
    return false;
  }

  switch (format)
  {
    case GPUTexture::Format::RGBA8:
      return (red_size == 8 && green_size == 8 && blue_size == 8 && alpha_size == 8);

    case GPUTexture::Format::RGB565:
      return (red_size == 5 && green_size == 6 && blue_size == 5);

    case GPUTexture::Format::RGBA5551:
      return (red_size == 5 && green_size == 5 && blue_size == 5 && alpha_size == 1);

    case GPUTexture::Format::Unknown:
      return true;

    default:
      return false;
  }
}

GPUTexture::Format ContextEGL::GetSurfaceTextureFormat() const
{
  int red_size = 0, green_size = 0, blue_size = 0, alpha_size = 0;
  eglGetConfigAttrib(m_display, m_config, EGL_RED_SIZE, &red_size);
  eglGetConfigAttrib(m_display, m_config, EGL_GREEN_SIZE, &green_size);
  eglGetConfigAttrib(m_display, m_config, EGL_BLUE_SIZE, &blue_size);
  eglGetConfigAttrib(m_display, m_config, EGL_ALPHA_SIZE, &alpha_size);

  if (red_size == 5 && green_size == 6 && red_size == 5)
  {
    return GPUTexture::Format::RGB565;
  }
  else if (red_size == 5 && green_size == 5 && red_size == 5 && alpha_size == 1)
  {
    return GPUTexture::Format::RGBA5551;
  }
  else if (red_size == 8 && green_size == 8 && blue_size == 8 && alpha_size == 8)
  {
    return GPUTexture::Format::RGBA8;
  }
  else
  {
    Log_ErrorPrintf("Unknown surface format: R=%u, G=%u, B=%u, A=%u", red_size, green_size, blue_size, alpha_size);
    return GPUTexture::Format::RGBA8;
  }
}

void ContextEGL::DestroyContext()
{
  if (eglGetCurrentContext() == m_context)
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (m_context != EGL_NO_CONTEXT)
  {
    eglDestroyContext(m_display, m_context);
    m_context = EGL_NO_CONTEXT;
  }
}

void ContextEGL::DestroySurface()
{
  if (eglGetCurrentSurface(EGL_DRAW) == m_surface)
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (m_surface != EGL_NO_SURFACE)
  {
    eglDestroySurface(m_display, m_surface);
    m_surface = EGL_NO_SURFACE;
  }
}

bool ContextEGL::CreateContext(const Version& version, EGLContext share_context)
{
  Log_DevPrintf(
    "Trying version %u.%u (%s)", version.major_version, version.minor_version,
    version.profile == Context::Profile::ES ? "ES" : (version.profile == Context::Profile::Core ? "Core" : "None"));
  int surface_attribs[16] = {
    EGL_RENDERABLE_TYPE,
    (version.profile == Profile::ES) ?
      ((version.major_version >= 3) ? EGL_OPENGL_ES3_BIT :
                                      ((version.major_version == 2) ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES_BIT)) :
      EGL_OPENGL_BIT,
    EGL_SURFACE_TYPE,
    (m_wi.type != WindowInfo::Type::Surfaceless) ? EGL_WINDOW_BIT : 0,
  };
  int nsurface_attribs = 4;

  const GPUTexture::Format format = m_wi.surface_format;
  if (format == GPUTexture::Format::Unknown)
  {
    Log_WarningPrint("Surface format not specified, assuming RGBA8.");
    m_wi.surface_format = GPUTexture::Format::RGBA8;
  }

  switch (m_wi.surface_format)
  {
    case GPUTexture::Format::RGBA8:
      surface_attribs[nsurface_attribs++] = EGL_RED_SIZE;
      surface_attribs[nsurface_attribs++] = 8;
      surface_attribs[nsurface_attribs++] = EGL_GREEN_SIZE;
      surface_attribs[nsurface_attribs++] = 8;
      surface_attribs[nsurface_attribs++] = EGL_BLUE_SIZE;
      surface_attribs[nsurface_attribs++] = 8;
      surface_attribs[nsurface_attribs++] = EGL_ALPHA_SIZE;
      surface_attribs[nsurface_attribs++] = 8;
      break;

    case GPUTexture::Format::RGB565:
      surface_attribs[nsurface_attribs++] = EGL_RED_SIZE;
      surface_attribs[nsurface_attribs++] = 5;
      surface_attribs[nsurface_attribs++] = EGL_GREEN_SIZE;
      surface_attribs[nsurface_attribs++] = 6;
      surface_attribs[nsurface_attribs++] = EGL_BLUE_SIZE;
      surface_attribs[nsurface_attribs++] = 5;
      break;

    case GPUTexture::Format::Unknown:
      break;

    default:
      UnreachableCode();
      break;
  }

  surface_attribs[nsurface_attribs++] = EGL_NONE;
  surface_attribs[nsurface_attribs++] = 0;

  EGLint num_configs;
  if (!eglChooseConfig(m_display, surface_attribs, nullptr, 0, &num_configs) || num_configs == 0)
  {
    Log_ErrorPrintf("eglChooseConfig() failed: %d", eglGetError());
    return false;
  }

  std::vector<EGLConfig> configs(static_cast<u32>(num_configs));
  if (!eglChooseConfig(m_display, surface_attribs, configs.data(), num_configs, &num_configs))
  {
    Log_ErrorPrintf("eglChooseConfig() failed: %d", eglGetError());
    return false;
  }
  configs.resize(static_cast<u32>(num_configs));

  std::optional<EGLConfig> config;
  for (EGLConfig check_config : configs)
  {
    if (CheckConfigSurfaceFormat(check_config, m_wi.surface_format))
    {
      config = check_config;
      break;
    }
  }

  if (!config.has_value())
  {
    Log_WarningPrintf("No EGL configs matched exactly, using first.");
    config = configs.front();
  }

  int attribs[8];
  int nattribs = 0;
  if (version.profile != Profile::NoProfile)
  {
    attribs[nattribs++] = EGL_CONTEXT_MAJOR_VERSION;
    attribs[nattribs++] = version.major_version;
    attribs[nattribs++] = EGL_CONTEXT_MINOR_VERSION;
    attribs[nattribs++] = version.minor_version;
  }
  attribs[nattribs++] = EGL_NONE;
  attribs[nattribs++] = 0;

  if (!eglBindAPI((version.profile == Profile::ES) ? EGL_OPENGL_ES_API : EGL_OPENGL_API))
  {
    Log_ErrorPrintf("eglBindAPI(%s) failed", (version.profile == Profile::ES) ? "EGL_OPENGL_ES_API" : "EGL_OPENGL_API");
    return false;
  }

  m_context = eglCreateContext(m_display, config.value(), share_context, attribs);
  if (!m_context)
  {
    Log_ErrorPrintf("eglCreateContext() failed: %d", eglGetError());
    return false;
  }

  Log_InfoPrintf(
    "Got version %u.%u (%s)", version.major_version, version.minor_version,
    version.profile == Context::Profile::ES ? "ES" : (version.profile == Context::Profile::Core ? "Core" : "None"));

  m_config = config.value();
  m_version = version;
  return true;
}

bool ContextEGL::CreateContextAndSurface(const Version& version, EGLContext share_context, bool make_current)
{
  if (!CreateContext(version, share_context))
    return false;

  if (!CreateSurface())
  {
    Log_ErrorPrintf("Failed to create surface for context");
    eglDestroyContext(m_display, m_context);
    m_context = EGL_NO_CONTEXT;
    return false;
  }

  if (make_current && !eglMakeCurrent(m_display, m_surface, m_surface, m_context))
  {
    Log_ErrorPrintf("eglMakeCurrent() failed: %d", eglGetError());
    if (m_surface != EGL_NO_SURFACE)
    {
      eglDestroySurface(m_display, m_surface);
      m_surface = EGL_NO_SURFACE;
    }
    eglDestroyContext(m_display, m_context);
    m_context = EGL_NO_CONTEXT;
    return false;
  }

  return true;
}
} // namespace GL
