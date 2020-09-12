#pragma once
#include "gpu_hw.h"
#include "host_display.h"
#include <sstream>
#include <string>

class ShaderGen
{
public:
  ShaderGen(HostDisplay::RenderAPI render_api, bool supports_dual_source_blend);
  ~ShaderGen();

  static bool UseGLSLBindingLayout();

  std::string GenerateScreenQuadVertexShader();
  std::string GenerateFillFragmentShader();
  std::string GenerateCopyFragmentShader();

protected:
  ALWAYS_INLINE bool IsVulkan() const { return (m_render_api == HostDisplay::RenderAPI::Vulkan); }

  void SetGLSLVersionString();
  void DefineMacro(std::stringstream& ss, const char* name, bool enabled);
  void WriteHeader(std::stringstream& ss);
  void WriteUniformBufferDeclaration(std::stringstream& ss, bool push_constant_on_vulkan);
  void DeclareUniformBuffer(std::stringstream& ss, const std::initializer_list<const char*>& members,
                            bool push_constant_on_vulkan);
  void DeclareTexture(std::stringstream& ss, const char* name, u32 index);
  void DeclareTextureBuffer(std::stringstream& ss, const char* name, u32 index, bool is_int, bool is_unsigned);
  void DeclareVertexEntryPoint(std::stringstream& ss, const std::initializer_list<const char*>& attributes,
                               u32 num_color_outputs, u32 num_texcoord_outputs,
                               const std::initializer_list<std::pair<const char*, const char*>>& additional_outputs,
                               bool declare_vertex_id = false, const char* output_block_suffix = "");
  void DeclareFragmentEntryPoint(std::stringstream& ss, u32 num_color_inputs, u32 num_texcoord_inputs,
                                 const std::initializer_list<std::pair<const char*, const char*>>& additional_inputs,
                                 bool declare_fragcoord = false, u32 num_color_outputs = 1, bool depth_output = false);

  HostDisplay::RenderAPI m_render_api;
  bool m_glsl;
  bool m_supports_dual_source_blend;
  bool m_use_glsl_interface_blocks;
  bool m_use_glsl_binding_layout;

  std::string m_glsl_version_string;
};
