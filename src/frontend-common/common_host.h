#pragma once
#include "core/system.h"
#include <memory>
#include <mutex>

class SettingsInterface;

class AudioStream;
enum class AudioStretchMode : u8;

namespace CommonHost {
/// Initializes configuration.
void UpdateLogSettings();

void Initialize();
void Shutdown();

void SetDefaultSettings(SettingsInterface& si);
void SetDefaultControllerSettings(SettingsInterface& si);
void SetDefaultHotkeyBindings(SettingsInterface& si);
void LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock);
void CheckForSettingsChanges(const Settings& old_settings);
void OnSystemStarting();
void OnSystemStarted();
void OnSystemDestroyed();
void OnSystemPaused();
void OnSystemResumed();
void OnGameChanged(const std::string& disc_path, const std::string& game_serial, const std::string& game_name);
void PumpMessagesOnCPUThread();
bool CreateHostDisplayResources();
void ReleaseHostDisplayResources();

#ifdef _WIN32
std::unique_ptr<AudioStream> CreateXAudio2Stream(u32 sample_rate, u32 channels, u32 buffer_ms, u32 latency_ms,
                                                 AudioStretchMode stretch);
#endif
#ifdef WITH_SDL2
std::unique_ptr<AudioStream> CreateSDLAudioStream(u32 sample_rate, u32 channels, u32 buffer_ms, u32 latency_ms,
                                                  AudioStretchMode stretch);
#endif
#ifndef _UWP
std::unique_ptr<AudioStream> CreateCubebAudioStream(u32 sample_rate, u32 channels, u32 buffer_ms, u32 latency_ms,
                                                    AudioStretchMode stretch);
#endif
} // namespace CommonHost

namespace ImGuiManager {
void RenderDebugWindows();
}

namespace Host {
/// Return the current window handle. Needed for DInput.
void* GetTopLevelWindowHandle();
} // namespace Host