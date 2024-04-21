// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once

#include "common/types.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // warning C4324: structure was padded due to alignment specifier
#endif

class Error;
class SettingsInterface;

class FreeSurroundDecoder;
namespace soundtouch {
class SoundTouch;
}

enum class AudioBackend : u8
{
  Null,
#ifndef __ANDROID__
  Cubeb,
  SDL,
#else
  AAudio,
  OpenSLES,
#endif
#ifdef _WIN32
  XAudio2,
#endif
  Count
};

enum class AudioStretchMode : u8
{
  Off,
  Resample,
  TimeStretch,
  Count
};

enum class AudioExpansionMode : u8
{
  Disabled,
  StereoLFE,
  Quadraphonic,
  QuadraphonicLFE,
  Surround51,
  Surround71,
  Count
};

struct AudioStreamParameters
{
  AudioStretchMode stretch_mode = DEFAULT_STRETCH_MODE;
  AudioExpansionMode expansion_mode = DEFAULT_EXPANSION_MODE;
  u16 buffer_ms = DEFAULT_BUFFER_MS;
  u16 output_latency_ms = DEFAULT_OUTPUT_LATENCY_MS;

  u16 stretch_sequence_length_ms = DEFAULT_STRETCH_SEQUENCE_LENGTH;
  u16 stretch_seekwindow_ms = DEFAULT_STRETCH_SEEKWINDOW;
  u16 stretch_overlap_ms = DEFAULT_STRETCH_OVERLAP;
  bool stretch_use_quickseek = DEFAULT_STRETCH_USE_QUICKSEEK;
  bool stretch_use_aa_filter = DEFAULT_STRETCH_USE_AA_FILTER;

  float expand_circular_wrap = DEFAULT_EXPAND_CIRCULAR_WRAP;
  float expand_shift = DEFAULT_EXPAND_SHIFT;
  float expand_depth = DEFAULT_EXPAND_DEPTH;
  float expand_focus = DEFAULT_EXPAND_FOCUS;
  float expand_center_image = DEFAULT_EXPAND_CENTER_IMAGE;
  float expand_front_separation = DEFAULT_EXPAND_FRONT_SEPARATION;
  float expand_rear_separation = DEFAULT_EXPAND_REAR_SEPARATION;
  u16 expand_block_size = DEFAULT_EXPAND_BLOCK_SIZE;
  u8 expand_low_cutoff = DEFAULT_EXPAND_LOW_CUTOFF;
  u8 expand_high_cutoff = DEFAULT_EXPAND_HIGH_CUTOFF;

  static constexpr AudioStretchMode DEFAULT_STRETCH_MODE = AudioStretchMode::TimeStretch;
  static constexpr AudioExpansionMode DEFAULT_EXPANSION_MODE = AudioExpansionMode::Disabled;
#ifndef __ANDROID__
  static constexpr u16 DEFAULT_BUFFER_MS = 50;
  static constexpr u16 DEFAULT_OUTPUT_LATENCY_MS = 20;
#else
  static constexpr u16 DEFAULT_BUFFER_MS = 100;
  static constexpr u16 DEFAULT_OUTPUT_LATENCY_MS = 20;
#endif
  static constexpr u16 DEFAULT_EXPAND_BLOCK_SIZE = 1024;
  static constexpr float DEFAULT_EXPAND_CIRCULAR_WRAP = 90.0f;
  static constexpr float DEFAULT_EXPAND_SHIFT = 0.0f;
  static constexpr float DEFAULT_EXPAND_DEPTH = 1.0f;
  static constexpr float DEFAULT_EXPAND_FOCUS = 0.0f;
  static constexpr float DEFAULT_EXPAND_CENTER_IMAGE = 1.0f;
  static constexpr float DEFAULT_EXPAND_FRONT_SEPARATION = 1.0f;
  static constexpr float DEFAULT_EXPAND_REAR_SEPARATION = 1.0f;
  static constexpr u8 DEFAULT_EXPAND_LOW_CUTOFF = 40;
  static constexpr u8 DEFAULT_EXPAND_HIGH_CUTOFF = 90;

  static constexpr u16 DEFAULT_STRETCH_SEQUENCE_LENGTH = 30;
  static constexpr u16 DEFAULT_STRETCH_SEEKWINDOW = 20;
  static constexpr u16 DEFAULT_STRETCH_OVERLAP = 10;

  static constexpr bool DEFAULT_STRETCH_USE_QUICKSEEK = false;
  static constexpr bool DEFAULT_STRETCH_USE_AA_FILTER = false;

  void Load(SettingsInterface& si, const char* section);
  void Save(SettingsInterface& si, const char* section) const;
  void Clear(SettingsInterface& si, const char* section);

  bool operator==(const AudioStreamParameters& rhs) const;
  bool operator!=(const AudioStreamParameters& rhs) const;
};

class AudioStream
{
public:
  using SampleType = s16;

  static constexpr u32 NUM_INPUT_CHANNELS = 2;
  static constexpr u32 MAX_OUTPUT_CHANNELS = 8;
  static constexpr u32 CHUNK_SIZE = 64;
  static constexpr u32 MIN_EXPANSION_BLOCK_SIZE = 256;
  static constexpr u32 MAX_EXPANSION_BLOCK_SIZE = 4096;

#ifndef __ANDROID__
  static constexpr AudioBackend DEFAULT_BACKEND = AudioBackend::Cubeb;
#else
  static constexpr AudioBackend DEFAULT_BACKEND = AudioBackend::AAudio;
#endif

public:
  virtual ~AudioStream();

  static u32 GetAlignedBufferSize(u32 size);
  static u32 GetBufferSizeForMS(u32 sample_rate, u32 ms);
  static u32 GetMSForBufferSize(u32 sample_rate, u32 buffer_size);

  static std::optional<AudioBackend> ParseBackendName(const char* str);
  static const char* GetBackendName(AudioBackend backend);
  static const char* GetBackendDisplayName(AudioBackend backend);

  static const char* GetExpansionModeName(AudioExpansionMode mode);
  static const char* GetExpansionModeDisplayName(AudioExpansionMode mode);
  static std::optional<AudioExpansionMode> ParseExpansionMode(const char* name);

  static const char* GetStretchModeName(AudioStretchMode mode);
  static const char* GetStretchModeDisplayName(AudioStretchMode mode);
  static std::optional<AudioStretchMode> ParseStretchMode(const char* name);

  ALWAYS_INLINE u32 GetSampleRate() const { return m_sample_rate; }
  ALWAYS_INLINE u32 GetInternalChannels() const { return m_internal_channels; }
  ALWAYS_INLINE u32 GetOutputChannels() const { return m_internal_channels; }
  ALWAYS_INLINE u32 GetBufferSize() const { return m_buffer_size; }
  ALWAYS_INLINE u32 GetTargetBufferSize() const { return m_target_buffer_size; }
  ALWAYS_INLINE u32 GetOutputVolume() const { return m_volume; }
  ALWAYS_INLINE float GetNominalTempo() const { return m_nominal_rate; }
  ALWAYS_INLINE bool IsPaused() const { return m_paused; }

  u32 GetBufferedFramesRelaxed() const;

  /// Temporarily pauses the stream, preventing it from requesting data.
  virtual void SetPaused(bool paused);

  virtual void SetOutputVolume(u32 volume);

  void BeginWrite(SampleType** buffer_ptr, u32* num_frames);
  void WriteFrames(const SampleType* frames, u32 num_frames);
  void EndWrite(u32 num_frames);

  void EmptyBuffer();

  /// Nominal rate is used for both resampling and timestretching, input samples are assumed to be this amount faster
  /// than the sample rate.
  void SetNominalRate(float tempo);
  void UpdateTargetTempo(float tempo);

  void SetStretchMode(AudioStretchMode mode);

  static std::unique_ptr<AudioStream> CreateStream(AudioBackend backend, u32 sample_rate,
                                                   const AudioStreamParameters& parameters, Error* error = nullptr);
  static std::unique_ptr<AudioStream> CreateNullStream(u32 sample_rate, u32 buffer_ms);

#ifndef __ANDROID__
  static std::vector<std::string> GetCubebDriverNames();
  static std::vector<std::pair<std::string, std::string>> GetCubebOutputDevices(const char* driver);
#endif

protected:
  enum ReadChannel : u8
  {
    READ_CHANNEL_FRONT_LEFT,
    READ_CHANNEL_FRONT_CENTER,
    READ_CHANNEL_FRONT_RIGHT,
    READ_CHANNEL_SIDE_LEFT,
    READ_CHANNEL_SIDE_RIGHT,
    READ_CHANNEL_REAR_LEFT,
    READ_CHANNEL_REAR_RIGHT,
    READ_CHANNEL_LFE,
    READ_CHANNEL_NONE
  };

  using SampleReader = void (*)(SampleType* dest, const SampleType* src, u32 num_frames);

  AudioStream(u32 sample_rate, const AudioStreamParameters& parameters);
  void BaseInitialize(SampleReader sample_reader);

  void ReadFrames(SampleType* samples, u32 num_frames);

  template<AudioExpansionMode mode, ReadChannel c0 = READ_CHANNEL_NONE, ReadChannel c1 = READ_CHANNEL_NONE,
           ReadChannel c2 = READ_CHANNEL_NONE, ReadChannel c3 = READ_CHANNEL_NONE, ReadChannel c4 = READ_CHANNEL_NONE,
           ReadChannel c5 = READ_CHANNEL_NONE, ReadChannel c6 = READ_CHANNEL_NONE, ReadChannel c7 = READ_CHANNEL_NONE>
  static void SampleReaderImpl(SampleType* dest, const SampleType* src, u32 num_frames);
  static void StereoSampleReaderImpl(SampleType* dest, const SampleType* src, u32 num_frames);

  void ApplyVolume(SampleType* samples, u32 num_samples);

  u32 m_sample_rate = 0;
  u32 m_volume = 0;
  AudioStreamParameters m_parameters;
  u8 m_internal_channels = 0;
  u8 m_output_channels = 0;
  bool m_stretch_inactive = false;
  bool m_filling = false;
  bool m_paused = false;

private:
  static constexpr u32 AVERAGING_BUFFER_SIZE = 256;
  static constexpr u32 AVERAGING_WINDOW = 50;
  static constexpr u32 STRETCH_RESET_THRESHOLD = 5;
  static constexpr u32 TARGET_IPS = 691;

  ALWAYS_INLINE bool IsExpansionEnabled() const { return m_parameters.expansion_mode != AudioExpansionMode::Disabled; }
  ALWAYS_INLINE bool IsStretchEnabled() const { return m_parameters.stretch_mode != AudioStretchMode::Off; }

  void AllocateBuffer();
  void DestroyBuffer();

  void InternalWriteFrames(SampleType* samples, u32 num_frames);

#ifndef __ANDROID__
  void ExpandAllocate();
#endif

  void StretchAllocate();
  void StretchDestroy();
  void StretchWriteBlock(const float* block);
  void StretchUnderrun();
  void StretchOverrun();

  float AddAndGetAverageTempo(float val);
  void UpdateStretchTempo();

  u32 m_buffer_size = 0;
  std::unique_ptr<s16[]> m_buffer;
  SampleReader m_sample_reader = nullptr;

  std::atomic<u32> m_rpos{0};
  std::atomic<u32> m_wpos{0};

  std::unique_ptr<soundtouch::SoundTouch> m_soundtouch;

  u32 m_target_buffer_size = 0;
  u32 m_stretch_reset = STRETCH_RESET_THRESHOLD;

  u32 m_stretch_ok_count = 0;
  float m_nominal_rate = 1.0f;
  float m_dynamic_target_usage = 0.0f;

  u32 m_average_position = 0;
  u32 m_average_available = 0;
  u32 m_staging_buffer_pos = 0;

  std::array<float, AVERAGING_BUFFER_SIZE> m_average_fullness = {};

  // temporary staging buffer, used for timestretching
  std::unique_ptr<s16[]> m_staging_buffer;

  // float buffer, soundtouch only accepts float samples as input
  std::unique_ptr<float[]> m_float_buffer;

#ifndef __ANDROID__
  std::unique_ptr<FreeSurroundDecoder> m_expander;

  // block buffer for expansion
  std::unique_ptr<float[]> m_expand_buffer;
  float* m_expand_output_buffer = nullptr;
  u32 m_expand_buffer_pos = 0;
#endif

#ifndef __ANDROID__
  static std::unique_ptr<AudioStream> CreateCubebAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
                                                             Error* error);
  static std::unique_ptr<AudioStream> CreateSDLAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
                                                           Error* error);
#endif
#ifdef _WIN32
  static std::unique_ptr<AudioStream> CreateXAudio2Stream(u32 sample_rate, const AudioStreamParameters& parameters,
                                                          Error* error);
#endif
};

template<AudioExpansionMode mode, AudioStream::ReadChannel c0, AudioStream::ReadChannel c1, AudioStream::ReadChannel c2,
         AudioStream::ReadChannel c3, AudioStream::ReadChannel c4, AudioStream::ReadChannel c5,
         AudioStream::ReadChannel c6, AudioStream::ReadChannel c7>
void AudioStream::SampleReaderImpl(SampleType* dest, const SampleType* src, u32 num_frames)
{
  static_assert(READ_CHANNEL_NONE == MAX_OUTPUT_CHANNELS);
  static constexpr const std::array<std::pair<std::array<s8, MAX_OUTPUT_CHANNELS>, u8>,
                                    static_cast<size_t>(AudioExpansionMode::Count)>
    luts = {{
      // FL FC FR SL SR RL RR LFE
      {{0, -1, 1, -1, -1, -1, -1, -1}, 2}, // Disabled
      {{0, -1, 1, -1, -1, -1, -1, 2}, 3},  // StereoLFE
      {{0, -1, 1, -1, -1, 2, 3, -1}, 5},   // Quadraphonic
      {{0, -1, 2, -1, -1, 2, 3, 4}, 5},    // QuadraphonicLFE
      {{0, 1, 2, -1, -1, 3, 4, 5}, 6},     // Surround51
      {{0, 1, 2, 3, 4, 5, 6, 7}, 8},       // Surround71
    }};
  constexpr const auto& lut = luts[static_cast<size_t>(mode)].first;
  for (u32 i = 0; i < num_frames; i++)
  {
    if constexpr (c0 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c0] >= 0 && lut[c0] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c0]];
    }
    if constexpr (c1 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c1] >= 0 && lut[c1] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c1]];
    }
    if constexpr (c2 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c2] >= 0 && lut[c2] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c2]];
    }
    if constexpr (c3 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c3] >= 0 && lut[c3] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c3]];
    }
    if constexpr (c4 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c4] >= 0 && lut[c4] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c4]];
    }
    if constexpr (c5 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c5] >= 0 && lut[c5] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c5]];
    }
    if constexpr (c6 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c6] >= 0 && lut[c6] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c6]];
    }
    if constexpr (c7 != READ_CHANNEL_NONE)
    {
      static_assert(lut[c7] >= 0 && lut[c7] < MAX_OUTPUT_CHANNELS);
      *(dest++) = src[lut[c7]];
    }

    src += luts[static_cast<size_t>(mode)].second;
  }
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
