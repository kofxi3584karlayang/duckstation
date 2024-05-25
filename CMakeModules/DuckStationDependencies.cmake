# From PCSX2: On macOS, Mono.framework contains an ancient version of libpng. We don't want that.
# Avoid it by telling cmake to avoid finding frameworks while we search for libpng.
if(APPLE)
  set(FIND_FRAMEWORK_BACKUP ${CMAKE_FIND_FRAMEWORK})
  set(CMAKE_FIND_FRAMEWORK NEVER)
endif()

# Enable threads everywhere.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(SDL2 2.30.3 REQUIRED)
find_package(Zstd 1.5.5 REQUIRED)
find_package(WebP REQUIRED) # v1.3.2, spews an error on Linux because no pkg-config.
find_package(ZLIB REQUIRED) # 1.3, but Mac currently doesn't use it.
find_package(PNG 1.6.40 REQUIRED)
find_package(JPEG REQUIRED) # No version because flatpak uses libjpeg-turbo.
find_package(Freetype 2.11.1 REQUIRED)

if(NOT WIN32)
  find_package(CURL REQUIRED)
endif()

if(ENABLE_X11)
  find_package(X11 REQUIRED)
  if (NOT X11_Xrandr_FOUND)
    message(FATAL_ERROR "XRandR extension is required")
  endif()
endif()

if(ENABLE_WAYLAND)
  find_package(ECM REQUIRED NO_MODULE)
  list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
  find_package(Wayland REQUIRED Egl)
endif()

if(ENABLE_VULKAN OR APPLE)
  find_package(Shaderc REQUIRED)

  if(LINUX AND ENABLE_VULKAN)
    # We need to add the rpath for shaderc to the executable.
    get_filename_component(SHADERC_LIBRARY_DIRECTORY ${SHADERC_LIBRARY} DIRECTORY)
    list(APPEND CMAKE_BUILD_RPATH ${SHADERC_LIBRARY_DIRECTORY})
  endif()
endif()

if(APPLE)
  # SPIRV-Cross is currently only used on MacOS.
  find_package(spirv_cross_c_shared REQUIRED)
endif()

if(LINUX)
  find_package(UDEV REQUIRED)
endif()

if(NOT WIN32 AND NOT APPLE)
  find_package(Libbacktrace REQUIRED)
endif()

if(APPLE)
  set(CMAKE_FIND_FRAMEWORK ${FIND_FRAMEWORK_BACKUP})
endif()
