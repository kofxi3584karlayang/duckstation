#pragma once
#include "gpu_hw.h"
#include "shadergen.h"

class GPU_HW_ShaderGen : public ShaderGen
{
public:
  GPU_HW_ShaderGen(HostDisplay::RenderAPI render_api, u32 resolution_scale, bool true_color, bool scaled_dithering,
                   GPUTextureFilter texture_filtering, bool uv_limits, bool supports_dual_source_blend);
  ~GPU_HW_ShaderGen();

  std::string GenerateBatchVertexShader(bool textured);
  std::string GenerateBatchFragmentShader(GPU_HW::BatchRenderMode transparency, GPU::TextureMode texture_mode,
                                          bool dithering, bool interlacing);
  std::string GenerateInterlacedFillFragmentShader();
  std::string GenerateDisplayFragmentShader(bool depth_24bit, GPU_HW::InterlacedRenderMode interlace_mode);
  std::string GenerateVRAMReadFragmentShader();
  std::string GenerateVRAMWriteFragmentShader(bool use_ssbo);
  std::string GenerateVRAMCopyFragmentShader();
  std::string GenerateVRAMUpdateDepthFragmentShader();

private:
  void WriteCommonFunctions(std::stringstream& ss);
  void WriteBatchUniformBuffer(std::stringstream& ss);
  void WriteBatchTextureFilter(std::stringstream& ss, GPUTextureFilter texture_filter);

  u32 m_resolution_scale;
  bool m_true_color;
  bool m_scaled_dithering;
  GPUTextureFilter m_texture_filter;
  bool m_uv_limits;
};
