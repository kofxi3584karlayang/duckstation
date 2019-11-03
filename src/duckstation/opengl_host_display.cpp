#include "opengl_host_display.h"
#include "YBaseLib/Log.h"
#include "icon.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl.h>
Log_SetChannel(OpenGLHostDisplay);

class OpenGLHostDisplayTexture : public HostDisplayTexture
{
public:
  OpenGLHostDisplayTexture(GLuint id, u32 width, u32 height) : m_id(id), m_width(width), m_height(height) {}
  ~OpenGLHostDisplayTexture() override { glDeleteTextures(1, &m_id); }

  void* GetHandle() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_id)); }
  u32 GetWidth() const override { return m_width; }
  u32 GetHeight() const override { return m_height; }

  GLuint GetGLID() const { return m_id; }

  static std::unique_ptr<OpenGLHostDisplayTexture> Create(u32 width, u32 height, const void* initial_data,
                                                          u32 initial_data_stride)
  {
    GLuint id;
    glGenTextures(1, &id);

    GLint old_texture_binding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture_binding);

    // TODO: Set pack width
    Assert(initial_data_stride == (width * sizeof(u32)));

    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, initial_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);

    glBindTexture(GL_TEXTURE_2D, id);
    return std::make_unique<OpenGLHostDisplayTexture>(id, width, height);
  }

private:
  GLuint m_id;
  u32 m_width;
  u32 m_height;
};

OpenGLHostDisplay::OpenGLHostDisplay(SDL_Window* window) : m_window(window)
{
  SDL_GetWindowSize(window, &m_window_width, &m_window_height);
}

OpenGLHostDisplay::~OpenGLHostDisplay()
{
  if (m_gl_context)
  {
    if (m_display_vao != 0)
      glDeleteVertexArrays(1, &m_display_vao);
    if (m_display_linear_sampler != 0)
      glDeleteSamplers(1, &m_display_linear_sampler);
    if (m_display_nearest_sampler != 0)
      glDeleteSamplers(1, &m_display_nearest_sampler);

    m_display_program.Destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    SDL_GL_MakeCurrent(nullptr, nullptr);
    SDL_GL_DeleteContext(m_gl_context);
  }

  if (m_window)
    SDL_DestroyWindow(m_window);
}

HostDisplay::RenderAPI OpenGLHostDisplay::GetRenderAPI() const
{
  return HostDisplay::RenderAPI::OpenGL;
}

void* OpenGLHostDisplay::GetHostRenderDevice() const
{
  return nullptr;
}

void* OpenGLHostDisplay::GetHostRenderContext() const
{
  return m_gl_context;
}

std::unique_ptr<HostDisplayTexture> OpenGLHostDisplay::CreateTexture(u32 width, u32 height, const void* data,
                                                                     u32 data_stride, bool dynamic)
{
  return OpenGLHostDisplayTexture::Create(width, height, data, data_stride);
}

void OpenGLHostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height,
                                      const void* data, u32 data_stride)
{
  OpenGLHostDisplayTexture* tex = static_cast<OpenGLHostDisplayTexture*>(texture);
  Assert(data_stride == (width * sizeof(u32)));

  GLint old_texture_binding = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture_binding);

  glBindTexture(GL_TEXTURE_2D, tex->GetGLID());
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);

  glBindTexture(GL_TEXTURE_2D, old_texture_binding);
}

void OpenGLHostDisplay::SetDisplayTexture(void* texture, u32 offset_x, u32 offset_y, u32 width, u32 height,
                                          u32 texture_width, u32 texture_height, float aspect_ratio)
{
  m_display_texture_id = static_cast<GLuint>(reinterpret_cast<uintptr_t>(texture));
  m_display_offset_x = offset_x;
  m_display_offset_y = offset_y;
  m_display_width = width;
  m_display_height = height;
  m_display_texture_width = texture_width;
  m_display_texture_height = texture_height;
  m_display_aspect_ratio = aspect_ratio;
  m_display_texture_changed = true;
}

void OpenGLHostDisplay::SetDisplayLinearFiltering(bool enabled)
{
  m_display_linear_filtering = enabled;
}

void OpenGLHostDisplay::SetVSync(bool enabled)
{
  // Window framebuffer has to be bound to call SetSwapInterval.
  GLint current_fbo = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  SDL_GL_SetSwapInterval(enabled ? 1 : 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current_fbo);
}

std::tuple<u32, u32> OpenGLHostDisplay::GetWindowSize() const
{
  return std::make_tuple(static_cast<u32>(m_window_width), static_cast<u32>(m_window_height));
}

void OpenGLHostDisplay::WindowResized()
{
  SDL_GetWindowSize(m_window, &m_window_width, &m_window_height);
}

static void APIENTRY GLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                     const GLchar* message, const void* userParam)
{
  switch (severity)
  {
    case GL_DEBUG_SEVERITY_HIGH_KHR:
      Log_ErrorPrintf(message);
      break;
    case GL_DEBUG_SEVERITY_MEDIUM_KHR:
      Log_WarningPrint(message);
      break;
    case GL_DEBUG_SEVERITY_LOW_KHR:
      Log_InfoPrintf(message);
      break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
      // Log_DebugPrint(message);
      break;
  }
}

bool OpenGLHostDisplay::CreateGLContext()
{
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  m_gl_context = SDL_GL_CreateContext(m_window);
  if (!m_gl_context || SDL_GL_MakeCurrent(m_window, m_gl_context) != 0 || !gladLoadGL())
  {
    Panic("Failed to create GL context");
    return false;
  }

#if 1
  if (GLAD_GL_KHR_debug)
  {
    glad_glDebugMessageCallbackKHR(GLDebugCallback, nullptr);
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  }
#endif

  SDL_GL_SetSwapInterval(0);
  return true;
}

bool OpenGLHostDisplay::CreateImGuiContext()
{
  if (!ImGui_ImplSDL2_InitForOpenGL(m_window, m_gl_context) || !ImGui_ImplOpenGL3_Init())
    return false;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame(m_window);
  return true;
}

bool OpenGLHostDisplay::CreateGLResources()
{
  static constexpr char fullscreen_quad_vertex_shader[] = R"(
#version 330 core

out vec2 v_tex0;

void main()
{
  v_tex0 = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(v_tex0 * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);
  gl_Position.y = -gl_Position.y;
}
)";

  static constexpr char display_fragment_shader[] = R"(
#version 330 core

uniform sampler2D samp0;
uniform vec4 u_src_rect;

in vec2 v_tex0;
out vec4 o_col0;

void main()
{
  vec2 coords = u_src_rect.xy + v_tex0 * u_src_rect.zw;
  o_col0 = texture(samp0, coords);
}
)";

  if (!m_display_program.Compile(fullscreen_quad_vertex_shader, display_fragment_shader))
    return false;

  m_display_program.BindFragData(0, "o_col0");
  if (!m_display_program.Link())
    return false;

  m_display_program.Bind();
  m_display_program.RegisterUniform("u_src_rect");
  m_display_program.RegisterUniform("samp0");
  m_display_program.Uniform1i(1, 0);

  glGenVertexArrays(1, &m_display_vao);

  m_app_icon_texture =
    std::make_unique<GL::Texture>(APP_ICON_WIDTH, APP_ICON_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, APP_ICON_DATA, true);

  // samplers
  glGenSamplers(1, &m_display_nearest_sampler);
  glSamplerParameteri(m_display_nearest_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glSamplerParameteri(m_display_nearest_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenSamplers(1, &m_display_linear_sampler);
  glSamplerParameteri(m_display_linear_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(m_display_linear_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  return true;
}

std::unique_ptr<HostDisplay> OpenGLHostDisplay::Create(SDL_Window* window)
{
  std::unique_ptr<OpenGLHostDisplay> display = std::make_unique<OpenGLHostDisplay>(window);
  if (!display->CreateGLContext() || !display->CreateImGuiContext() || !display->CreateGLResources())
    return nullptr;

  return display;
}

void OpenGLHostDisplay::Render()
{
  glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  RenderDisplay();

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  SDL_GL_SwapWindow(m_window);

  ImGui_ImplSDL2_NewFrame(m_window);
  ImGui_ImplOpenGL3_NewFrame();

  GL::Program::ResetLastProgram();
}

void OpenGLHostDisplay::RenderDisplay()
{
  if (!m_display_texture_id)
    return;

  // - 20 for main menu padding
  const auto [vp_left, vp_top, vp_width, vp_height] =
    CalculateDrawRect(m_window_width, std::max(m_window_height - 20, 1), m_display_aspect_ratio);

  glViewport(vp_left, m_window_height - (20 + vp_top) - vp_height, vp_width, vp_height);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_FALSE);
  m_display_program.Bind();
  m_display_program.Uniform4f(0, static_cast<float>(m_display_offset_x) / static_cast<float>(m_display_texture_width),
                              static_cast<float>(m_display_offset_y) / static_cast<float>(m_display_texture_height),
                              static_cast<float>(m_display_width) / static_cast<float>(m_display_texture_width),
                              static_cast<float>(m_display_height) / static_cast<float>(m_display_texture_height));
  glBindTexture(GL_TEXTURE_2D, m_display_texture_id);
  glBindSampler(0, m_display_linear_filtering ? m_display_linear_sampler : m_display_nearest_sampler);
  glBindVertexArray(m_display_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindSampler(0, 0);
}
