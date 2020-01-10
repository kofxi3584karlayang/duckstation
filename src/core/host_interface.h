#pragma once
#include "common/timer.h"
#include "settings.h"
#include "types.h"
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class AudioStream;
class HostDisplay;

class System;

class HostInterface
{
public:
  HostInterface();
  virtual ~HostInterface();

  /// Access to host display.
  ALWAYS_INLINE HostDisplay* GetDisplay() const { return m_display.get(); }

  /// Access to host audio stream.
  AudioStream* GetAudioStream() const { return m_audio_stream.get(); }

  /// Returns a settings object which can be modified.
  Settings& GetSettings() { return m_settings; }

  /// Adjusts the throttle frequency, i.e. how many times we should sleep per second.
  void SetThrottleFrequency(double frequency) { m_throttle_period = static_cast<s64>(1000000000.0 / frequency); }

  bool CreateSystem();
  bool BootSystem(const char* filename, const char* state_filename);
  void ResetSystem();
  void DestroySystem();

  virtual void ReportError(const char* message);
  virtual void ReportMessage(const char* message);

  void ReportFormattedError(const char* format, ...);
  void ReportFormattedMessage(const char* format, ...);

  /// Adds OSD messages, duration is in seconds.
  void AddOSDMessage(const char* message, float duration = 2.0f);
  void AddFormattedOSDMessage(float duration, const char* format, ...);

  /// Loads the BIOS image for the specified region.
  virtual std::optional<std::vector<u8>> GetBIOSImage(ConsoleRegion region);

  bool LoadState(const char* filename);
  bool SaveState(const char* filename);

protected:
  using ThrottleClock = std::chrono::steady_clock;

  enum : u32
  {
    AUDIO_SAMPLE_RATE = 44100,
    AUDIO_CHANNELS = 2,
    AUDIO_BUFFER_SIZE = 2048,
    AUDIO_BUFFERS = 2
  };

  struct OSDMessage
  {
    std::string text;
    Common::Timer time;
    float duration;
  };

  /// Throttles the system, i.e. sleeps until it's time to execute the next frame.
  void Throttle();

  void UpdateSpeedLimiterState();

  void DrawFPSWindow();
  void DrawOSDMessages();
  void DrawDebugWindows();
  void ClearImGuiFocus();

  void UpdatePerformanceCounters();
  void ResetPerformanceCounters();

  std::unique_ptr<HostDisplay> m_display;
  std::unique_ptr<AudioStream> m_audio_stream;
  std::unique_ptr<System> m_system;
  Settings m_settings;

  u64 m_last_throttle_time = 0;
  s64 m_throttle_period = INT64_C(1000000000) / 60;
  Common::Timer m_throttle_timer;
  Common::Timer m_speed_lost_time_timestamp;

  float m_vps = 0.0f;
  float m_fps = 0.0f;
  float m_speed = 0.0f;
  u32 m_last_frame_number = 0;
  u32 m_last_internal_frame_number = 0;
  u32 m_last_global_tick_counter = 0;
  Common::Timer m_fps_timer;

  std::deque<OSDMessage> m_osd_messages;
  std::mutex m_osd_messages_lock;

  bool m_paused = false;
  bool m_speed_limiter_temp_disabled = false;
  bool m_speed_limiter_enabled = false;
};
