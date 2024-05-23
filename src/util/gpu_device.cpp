// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "gpu_device.h"
#include "core/host.h"     // TODO: Remove, needed for getting fullscreen mode.
#include "core/settings.h" // TODO: Remove, needed for dump directory.
#include "gpu_framebuffer_manager.h"
#include "shadergen.h"

#include "common/assert.h"
#include "common/dynamic_library.h"
#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/string_util.h"
#include "common/timer.h"

#include "fmt/format.h"
#include "imgui.h"
#include "xxhash.h"

Log_SetChannel(GPUDevice);

#ifdef _WIN32
#include "common/windows_headers.h"
#include "d3d11_device.h"
#include "d3d12_device.h"
#include "d3d_common.h"
#endif

#ifdef ENABLE_OPENGL
#include "opengl_device.h"
#endif

#ifdef ENABLE_VULKAN
#include "vulkan_device.h"
#endif

#if defined(ENABLE_VULKAN) || defined(__APPLE__)
#include "shaderc/shaderc.h"
#endif

std::unique_ptr<GPUDevice> g_gpu_device;

static std::string s_pipeline_cache_path;
size_t GPUDevice::s_total_vram_usage = 0;
GPUDevice::Statistics GPUDevice::s_stats = {};

GPUSampler::GPUSampler() = default;

GPUSampler::~GPUSampler() = default;

GPUSampler::Config GPUSampler::GetNearestConfig()
{
  Config config = {};
  config.address_u = GPUSampler::AddressMode::ClampToEdge;
  config.address_v = GPUSampler::AddressMode::ClampToEdge;
  config.address_w = GPUSampler::AddressMode::ClampToEdge;
  config.min_filter = GPUSampler::Filter::Nearest;
  config.mag_filter = GPUSampler::Filter::Nearest;
  return config;
}

GPUSampler::Config GPUSampler::GetLinearConfig()
{
  Config config = {};
  config.address_u = GPUSampler::AddressMode::ClampToEdge;
  config.address_v = GPUSampler::AddressMode::ClampToEdge;
  config.address_w = GPUSampler::AddressMode::ClampToEdge;
  config.min_filter = GPUSampler::Filter::Linear;
  config.mag_filter = GPUSampler::Filter::Linear;
  return config;
}

GPUShader::GPUShader(GPUShaderStage stage) : m_stage(stage)
{
}

GPUShader::~GPUShader() = default;

const char* GPUShader::GetStageName(GPUShaderStage stage)
{
  static constexpr std::array<const char*, static_cast<u32>(GPUShaderStage::MaxCount)> names = {"Vertex", "Fragment",
                                                                                                "Geometry", "Compute"};

  return names[static_cast<u32>(stage)];
}

GPUPipeline::GPUPipeline() = default;

GPUPipeline::~GPUPipeline() = default;

size_t GPUPipeline::InputLayoutHash::operator()(const InputLayout& il) const
{
  std::size_t h = 0;
  hash_combine(h, il.vertex_attributes.size(), il.vertex_stride);

  for (const VertexAttribute& va : il.vertex_attributes)
    hash_combine(h, va.key);

  return h;
}

bool GPUPipeline::InputLayout::operator==(const InputLayout& rhs) const
{
  return (vertex_stride == rhs.vertex_stride && vertex_attributes.size() == rhs.vertex_attributes.size() &&
          std::memcmp(vertex_attributes.data(), rhs.vertex_attributes.data(),
                      sizeof(VertexAttribute) * rhs.vertex_attributes.size()) == 0);
}

bool GPUPipeline::InputLayout::operator!=(const InputLayout& rhs) const
{
  return (vertex_stride != rhs.vertex_stride || vertex_attributes.size() != rhs.vertex_attributes.size() ||
          std::memcmp(vertex_attributes.data(), rhs.vertex_attributes.data(),
                      sizeof(VertexAttribute) * rhs.vertex_attributes.size()) != 0);
}

GPUPipeline::RasterizationState GPUPipeline::RasterizationState::GetNoCullState()
{
  RasterizationState ret = {};
  ret.cull_mode = CullMode::None;
  return ret;
}

GPUPipeline::DepthState GPUPipeline::DepthState::GetNoTestsState()
{
  DepthState ret = {};
  ret.depth_test = DepthFunc::Always;
  return ret;
}

GPUPipeline::DepthState GPUPipeline::DepthState::GetAlwaysWriteState()
{
  DepthState ret = {};
  ret.depth_test = DepthFunc::Always;
  ret.depth_write = true;
  return ret;
}

GPUPipeline::BlendState GPUPipeline::BlendState::GetNoBlendingState()
{
  BlendState ret = {};
  ret.write_mask = 0xf;
  return ret;
}

GPUPipeline::BlendState GPUPipeline::BlendState::GetAlphaBlendingState()
{
  BlendState ret = {};
  ret.enable = true;
  ret.src_blend = BlendFunc::SrcAlpha;
  ret.dst_blend = BlendFunc::InvSrcAlpha;
  ret.blend_op = BlendOp::Add;
  ret.src_alpha_blend = BlendFunc::One;
  ret.dst_alpha_blend = BlendFunc::Zero;
  ret.alpha_blend_op = BlendOp::Add;
  ret.write_mask = 0xf;
  return ret;
}

void GPUPipeline::GraphicsConfig::SetTargetFormats(GPUTexture::Format color_format,
                                                   GPUTexture::Format depth_format_ /* = GPUTexture::Format::Unknown */)
{
  color_formats[0] = color_format;
  for (size_t i = 1; i < std::size(color_formats); i++)
    color_formats[i] = GPUTexture::Format::Unknown;
  depth_format = depth_format_;
}

GPUTextureBuffer::GPUTextureBuffer(Format format, u32 size) : m_format(format), m_size_in_elements(size)
{
}

GPUTextureBuffer::~GPUTextureBuffer() = default;

u32 GPUTextureBuffer::GetElementSize(Format format)
{
  static constexpr std::array<u32, static_cast<u32>(Format::MaxCount)> element_size = {{
    sizeof(u16),
  }};

  return element_size[static_cast<u32>(format)];
}

bool GPUFramebufferManagerBase::Key::operator==(const Key& rhs) const
{
  return (std::memcmp(this, &rhs, sizeof(*this)) == 0);
}

bool GPUFramebufferManagerBase::Key::operator!=(const Key& rhs) const
{
  return (std::memcmp(this, &rhs, sizeof(*this)) != 0);
}

bool GPUFramebufferManagerBase::Key::ContainsRT(const GPUTexture* tex) const
{
  // num_rts is worse for predictability.
  for (u32 i = 0; i < GPUDevice::MAX_RENDER_TARGETS; i++)
  {
    if (rts[i] == tex)
      return true;
  }
  return false;
}

size_t GPUFramebufferManagerBase::KeyHash::operator()(const Key& key) const
{
  if constexpr (sizeof(void*) == 8)
    return XXH3_64bits(&key, sizeof(key));
  else
    return XXH32(&key, sizeof(key), 0x1337);
}

GPUDevice::GPUDevice()
{
  ResetStatistics();
}

GPUDevice::~GPUDevice() = default;

RenderAPI GPUDevice::GetPreferredAPI()
{
  static RenderAPI preferred_renderer = RenderAPI::None;
  if (preferred_renderer == RenderAPI::None) [[unlikely]]
  {
#if defined(_WIN32) && !defined(_M_ARM64)
    // Perfer DX11 on Windows, except ARM64, where QCom has slow DX11 drivers.
    preferred_renderer = RenderAPI::D3D11;
#elif defined(_WIN32) && defined(_M_ARM64)
    preferred_renderer = RenderAPI::D3D12;
#elif defined(__APPLE__)
    // Prefer Metal on MacOS.
    preferred_renderer = RenderAPI::Metal;
#elif defined(ENABLE_OPENGL) && defined(ENABLE_VULKAN)
    // On Linux, if we have both GL and Vulkan, prefer VK if the driver isn't software.
    preferred_renderer = VulkanDevice::IsSuitableDefaultRenderer() ? RenderAPI::Vulkan : RenderAPI::OpenGL;
#elif defined(ENABLE_OPENGL)
    preferred_renderer = RenderAPI::OpenGL;
#elif defined(ENABLE_VULKAN)
    preferred_renderer = RenderAPI::Vulkan;
#else
    // Uhhh, what?
    ERROR_LOG("Somehow don't have any renderers available...");
    preferred_renderer = RenderAPI::None;
#endif
  }

  return preferred_renderer;
}

const char* GPUDevice::RenderAPIToString(RenderAPI api)
{
  switch (api)
  {
    // clang-format off
#define CASE(x) case RenderAPI::x: return #x
    CASE(None);
    CASE(D3D11);
    CASE(D3D12);
    CASE(Metal);
    CASE(Vulkan);
    CASE(OpenGL);
    CASE(OpenGLES);
#undef CASE
      // clang-format on
    default:
      return "Unknown";
  }
}

bool GPUDevice::IsSameRenderAPI(RenderAPI lhs, RenderAPI rhs)
{
  return (lhs == rhs || ((lhs == RenderAPI::OpenGL || lhs == RenderAPI::OpenGLES) &&
                         (rhs == RenderAPI::OpenGL || rhs == RenderAPI::OpenGLES)));
}

bool GPUDevice::Create(std::string_view adapter, std::string_view shader_cache_path, u32 shader_cache_version,
                       bool debug_device, GPUVSyncMode vsync, bool threaded_presentation,
                       std::optional<bool> exclusive_fullscreen_control, FeatureMask disabled_features, Error* error)
{
  m_vsync_mode = vsync;
  m_debug_device = debug_device;

  if (!AcquireWindow(true))
  {
    Error::SetStringView(error, "Failed to acquire window from host.");
    return false;
  }

  if (!CreateDevice(adapter, threaded_presentation, exclusive_fullscreen_control, disabled_features, error))
  {
    if (error && !error->IsValid())
      error->SetStringView("Failed to create device.");
    return false;
  }

  INFO_LOG("Graphics Driver Info:\n{}", GetDriverInfo());

  OpenShaderCache(shader_cache_path, shader_cache_version);

  if (!CreateResources())
  {
    Error::SetStringView(error, "Failed to create base resources.");
    return false;
  }

  return true;
}

void GPUDevice::Destroy()
{
  PurgeTexturePool();
  if (HasSurface())
    DestroySurface();
  DestroyResources();
  CloseShaderCache();
  DestroyDevice();
}

bool GPUDevice::SupportsExclusiveFullscreen() const
{
  return false;
}

void GPUDevice::OpenShaderCache(std::string_view base_path, u32 version)
{
  if (m_features.shader_cache && !base_path.empty())
  {
    const std::string basename = GetShaderCacheBaseName("shaders");
    const std::string filename = Path::Combine(base_path, basename);
    if (!m_shader_cache.Open(filename.c_str(), version))
    {
      WARNING_LOG("Failed to open shader cache. Creating new cache.");
      if (!m_shader_cache.Create())
        ERROR_LOG("Failed to create new shader cache.");

      // Squish the pipeline cache too, it's going to be stale.
      if (m_features.pipeline_cache)
      {
        const std::string pc_filename =
          Path::Combine(base_path, TinyString::from_format("{}.bin", GetShaderCacheBaseName("pipelines")));
        if (FileSystem::FileExists(pc_filename.c_str()))
        {
          INFO_LOG("Removing old pipeline cache '{}'", Path::GetFileName(pc_filename));
          FileSystem::DeleteFile(pc_filename.c_str());
        }
      }
    }
  }
  else
  {
    // Still need to set the version - GL needs it.
    m_shader_cache.Open(std::string_view(), version);
  }

  s_pipeline_cache_path = {};
  if (m_features.pipeline_cache && !base_path.empty())
  {
    const std::string basename = GetShaderCacheBaseName("pipelines");
    const std::string filename = Path::Combine(base_path, TinyString::from_format("{}.bin", basename));
    if (ReadPipelineCache(filename))
      s_pipeline_cache_path = std::move(filename);
    else
      WARNING_LOG("Failed to read pipeline cache.");
  }
}

void GPUDevice::CloseShaderCache()
{
  m_shader_cache.Close();

  if (!s_pipeline_cache_path.empty())
  {
    DynamicHeapArray<u8> data;
    if (GetPipelineCacheData(&data))
    {
      // Save disk writes if it hasn't changed, think of the poor SSDs.
      FILESYSTEM_STAT_DATA sd;
      if (!FileSystem::StatFile(s_pipeline_cache_path.c_str(), &sd) || sd.Size != static_cast<s64>(data.size()))
      {
        INFO_LOG("Writing {} bytes to '{}'", data.size(), Path::GetFileName(s_pipeline_cache_path));
        if (!FileSystem::WriteBinaryFile(s_pipeline_cache_path.c_str(), data.data(), data.size()))
          ERROR_LOG("Failed to write pipeline cache to '{}'", Path::GetFileName(s_pipeline_cache_path));
      }
      else
      {
        INFO_LOG("Skipping updating pipeline cache '{}' due to no changes.", Path::GetFileName(s_pipeline_cache_path));
      }
    }

    s_pipeline_cache_path = {};
  }
}

std::string GPUDevice::GetShaderCacheBaseName(std::string_view type) const
{
  const std::string_view debug_suffix = m_debug_device ? "_debug" : "";

  std::string ret;
  switch (GetRenderAPI())
  {
#ifdef _WIN32
    case RenderAPI::D3D11:
      ret = fmt::format(
        "d3d11_{}_{}{}", type,
        D3DCommon::GetFeatureLevelShaderModelString(D3D11Device::GetInstance().GetD3DDevice()->GetFeatureLevel()),
        debug_suffix);
      break;
    case RenderAPI::D3D12:
      ret = fmt::format("d3d12_{}{}", type, debug_suffix);
      break;
#endif
#ifdef ENABLE_VULKAN
    case RenderAPI::Vulkan:
      ret = fmt::format("vulkan_{}{}", type, debug_suffix);
      break;
#endif
#ifdef ENABLE_OPENGL
    case RenderAPI::OpenGL:
      ret = fmt::format("opengl_{}{}", type, debug_suffix);
      break;
    case RenderAPI::OpenGLES:
      ret = fmt::format("opengles_{}{}", type, debug_suffix);
      break;
#endif
#ifdef __APPLE__
    case RenderAPI::Metal:
      ret = fmt::format("metal_{}{}", type, debug_suffix);
      break;
#endif
    default:
      UnreachableCode();
      break;
  }

  return ret;
}

bool GPUDevice::ReadPipelineCache(const std::string& filename)
{
  return false;
}

bool GPUDevice::GetPipelineCacheData(DynamicHeapArray<u8>* data)
{
  return false;
}

bool GPUDevice::AcquireWindow(bool recreate_window)
{
  std::optional<WindowInfo> wi = Host::AcquireRenderWindow(recreate_window);
  if (!wi.has_value())
    return false;

  INFO_LOG("Render window is {}x{}.", wi->surface_width, wi->surface_height);
  m_window_info = wi.value();
  return true;
}

bool GPUDevice::CreateResources()
{
  if (!(m_nearest_sampler = CreateSampler(GPUSampler::GetNearestConfig())))
    return false;

  if (!(m_linear_sampler = CreateSampler(GPUSampler::GetLinearConfig())))
    return false;

  ShaderGen shadergen(GetRenderAPI(), m_features.dual_source_blend, m_features.framebuffer_fetch);

  std::unique_ptr<GPUShader> imgui_vs = CreateShader(GPUShaderStage::Vertex, shadergen.GenerateImGuiVertexShader());
  std::unique_ptr<GPUShader> imgui_fs = CreateShader(GPUShaderStage::Fragment, shadergen.GenerateImGuiFragmentShader());
  if (!imgui_vs || !imgui_fs)
    return false;
  GL_OBJECT_NAME(imgui_vs, "ImGui Vertex Shader");
  GL_OBJECT_NAME(imgui_fs, "ImGui Fragment Shader");

  static constexpr GPUPipeline::VertexAttribute imgui_attributes[] = {
    GPUPipeline::VertexAttribute::Make(0, GPUPipeline::VertexAttribute::Semantic::Position, 0,
                                       GPUPipeline::VertexAttribute::Type::Float, 2, OFFSETOF(ImDrawVert, pos)),
    GPUPipeline::VertexAttribute::Make(1, GPUPipeline::VertexAttribute::Semantic::TexCoord, 0,
                                       GPUPipeline::VertexAttribute::Type::Float, 2, OFFSETOF(ImDrawVert, uv)),
    GPUPipeline::VertexAttribute::Make(2, GPUPipeline::VertexAttribute::Semantic::Color, 0,
                                       GPUPipeline::VertexAttribute::Type::UNorm8, 4, OFFSETOF(ImDrawVert, col)),
  };

  GPUPipeline::GraphicsConfig plconfig;
  plconfig.layout = GPUPipeline::Layout::SingleTextureAndPushConstants;
  plconfig.input_layout.vertex_attributes = imgui_attributes;
  plconfig.input_layout.vertex_stride = sizeof(ImDrawVert);
  plconfig.primitive = GPUPipeline::Primitive::Triangles;
  plconfig.rasterization = GPUPipeline::RasterizationState::GetNoCullState();
  plconfig.depth = GPUPipeline::DepthState::GetNoTestsState();
  plconfig.blend = GPUPipeline::BlendState::GetAlphaBlendingState();
  plconfig.blend.write_mask = 0x7;
  plconfig.SetTargetFormats(HasSurface() ? m_window_info.surface_format : GPUTexture::Format::RGBA8);
  plconfig.samples = 1;
  plconfig.per_sample_shading = false;
  plconfig.render_pass_flags = GPUPipeline::NoRenderPassFlags;
  plconfig.vertex_shader = imgui_vs.get();
  plconfig.geometry_shader = nullptr;
  plconfig.fragment_shader = imgui_fs.get();

  m_imgui_pipeline = CreatePipeline(plconfig);
  if (!m_imgui_pipeline)
  {
    ERROR_LOG("Failed to compile ImGui pipeline.");
    return false;
  }
  GL_OBJECT_NAME(m_imgui_pipeline, "ImGui Pipeline");

  return true;
}

void GPUDevice::DestroyResources()
{
  m_imgui_font_texture.reset();
  m_imgui_pipeline.reset();

  m_imgui_pipeline.reset();

  m_linear_sampler.reset();
  m_nearest_sampler.reset();

  m_shader_cache.Close();
}

void GPUDevice::RenderImGui()
{
  GL_SCOPE("RenderImGui");

  ImGui::Render();

  const ImDrawData* draw_data = ImGui::GetDrawData();
  if (draw_data->CmdListsCount == 0)
    return;

  SetPipeline(m_imgui_pipeline.get());
  SetViewportAndScissor(0, 0, m_window_info.surface_width, m_window_info.surface_height);

  const float L = 0.0f;
  const float R = static_cast<float>(m_window_info.surface_width);
  const float T = 0.0f;
  const float B = static_cast<float>(m_window_info.surface_height);
  const float ortho_projection[4][4] = {
    {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
    {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
    {0.0f, 0.0f, 0.5f, 0.0f},
    {(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f},
  };
  PushUniformBuffer(ortho_projection, sizeof(ortho_projection));

  // Render command lists
  const bool flip = UsesLowerLeftOrigin();
  for (int n = 0; n < draw_data->CmdListsCount; n++)
  {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    static_assert(sizeof(ImDrawIdx) == sizeof(DrawIndex));

    u32 base_vertex, base_index;
    UploadVertexBuffer(cmd_list->VtxBuffer.Data, sizeof(ImDrawVert), cmd_list->VtxBuffer.Size, &base_vertex);
    UploadIndexBuffer(cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size, &base_index);

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
    {
      const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
      DebugAssert(!pcmd->UserCallback);

      if (pcmd->ElemCount == 0 || pcmd->ClipRect.z <= pcmd->ClipRect.x || pcmd->ClipRect.w <= pcmd->ClipRect.y)
        continue;

      if (flip)
      {
        const s32 height = static_cast<s32>(pcmd->ClipRect.w - pcmd->ClipRect.y);
        const s32 flipped_y =
          static_cast<s32>(m_window_info.surface_height) - static_cast<s32>(pcmd->ClipRect.y) - height;
        SetScissor(static_cast<s32>(pcmd->ClipRect.x), flipped_y, static_cast<s32>(pcmd->ClipRect.z - pcmd->ClipRect.x),
                   height);
      }
      else
      {
        SetScissor(static_cast<s32>(pcmd->ClipRect.x), static_cast<s32>(pcmd->ClipRect.y),
                   static_cast<s32>(pcmd->ClipRect.z - pcmd->ClipRect.x),
                   static_cast<s32>(pcmd->ClipRect.w - pcmd->ClipRect.y));
      }

      SetTextureSampler(0, reinterpret_cast<GPUTexture*>(pcmd->TextureId), m_linear_sampler.get());
      DrawIndexed(pcmd->ElemCount, base_index + pcmd->IdxOffset, base_vertex + pcmd->VtxOffset);
    }
  }
}

void GPUDevice::UploadVertexBuffer(const void* vertices, u32 vertex_size, u32 vertex_count, u32* base_vertex)
{
  void* map;
  u32 space;
  MapVertexBuffer(vertex_size, vertex_count, &map, &space, base_vertex);
  std::memcpy(map, vertices, vertex_size * vertex_count);
  UnmapVertexBuffer(vertex_size, vertex_count);
}

void GPUDevice::UploadIndexBuffer(const u16* indices, u32 index_count, u32* base_index)
{
  u16* map;
  u32 space;
  MapIndexBuffer(index_count, &map, &space, base_index);
  std::memcpy(map, indices, sizeof(u16) * index_count);
  UnmapIndexBuffer(index_count);
}

void GPUDevice::UploadUniformBuffer(const void* data, u32 data_size)
{
  void* map = MapUniformBuffer(data_size);
  std::memcpy(map, data, data_size);
  UnmapUniformBuffer(data_size);
}

void GPUDevice::SetRenderTarget(GPUTexture* rt, GPUTexture* ds, GPUPipeline::RenderPassFlag render_pass_flags)
{
  SetRenderTargets(rt ? &rt : nullptr, rt ? 1 : 0, ds, render_pass_flags);
}

void GPUDevice::SetViewportAndScissor(s32 x, s32 y, s32 width, s32 height)
{
  SetViewport(x, y, width, height);
  SetScissor(x, y, width, height);
}

void GPUDevice::ClearRenderTarget(GPUTexture* t, u32 c)
{
  t->SetClearColor(c);
}

void GPUDevice::ClearDepth(GPUTexture* t, float d)
{
  t->SetClearDepth(d);
}

void GPUDevice::InvalidateRenderTarget(GPUTexture* t)
{
  t->SetState(GPUTexture::State::Invalidated);
}

std::unique_ptr<GPUShader> GPUDevice::CreateShader(GPUShaderStage stage, std::string_view source,
                                                   const char* entry_point /* = "main" */)
{
  std::unique_ptr<GPUShader> shader;
  if (!m_shader_cache.IsOpen())
  {
    shader = CreateShaderFromSource(stage, source, entry_point, nullptr);
    return shader;
  }

  const GPUShaderCache::CacheIndexKey key = m_shader_cache.GetCacheKey(stage, source, entry_point);
  DynamicHeapArray<u8> binary;
  if (m_shader_cache.Lookup(key, &binary))
  {
    shader = CreateShaderFromBinary(stage, binary);
    if (shader)
      return shader;

    ERROR_LOG("Failed to create shader from binary (driver changed?). Clearing cache.");
    m_shader_cache.Clear();
  }

  shader = CreateShaderFromSource(stage, source, entry_point, &binary);
  if (!shader)
    return shader;

  // Don't insert empty shaders into the cache...
  if (!binary.empty())
  {
    if (!m_shader_cache.Insert(key, binary.data(), static_cast<u32>(binary.size())))
      m_shader_cache.Close();
  }

  return shader;
}

bool GPUDevice::GetRequestedExclusiveFullscreenMode(u32* width, u32* height, float* refresh_rate)
{
  const std::string mode = Host::GetBaseStringSettingValue("GPU", "FullscreenMode", "");
  if (!mode.empty())
  {
    const std::string_view mode_view = mode;
    std::string_view::size_type sep1 = mode.find('x');
    if (sep1 != std::string_view::npos)
    {
      std::optional<u32> owidth = StringUtil::FromChars<u32>(mode_view.substr(0, sep1));
      sep1++;

      while (sep1 < mode.length() && std::isspace(mode[sep1]))
        sep1++;

      if (owidth.has_value() && sep1 < mode.length())
      {
        std::string_view::size_type sep2 = mode.find('@', sep1);
        if (sep2 != std::string_view::npos)
        {
          std::optional<u32> oheight = StringUtil::FromChars<u32>(mode_view.substr(sep1, sep2 - sep1));
          sep2++;

          while (sep2 < mode.length() && std::isspace(mode[sep2]))
            sep2++;

          if (oheight.has_value() && sep2 < mode.length())
          {
            std::optional<float> orefresh_rate = StringUtil::FromChars<float>(mode_view.substr(sep2));
            if (orefresh_rate.has_value())
            {
              *width = owidth.value();
              *height = oheight.value();
              *refresh_rate = orefresh_rate.value();
              return true;
            }
          }
        }
      }
    }
  }

  *width = 0;
  *height = 0;
  *refresh_rate = 0;
  return false;
}

std::string GPUDevice::GetFullscreenModeString(u32 width, u32 height, float refresh_rate)
{
  return fmt::format("{} x {} @ {} hz", width, height, refresh_rate);
}

std::string GPUDevice::GetShaderDumpPath(std::string_view name)
{
  return Path::Combine(EmuFolders::Dumps, name);
}

void GPUDevice::DumpBadShader(std::string_view code, std::string_view errors)
{
  static u32 next_bad_shader_id = 0;

  const std::string filename = GetShaderDumpPath(fmt::format("bad_shader_{}.txt", ++next_bad_shader_id));
  auto fp = FileSystem::OpenManagedCFile(filename.c_str(), "wb");
  if (fp)
  {
    if (!code.empty())
      std::fwrite(code.data(), code.size(), 1, fp.get());
    std::fputs("\n\n**** ERRORS ****\n", fp.get());
    if (!errors.empty())
      std::fwrite(errors.data(), errors.size(), 1, fp.get());
  }
}

std::array<float, 4> GPUDevice::RGBA8ToFloat(u32 rgba)
{
  return std::array<float, 4>{static_cast<float>(rgba & UINT32_C(0xFF)) * (1.0f / 255.0f),
                              static_cast<float>((rgba >> 8) & UINT32_C(0xFF)) * (1.0f / 255.0f),
                              static_cast<float>((rgba >> 16) & UINT32_C(0xFF)) * (1.0f / 255.0f),
                              static_cast<float>(rgba >> 24) * (1.0f / 255.0f)};
}

bool GPUDevice::UpdateImGuiFontTexture()
{
  ImGuiIO& io = ImGui::GetIO();

  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  const u32 pitch = sizeof(u32) * width;

  if (m_imgui_font_texture && m_imgui_font_texture->GetWidth() == static_cast<u32>(width) &&
      m_imgui_font_texture->GetHeight() == static_cast<u32>(height) &&
      m_imgui_font_texture->Update(0, 0, static_cast<u32>(width), static_cast<u32>(height), pixels, pitch))
  {
    io.Fonts->SetTexID(m_imgui_font_texture.get());
    return true;
  }

  std::unique_ptr<GPUTexture> new_font =
    FetchTexture(width, height, 1, 1, 1, GPUTexture::Type::Texture, GPUTexture::Format::RGBA8, pixels, pitch);
  if (!new_font)
    return false;

  RecycleTexture(std::move(m_imgui_font_texture));
  m_imgui_font_texture = std::move(new_font);
  io.Fonts->SetTexID(m_imgui_font_texture.get());
  return true;
}

bool GPUDevice::UsesLowerLeftOrigin() const
{
  const RenderAPI api = GetRenderAPI();
  return (api == RenderAPI::OpenGL || api == RenderAPI::OpenGLES);
}

Common::Rectangle<s32> GPUDevice::FlipToLowerLeft(const Common::Rectangle<s32>& rc, s32 target_height)
{
  const s32 height = rc.GetHeight();
  const s32 flipped_y = target_height - rc.top - height;
  return Common::Rectangle<s32>(rc.left, flipped_y, rc.right, flipped_y + height);
}

bool GPUDevice::IsTexturePoolType(GPUTexture::Type type)
{
  return (type == GPUTexture::Type::Texture || type == GPUTexture::Type::DynamicTexture);
}

std::unique_ptr<GPUTexture> GPUDevice::FetchTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                    GPUTexture::Type type, GPUTexture::Format format,
                                                    const void* data /*= nullptr*/, u32 data_stride /*= 0*/)
{
  std::unique_ptr<GPUTexture> ret;

  const TexturePoolKey key = {static_cast<u16>(width),
                              static_cast<u16>(height),
                              static_cast<u8>(layers),
                              static_cast<u8>(levels),
                              static_cast<u8>(samples),
                              type,
                              format,
                              0u};

  const bool is_texture = IsTexturePoolType(type);
  TexturePool& pool = is_texture ? m_texture_pool : m_target_pool;
  const u32 pool_size = (is_texture ? MAX_TEXTURE_POOL_SIZE : MAX_TARGET_POOL_SIZE);

  TexturePool::iterator it;

  if (is_texture && m_features.prefer_unused_textures)
  {
    // Try to find a texture that wasn't used this frame first.
    for (it = m_texture_pool.begin(); it != m_texture_pool.end(); ++it)
    {
      if (it->use_counter == m_texture_pool_counter)
      {
        // We're into textures recycled this frame, not going to find anything newer.
        // But prefer reuse over creating a new texture.
        if (m_texture_pool.size() < pool_size)
        {
          it = m_texture_pool.end();
          break;
        }
      }

      if (it->key == key)
        break;
    }
  }
  else
  {
    for (it = pool.begin(); it != pool.end(); ++it)
    {
      if (it->key == key)
        break;
    }
  }

  if (it != pool.end())
  {
    if (!data || it->texture->Update(0, 0, width, height, data, data_stride, 0, 0))
    {
      ret = std::move(it->texture);
      pool.erase(it);
      return ret;
    }
    else
    {
      // This shouldn't happen...
      ERROR_LOG("Failed to upload {}x{} to pooled texture", width, height);
    }
  }

  ret = CreateTexture(width, height, layers, levels, samples, type, format, data, data_stride);
  return ret;
}

std::unique_ptr<GPUTexture, GPUDevice::PooledTextureDeleter>
GPUDevice::FetchAutoRecycleTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples, GPUTexture::Type type,
                                   GPUTexture::Format format, const void* data /*= nullptr*/, u32 data_stride /*= 0*/,
                                   bool dynamic /*= false*/)
{
  std::unique_ptr<GPUTexture> ret =
    FetchTexture(width, height, layers, levels, samples, type, format, data, data_stride);
  return std::unique_ptr<GPUTexture, PooledTextureDeleter>(ret.release());
}

void GPUDevice::RecycleTexture(std::unique_ptr<GPUTexture> texture)
{
  if (!texture)
    return;

  const TexturePoolKey key = {static_cast<u16>(texture->GetWidth()),
                              static_cast<u16>(texture->GetHeight()),
                              static_cast<u8>(texture->GetLayers()),
                              static_cast<u8>(texture->GetLevels()),
                              static_cast<u8>(texture->GetSamples()),
                              texture->GetType(),
                              texture->GetFormat(),
                              0u};

  const bool is_texture = IsTexturePoolType(texture->GetType());
  TexturePool& pool = is_texture ? m_texture_pool : m_target_pool;
  pool.push_back({std::move(texture), m_texture_pool_counter, key});

  const u32 max_size = is_texture ? MAX_TEXTURE_POOL_SIZE : MAX_TARGET_POOL_SIZE;
  while (pool.size() > max_size)
  {
    DEBUG_LOG("Trim {}x{} texture from pool", pool.front().texture->GetWidth(), pool.front().texture->GetHeight());
    pool.pop_front();
  }
}

void GPUDevice::PurgeTexturePool()
{
  m_texture_pool_counter = 0;
  m_texture_pool.clear();
  m_target_pool.clear();
}

void GPUDevice::TrimTexturePool()
{
  GL_INS_FMT("Texture Pool Size: {}", m_texture_pool.size());
  GL_INS_FMT("Target Pool Size: {}", m_target_pool.size());
  GL_INS_FMT("VRAM Usage: {:.2f} MB", s_total_vram_usage / 1048576.0);

  DEBUG_LOG("Texture Pool Size: {} Target Pool Size: {} VRAM: {:.2f} MB", m_texture_pool.size(), m_target_pool.size(),
            s_total_vram_usage / 1048756.0);

  if (m_texture_pool.empty() && m_target_pool.empty())
    return;

  const u32 prev_counter = m_texture_pool_counter++;
  for (u32 pool_idx = 0; pool_idx < 2; pool_idx++)
  {
    TexturePool& pool = pool_idx ? m_target_pool : m_texture_pool;
    for (auto it = pool.begin(); it != pool.end();)
    {
      const u32 delta = (prev_counter - it->use_counter);
      if (delta < POOL_PURGE_DELAY)
        break;

      DEBUG_LOG("Trim {}x{} texture from pool", it->texture->GetWidth(), it->texture->GetHeight());
      it = pool.erase(it);
    }
  }

  if (m_texture_pool_counter < prev_counter) [[unlikely]]
  {
    // wrapped around, handle it
    if (m_texture_pool.empty() && m_target_pool.empty())
    {
      m_texture_pool_counter = 0;
    }
    else
    {
      const u32 texture_min =
        m_texture_pool.empty() ? std::numeric_limits<u32>::max() : m_texture_pool.front().use_counter;
      const u32 target_min =
        m_target_pool.empty() ? std::numeric_limits<u32>::max() : m_target_pool.front().use_counter;
      const u32 reduce = std::min(texture_min, target_min);
      m_texture_pool_counter -= reduce;
      for (u32 pool_idx = 0; pool_idx < 2; pool_idx++)
      {
        TexturePool& pool = pool_idx ? m_target_pool : m_texture_pool;
        for (TexturePoolEntry& entry : pool)
          entry.use_counter -= reduce;
      }
    }
  }
}

void GPUDevice::SetDisplayMaxFPS(float max_fps)
{
  m_display_frame_interval = (max_fps > 0.0f) ? (1.0f / max_fps) : 0.0f;
}

bool GPUDevice::ResizeTexture(std::unique_ptr<GPUTexture>* tex, u32 new_width, u32 new_height, GPUTexture::Type type,
                              GPUTexture::Format format, bool preserve /* = true */)
{
  GPUTexture* old_tex = tex->get();
  DebugAssert(!old_tex || (old_tex->GetLayers() == 1 && old_tex->GetLevels() == 1 && old_tex->GetSamples() == 1));
  std::unique_ptr<GPUTexture> new_tex = FetchTexture(new_width, new_height, 1, 1, 1, type, format);
  if (!new_tex) [[unlikely]]
  {
    ERROR_LOG("Failed to create new {}x{} texture", new_width, new_height);
    return false;
  }

  if (old_tex)
  {
    if (old_tex->GetState() == GPUTexture::State::Cleared)
    {
      if (type == GPUTexture::Type::RenderTarget)
        ClearRenderTarget(new_tex.get(), old_tex->GetClearColor());
    }
    else if (old_tex->GetState() == GPUTexture::State::Dirty)
    {
      const u32 copy_width = std::min(new_width, old_tex->GetWidth());
      const u32 copy_height = std::min(new_height, old_tex->GetHeight());
      if (type == GPUTexture::Type::RenderTarget)
        ClearRenderTarget(new_tex.get(), 0);
      CopyTextureRegion(new_tex.get(), 0, 0, 0, 0, old_tex, 0, 0, 0, 0, copy_width, copy_height);
    }
  }
  else if (preserve)
  {
    // If we're expecting data to be there, make sure to clear it.
    if (type == GPUTexture::Type::RenderTarget)
      ClearRenderTarget(new_tex.get(), 0);
  }

  RecycleTexture(std::move(*tex));
  *tex = std::move(new_tex);
  return true;
}

bool GPUDevice::ShouldSkipDisplayingFrame()
{
  if (m_display_frame_interval == 0.0f)
    return false;

  const u64 now = Common::Timer::GetCurrentValue();
  const double diff = Common::Timer::ConvertValueToSeconds(now - m_last_frame_displayed_time);
  if (diff < m_display_frame_interval)
    return true;

  m_last_frame_displayed_time = now;
  return false;
}

void GPUDevice::ThrottlePresentation()
{
  const float throttle_rate = (m_window_info.surface_refresh_rate > 0.0f) ? m_window_info.surface_refresh_rate : 60.0f;

  const u64 sleep_period = Common::Timer::ConvertNanosecondsToValue(1e+9f / static_cast<double>(throttle_rate));
  const u64 current_ts = Common::Timer::GetCurrentValue();

  // Allow it to fall behind/run ahead up to 2*period. Sleep isn't that precise, plus we need to
  // allow time for the actual rendering.
  const u64 max_variance = sleep_period * 2;
  if (static_cast<u64>(std::abs(static_cast<s64>(current_ts - m_last_frame_displayed_time))) > max_variance)
    m_last_frame_displayed_time = current_ts + sleep_period;
  else
    m_last_frame_displayed_time += sleep_period;

  Common::Timer::SleepUntil(m_last_frame_displayed_time, false);
}

bool GPUDevice::SetGPUTimingEnabled(bool enabled)
{
  return false;
}

float GPUDevice::GetAndResetAccumulatedGPUTime()
{
  return 0.0f;
}

void GPUDevice::ResetStatistics()
{
  s_stats = {};
}

std::unique_ptr<GPUDevice> GPUDevice::CreateDeviceForAPI(RenderAPI api)
{
  switch (api)
  {
#ifdef ENABLE_VULKAN
    case RenderAPI::Vulkan:
      return std::make_unique<VulkanDevice>();
#endif

#ifdef ENABLE_OPENGL
    case RenderAPI::OpenGL:
    case RenderAPI::OpenGLES:
      return std::make_unique<OpenGLDevice>();
#endif

#ifdef _WIN32
    case RenderAPI::D3D12:
      return std::make_unique<D3D12Device>();

    case RenderAPI::D3D11:
      return std::make_unique<D3D11Device>();
#endif

#ifdef __APPLE__
    case RenderAPI::Metal:
      return WrapNewMetalDevice();
#endif

    default:
      return {};
  }
}

#if defined(ENABLE_VULKAN) || defined(__APPLE__)
#define SHADERC_FUNCTIONS(X)                                                                                           \
  X(shaderc_compiler_initialize)                                                                                       \
  X(shaderc_compiler_release)                                                                                          \
  X(shaderc_compile_options_initialize)                                                                                \
  X(shaderc_compile_options_release)                                                                                   \
  X(shaderc_compile_options_set_source_language)                                                                       \
  X(shaderc_compile_options_set_generate_debug_info)                                                                   \
  X(shaderc_compile_options_set_optimization_level)                                                                    \
  X(shaderc_compile_options_set_target_env)                                                                            \
  X(shaderc_compilation_status_to_string)                                                                              \
  X(shaderc_compile_into_spv)                                                                                          \
  X(shaderc_result_release)                                                                                            \
  X(shaderc_result_get_length)                                                                                         \
  X(shaderc_result_get_num_warnings)                                                                                   \
  X(shaderc_result_get_bytes)                                                                                          \
  X(shaderc_result_get_error_message)

// TODO: NOT thread safe, yet.
namespace dyn_shaderc {
static bool Open();
static void Close();

static DynamicLibrary s_library;
static shaderc_compiler_t s_compiler = nullptr;

#define ADD_FUNC(F) static decltype(&::F) F;
SHADERC_FUNCTIONS(ADD_FUNC)
#undef ADD_FUNC

} // namespace dyn_shaderc

bool dyn_shaderc::Open()
{
  if (s_library.IsOpen())
    return true;

  Error error;

#ifdef _WIN32
  const std::string libname = DynamicLibrary::GetVersionedFilename("shaderc_shared");
#else
  // Use versioned, bundle post-processing adds it..
  const std::string libname = DynamicLibrary::GetVersionedFilename("shaderc_shared", 1);
#endif
  if (!s_library.Open(libname.c_str(), &error))
  {
    ERROR_LOG("Failed to load shaderc: {}", error.GetDescription());
    return false;
  }

#define LOAD_FUNC(F)                                                                                                   \
  if (!s_library.GetSymbol(#F, &F))                                                                                    \
  {                                                                                                                    \
    ERROR_LOG("Failed to find function {}", #F);                                                                       \
    Close();                                                                                                           \
    return false;                                                                                                      \
  }

  SHADERC_FUNCTIONS(LOAD_FUNC)
#undef LOAD_FUNC

  s_compiler = shaderc_compiler_initialize();
  if (!s_compiler)
  {
    ERROR_LOG("shaderc_compiler_initialize() failed");
    Close();
    return false;
  }

  std::atexit(&dyn_shaderc::Close);
  return true;
}

void dyn_shaderc::Close()
{
  if (s_compiler)
  {
    shaderc_compiler_release(s_compiler);
    s_compiler = nullptr;
  }

#define UNLOAD_FUNC(F) F = nullptr;
  SHADERC_FUNCTIONS(UNLOAD_FUNC)
#undef UNLOAD_FUNC

  s_library.Close();
}

#undef SHADERC_FUNCTIONS
#undef SHADERC_INIT_FUNCTIONS

bool GPUDevice::CompileGLSLShaderToVulkanSpv(GPUShaderStage stage, std::string_view source, const char* entry_point,
                                             bool nonsemantic_debug_info, DynamicHeapArray<u8>* out_binary)
{
  static constexpr const std::array<shaderc_shader_kind, static_cast<size_t>(GPUShaderStage::MaxCount)> stage_kinds = {{
    shaderc_glsl_vertex_shader,
    shaderc_glsl_fragment_shader,
    shaderc_glsl_geometry_shader,
    shaderc_glsl_compute_shader,
  }};

  if (!dyn_shaderc::Open())
    return false;

  shaderc_compile_options_t options = dyn_shaderc::shaderc_compile_options_initialize();
  AssertMsg(options, "shaderc_compile_options_initialize() failed");

  dyn_shaderc::shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
  dyn_shaderc::shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, 0);
  dyn_shaderc::shaderc_compile_options_set_generate_debug_info(options, m_debug_device,
                                                               m_debug_device && nonsemantic_debug_info);
  dyn_shaderc::shaderc_compile_options_set_optimization_level(
    options, m_debug_device ? shaderc_optimization_level_zero : shaderc_optimization_level_performance);

  shaderc_compilation_result_t result;
  const shaderc_compilation_status status = dyn_shaderc::shaderc_compile_into_spv(
    dyn_shaderc::s_compiler, source.data(), source.length(), stage_kinds[static_cast<size_t>(stage)], "source",
    entry_point, options, &result);
  if (status != shaderc_compilation_status_success)
  {
    const std::string_view errors(result ? dyn_shaderc::shaderc_result_get_error_message(result) :
                                           "null result object");
    ERROR_LOG("Failed to compile shader to SPIR-V: {}\n{}", dyn_shaderc::shaderc_compilation_status_to_string(status),
              errors);
    DumpBadShader(source, errors);
  }
  else
  {
    const size_t num_warnings = dyn_shaderc::shaderc_result_get_num_warnings(result);
    if (num_warnings > 0)
      WARNING_LOG("Shader compiled with warnings:\n{}", dyn_shaderc::shaderc_result_get_error_message(result));

    const size_t spirv_size = dyn_shaderc::shaderc_result_get_length(result);
    DebugAssert(spirv_size > 0);
    out_binary->resize(spirv_size);
    std::memcpy(out_binary->data(), dyn_shaderc::shaderc_result_get_bytes(result), spirv_size);
  }

  dyn_shaderc::shaderc_result_release(result);
  dyn_shaderc::shaderc_compile_options_release(options);
  return (status == shaderc_compilation_status_success);
}

#endif
