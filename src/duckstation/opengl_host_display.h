#pragma once
#include "common/gl/program.h"
#include "common/gl/texture.h"
#include "core/host_display.h"
#include <SDL.h>
#include <memory>

class OpenGLHostDisplay final : public HostDisplay
{
public:
  OpenGLHostDisplay(SDL_Window* window);
  ~OpenGLHostDisplay();

  static std::unique_ptr<HostDisplay> Create(SDL_Window* window);

  RenderAPI GetRenderAPI() const override;
  void* GetHostRenderDevice() const override;
  void* GetHostRenderContext() const override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(u32 width, u32 height, const void* data, u32 data_stride,
                                                    bool dynamic) override;
  void UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* data,
                     u32 data_stride) override;

  void SetDisplayTexture(void* texture, u32 offset_x, u32 offset_y, u32 width, u32 height, u32 texture_width,
                         u32 texture_height, float aspect_ratio) override;
  void SetDisplayLinearFiltering(bool enabled) override;

  void SetVSync(bool enabled) override;

  std::tuple<u32, u32> GetWindowSize() const override;
  void WindowResized() override;

private:
  bool CreateGLContext();
  bool CreateImGuiContext();
  bool CreateGLResources();

  void Render();
  void RenderDisplay();

  SDL_Window* m_window = nullptr;
  SDL_GLContext m_gl_context = nullptr;
  int m_window_width = 0;
  int m_window_height = 0;

  std::unique_ptr<GL::Texture> m_app_icon_texture = nullptr;

  GL::Program m_display_program;
  GLuint m_display_vao = 0;
  GLuint m_display_texture_id = 0;
  u32 m_display_offset_x = 0;
  u32 m_display_offset_y = 0;
  u32 m_display_width = 0;
  u32 m_display_height = 0;
  u32 m_display_texture_width = 0;
  u32 m_display_texture_height = 0;
  float m_display_aspect_ratio = 1.0f;
  GLuint m_display_nearest_sampler = 0;
  GLuint m_display_linear_sampler = 0;

  bool m_display_texture_changed = false;
  bool m_display_linear_filtering = false;
};
