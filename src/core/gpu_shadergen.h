// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include "util/shadergen.h"

class GPUShaderGen : public ShaderGen
{
public:
  GPUShaderGen(RenderAPI render_api, bool supports_dual_source_blend);
  ~GPUShaderGen();

  std::string GenerateDisplayVertexShader();
  std::string GenerateDisplayFragmentShader(bool clamp_uv);
  std::string GenerateDisplaySharpBilinearFragmentShader();

private:
  void WriteDisplayUniformBuffer(std::stringstream& ss);
};
