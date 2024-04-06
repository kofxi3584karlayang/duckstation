// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once

#include "gpu.h"
#include "texture_replacements.h"

#include "util/gpu_device.h"

#include "common/dimensional_array.h"
#include "common/heap_array.h"

#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class GPU_SW_Backend;
struct GPUBackendCommand;
struct GPUBackendDrawCommand;

class GPU_HW final : public GPU
{
public:
  enum class BatchRenderMode : u8
  {
    TransparencyDisabled,
    TransparentAndOpaque,
    OnlyOpaque,
    OnlyTransparent,
    ShaderBlend
  };

  GPU_HW();
  ~GPU_HW() override;

  const Threading::Thread* GetSWThread() const override;
  bool IsHardwareRenderer() const override;

  bool Initialize() override;
  void Reset(bool clear_vram) override;
  bool DoState(StateWrapper& sw, GPUTexture** host_texture, bool update_display) override;

  void RestoreDeviceContext() override;

  void UpdateSettings(const Settings& old_settings) override;
  void UpdateResolutionScale() override final;
  std::tuple<u32, u32> GetEffectiveDisplayResolution(bool scaled = true) override final;
  std::tuple<u32, u32> GetFullDisplayResolution(bool scaled = true) override final;

  void UpdateDisplay() override;

private:
  enum : u32
  {
    MAX_BATCH_VERTEX_COUNTER_IDS = 65536 - 2,
    MAX_VERTICES_FOR_RECTANGLE = 6 * (((MAX_PRIMITIVE_WIDTH + (TEXTURE_PAGE_WIDTH - 1)) / TEXTURE_PAGE_WIDTH) + 1u) *
                                 (((MAX_PRIMITIVE_HEIGHT + (TEXTURE_PAGE_HEIGHT - 1)) / TEXTURE_PAGE_HEIGHT) + 1u)
  };
  enum : u8
  {
    TEXPAGE_DIRTY_DRAWN_RECT = (1 << 0),
    TEXPAGE_DIRTY_WRITTEN_RECT = (1 << 1),
  };

  static_assert(GPUDevice::MIN_TEXEL_BUFFER_ELEMENTS >= (VRAM_WIDTH * VRAM_HEIGHT));

  struct BatchVertex
  {
    float x;
    float y;
    float z;
    float w;
    u32 color;
    u32 texpage;
    u16 u; // 16-bit texcoords are needed for 256 extent rectangles
    u16 v;
    u32 uv_limits;

    void Set(float x_, float y_, float z_, float w_, u32 color_, u32 texpage_, u16 packed_texcoord, u32 uv_limits_);
    void Set(float x_, float y_, float z_, float w_, u32 color_, u32 texpage_, u16 u_, u16 v_, u32 uv_limits_);
    static u32 PackUVLimits(u32 min_u, u32 max_u, u32 min_v, u32 max_v);
    void SetUVLimits(u32 min_u, u32 max_u, u32 min_v, u32 max_v);
  };

  struct BatchConfig
  {
    GPUTextureMode texture_mode = GPUTextureMode::Disabled;
    GPUTransparencyMode transparency_mode = GPUTransparencyMode::Disabled;
    bool dithering = false;
    bool interlacing = false;
    bool set_mask_while_drawing = false;
    bool check_mask_before_draw = false;
    bool use_depth_buffer = false;

    // Returns the render mode for this batch.
    BatchRenderMode GetRenderMode() const;
  };

  struct BatchUBOData
  {
    u32 u_texture_window_and[2];
    u32 u_texture_window_or[2];
    float u_src_alpha_factor;
    float u_dst_alpha_factor;
    u32 u_interlaced_displayed_field;
    u32 u_set_mask_while_drawing;
  };

  struct RendererStats
  {
    u32 num_batches;
    u32 num_vram_read_texture_updates;
    u32 num_uniform_buffer_updates;
  };

  /// Returns true if a depth buffer should be created.
  bool NeedsDepthBuffer() const;

  bool CreateBuffers();
  void ClearFramebuffer();
  void DestroyBuffers();

  bool CompilePipelines();
  void DestroyPipelines();

  void LoadVertices();

  void PrintSettingsToLog();
  void CheckSettings();

  void UpdateVRAMReadTexture(bool drawn, bool written);
  void UpdateDepthBufferFromMaskBit();
  void ClearDepthBuffer();
  void SetScissor();
  void SetVRAMRenderTarget();
  void MapGPUBuffer(u32 required_vertices, u32 required_indices);
  void UnmapGPUBuffer(u32 used_vertices, u32 used_indices);
  void DrawBatchVertices(BatchRenderMode render_mode, u32 num_indices, u32 base_index, u32 base_vertex);

  u32 CalculateResolutionScale() const;
  GPUDownsampleMode GetDownsampleMode(u32 resolution_scale) const;

  bool IsUsingMultisampling() const;
  bool IsUsingDownsampling() const;

  void SetFullVRAMDirtyRectangle();
  void ClearVRAMDirtyRectangle();
  void IncludeVRAMDirtyRectangle(Common::Rectangle<u32>& rect, const Common::Rectangle<u32>& new_rect);
  void CheckForTexPageOverlap(u32 texpage, u32 min_u, u32 min_v, u32 max_u, u32 max_v);

  bool IsFlushed() const;
  void EnsureVertexBufferSpace(u32 required_vertices, u32 required_indices);
  void EnsureVertexBufferSpaceForCurrentCommand();
  void ResetBatchVertexDepth();

  /// Returns the value to be written to the depth buffer for the current operation for mask bit emulation.
  float GetCurrentNormalizedVertexDepth() const;

  /// Returns if the draw needs to be broken into opaque/transparent passes.
  bool NeedsTwoPassRendering() const;

  /// Returns true if the draw is going to use shader blending/framebuffer fetch.
  bool NeedsShaderBlending(GPUTransparencyMode transparency, bool check_mask) const;

  void FillBackendCommandParameters(GPUBackendCommand* cmd) const;
  void FillDrawCommand(GPUBackendDrawCommand* cmd, GPURenderCommand rc) const;
  void UpdateSoftwareRenderer(bool copy_vram_from_hw);

  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color) override;
  void ReadVRAM(u32 x, u32 y, u32 width, u32 height) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height) override;
  void DispatchRenderCommand() override;
  void FlushRender() override;
  void DrawRendererStats() override;

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, u32 dst_x, u32 dst_y, u32 width, u32 height);

  /// Expands a line into two triangles.
  void DrawLine(float x0, float y0, u32 col0, float x1, float y1, u32 col1, float depth);

  /// Handles quads with flipped texture coordinate directions.
  void HandleFlippedQuadTextureCoordinates(BatchVertex* vertices);
  void ExpandLineTriangles(BatchVertex* vertices, u32 base_vertex);

  /// Computes polygon U/V boundaries.
  void ComputePolygonUVLimits(u32 texpage, BatchVertex* vertices, u32 num_vertices);

  /// Sets the depth test flag for PGXP depth buffering.
  void SetBatchDepthBuffer(bool enabled);
  void CheckForDepthClear(const BatchVertex* vertices, u32 num_vertices);

  /// Returns the number of mipmap levels used for adaptive smoothing.
  u32 GetAdaptiveDownsamplingMipLevels() const;

  void DownsampleFramebuffer(GPUTexture* source, u32 left, u32 top, u32 width, u32 height);
  void DownsampleFramebufferAdaptive(GPUTexture* source, u32 left, u32 top, u32 width, u32 height);
  void DownsampleFramebufferBoxFilter(GPUTexture* source, u32 left, u32 top, u32 width, u32 height);

  std::unique_ptr<GPUTexture> m_vram_texture;
  std::unique_ptr<GPUTexture> m_vram_depth_texture;
  std::unique_ptr<GPUTexture> m_vram_read_texture;
  std::unique_ptr<GPUTexture> m_vram_readback_texture;
  std::unique_ptr<GPUDownloadTexture> m_vram_readback_download_texture;
  std::unique_ptr<GPUTexture> m_vram_replacement_texture;

  std::unique_ptr<GPUTextureBuffer> m_vram_upload_buffer;
  std::unique_ptr<GPUTexture> m_vram_write_texture;

  std::unique_ptr<GPU_SW_Backend> m_sw_renderer;

  BatchVertex* m_batch_vertex_ptr = nullptr;
  u16* m_batch_index_ptr = nullptr;
  u32 m_batch_base_vertex = 0;
  u32 m_batch_base_index = 0;
  u16 m_batch_vertex_count = 0;
  u16 m_batch_index_count = 0;
  u16 m_batch_vertex_space = 0;
  u16 m_batch_index_space = 0;
  s32 m_current_depth = 0;
  float m_last_depth_z = 1.0f;

  u8 m_resolution_scale = 1;
  u8 m_multisamples = 1;

  bool m_supports_dual_source_blend : 1 = false;
  bool m_supports_framebuffer_fetch : 1 = false;
  bool m_per_sample_shading : 1 = false;
  bool m_scaled_dithering : 1 = false;
  bool m_disable_color_perspective : 1 = false;

  GPUTextureFilter m_texture_filtering = GPUTextureFilter::Nearest;
  GPULineDetectMode m_line_detect_mode = GPULineDetectMode::Disabled;
  GPUDownsampleMode m_downsample_mode = GPUDownsampleMode::Disabled;
  GPUWireframeMode m_wireframe_mode = GPUWireframeMode::Disabled;
  bool m_true_color : 1 = true;
  bool m_debanding : 1 = false;
  bool m_clamp_uvs : 1 = false;
  bool m_compute_uv_range : 1 = false;
  bool m_pgxp_depth_buffer : 1 = false;
  bool m_allow_shader_blend : 1 = false;
  bool m_prefer_shader_blend : 1 = false;
  u8 m_texpage_dirty = 0;

  BatchConfig m_batch;

  // Changed state
  bool m_batch_ubo_dirty = true;
  BatchUBOData m_batch_ubo_data = {};

  // Bounding box of VRAM area that the GPU has drawn into.
  Common::Rectangle<u32> m_vram_dirty_draw_rect;
  Common::Rectangle<u32> m_vram_dirty_write_rect;
  Common::Rectangle<u32> m_current_uv_range;

  std::unique_ptr<GPUPipeline> m_wireframe_pipeline;

  // [wrapped][interlaced]
  DimensionalArray<std::unique_ptr<GPUPipeline>, 2, 2> m_vram_fill_pipelines{};

  // [depth_test]
  std::array<std::unique_ptr<GPUPipeline>, 2> m_vram_write_pipelines{};
  std::array<std::unique_ptr<GPUPipeline>, 2> m_vram_copy_pipelines{};

  std::unique_ptr<GPUPipeline> m_vram_readback_pipeline;
  std::unique_ptr<GPUPipeline> m_vram_update_depth_pipeline;
  std::unique_ptr<GPUPipeline> m_vram_write_replacement_pipeline;

  std::array<std::unique_ptr<GPUPipeline>, 2> m_vram_extract_pipeline; // [24bit]
  std::unique_ptr<GPUTexture> m_vram_extract_texture;

  std::unique_ptr<GPUTexture> m_downsample_texture;
  std::unique_ptr<GPUPipeline> m_downsample_first_pass_pipeline;
  std::unique_ptr<GPUPipeline> m_downsample_mid_pass_pipeline;
  std::unique_ptr<GPUPipeline> m_downsample_blur_pass_pipeline;
  std::unique_ptr<GPUPipeline> m_downsample_composite_pass_pipeline;
  std::unique_ptr<GPUSampler> m_downsample_lod_sampler;
  std::unique_ptr<GPUSampler> m_downsample_composite_sampler;
  u32 m_downsample_scale_or_levels = 0;

  // [depth_test][transparency_mode][render_mode][texture_mode][dithering][interlacing][check_mask]
  DimensionalArray<std::unique_ptr<GPUPipeline>, 2, 2, 2, 9, 5, 5, 2> m_batch_pipelines{};
};
