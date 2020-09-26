#pragma once
#include "core/types.h"
#include <bitset>
#include <optional>
#include <string>
#include <unordered_map>

class ByteStream;

namespace GameSettings {
enum class Trait : u32
{
  ForceInterpreter,
  ForceSoftwareRenderer,
  ForceInterlacing,
  DisableTrueColor,
  DisableUpscaling,
  DisableScaledDithering,
  DisableForceNTSCTimings,
  DisableWidescreen,
  DisablePGXP,
  DisablePGXPCulling,
  DisablePGXPTextureCorrection,
  ForcePGXPVertexCache,
  ForcePGXPCPUMode,
  ForceDigitalController,
  ForceRecompilerMemoryExceptions,
  ForceRecompilerICache,

  Count
};

const char* GetTraitName(Trait trait);
const char* GetTraitDisplayName(Trait trait);

struct Entry
{
  std::bitset<static_cast<int>(Trait::Count)> traits{};
  std::optional<s16> display_active_start_offset;
  std::optional<s16> display_active_end_offset;

  // user settings
  std::optional<DisplayCropMode> display_crop_mode;
  std::optional<DisplayAspectRatio> display_aspect_ratio;
  std::optional<bool> display_linear_upscaling;
  std::optional<bool> display_integer_upscaling;
  std::optional<bool> display_force_4_3_for_24bit;
  std::optional<u32> gpu_resolution_scale;
  std::optional<bool> gpu_true_color;
  std::optional<bool> gpu_scaled_dithering;
  std::optional<bool> gpu_force_ntsc_timings;
  std::optional<GPUTextureFilter> gpu_texture_filter;
  std::optional<bool> gpu_widescreen_hack;
  std::optional<bool> gpu_pgxp;
  std::optional<ControllerType> controller_1_type;
  std::optional<ControllerType> controller_2_type;
  std::optional<MemoryCardType> memory_card_1_type;
  std::optional<MemoryCardType> memory_card_2_type;
  std::string memory_card_1_shared_path;
  std::string memory_card_2_shared_path;

  ALWAYS_INLINE bool HasTrait(Trait trait) const { return traits[static_cast<int>(trait)]; }
  ALWAYS_INLINE void AddTrait(Trait trait) { traits[static_cast<int>(trait)] = true; }
  ALWAYS_INLINE void RemoveTrait(Trait trait) { traits[static_cast<int>(trait)] = false; }
  ALWAYS_INLINE void SetTrait(Trait trait, bool enabled) { traits[static_cast<int>(trait)] = enabled; }

  bool LoadFromStream(ByteStream* stream);
  bool SaveToStream(ByteStream* stream) const;

  void ApplySettings(bool display_osd_messages) const;
};

class Database
{
public:
  Database();
  ~Database();

  const Entry* GetEntry(const std::string& code) const;
  void SetEntry(const std::string& code, const std::string& name, const Entry& entry, const char* save_path);

  bool Load(const char* path);

private:
  std::unordered_map<std::string, Entry> m_entries;
};

}; // namespace GameSettings