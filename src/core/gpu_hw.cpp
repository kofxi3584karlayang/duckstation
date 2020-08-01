#include "gpu_hw.h"
#include "common/assert.h"
#include "common/log.h"
#include "common/state_wrapper.h"
#include "cpu_core.h"
#include "pgxp.h"
#include "settings.h"
#include "system.h"
#include <imgui.h>
#include <sstream>
Log_SetChannel(GPU_HW);

GPU_HW::GPU_HW() : GPU()
{
  m_vram_ptr = m_vram_shadow.data();
}

GPU_HW::~GPU_HW() = default;

bool GPU_HW::IsHardwareRenderer() const
{
  return true;
}

bool GPU_HW::Initialize(HostDisplay* host_display)
{
  if (!GPU::Initialize(host_display))
    return false;

  m_resolution_scale = g_settings.gpu_resolution_scale;
  m_render_api = host_display->GetRenderAPI();
  m_true_color = g_settings.gpu_true_color;
  m_scaled_dithering = g_settings.gpu_scaled_dithering;
  m_texture_filtering = g_settings.gpu_texture_filtering;
  if (m_resolution_scale < 1 || m_resolution_scale > m_max_resolution_scale)
  {
    g_host_interface->AddFormattedOSDMessage(5.0f, "Invalid resolution scale %ux specified. Maximum is %u.",
                                             m_resolution_scale, m_max_resolution_scale);
    m_resolution_scale = std::clamp<u32>(m_resolution_scale, 1u, m_max_resolution_scale);
  }

  PrintSettingsToLog();
  return true;
}

void GPU_HW::Reset()
{
  GPU::Reset();

  m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;

  m_vram_shadow.fill(0);

  m_batch = {};
  m_batch_ubo_data = {};
  m_batch_ubo_dirty = true;
  m_current_depth = 1;

  SetFullVRAMDirtyRectangle();
}

bool GPU_HW::DoState(StateWrapper& sw)
{
  if (!GPU::DoState(sw))
    return false;

  // invalidate the whole VRAM read texture when loading state
  if (sw.IsReading())
  {
    m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;
    SetFullVRAMDirtyRectangle();
    ResetBatchVertexDepth();
  }

  return true;
}

void GPU_HW::UpdateSettings()
{
  GPU::UpdateSettings();

  m_resolution_scale = std::clamp<u32>(g_settings.gpu_resolution_scale, 1, m_max_resolution_scale);
  m_true_color = g_settings.gpu_true_color;
  m_scaled_dithering = g_settings.gpu_scaled_dithering;
  m_texture_filtering = g_settings.gpu_texture_filtering;
  PrintSettingsToLog();
}

void GPU_HW::PrintSettingsToLog()
{
  Log_InfoPrintf("Resolution Scale: %u (%ux%u), maximum %u", m_resolution_scale, VRAM_WIDTH * m_resolution_scale,
                 VRAM_HEIGHT * m_resolution_scale, m_max_resolution_scale);
  Log_InfoPrintf("Dithering: %s%s", m_true_color ? "Disabled" : "Enabled",
                 (!m_true_color && m_scaled_dithering) ? " (Scaled)" : "");
  Log_InfoPrintf("Texture Filtering: %s", m_texture_filtering ? "Enabled" : "Disabled");
  Log_InfoPrintf("Dual-source blending: %s", m_supports_dual_source_blend ? "Supported" : "Not supported");
}

void GPU_HW::UpdateVRAMReadTexture()
{
  m_renderer_stats.num_vram_read_texture_updates++;
  ClearVRAMDirtyRectangle();
}

void GPU_HW::HandleFlippedQuadTextureCoordinates(BatchVertex* vertices)
{
  // Taken from beetle-psx gpu_polygon.cpp
  // For X/Y flipped 2D sprites, PSX games rely on a very specific rasterization behavior. If U or V is decreasing in X
  // or Y, and we use the provided U/V as is, we will sample the wrong texel as interpolation covers an entire pixel,
  // while PSX samples its interpolation essentially in the top-left corner and splats that interpolant across the
  // entire pixel. While we could emulate this reasonably well in native resolution by shifting our vertex coords by
  // 0.5, this breaks in upscaling scenarios, because we have several samples per native sample and we need NN rules to
  // hit the same UV every time. One approach here is to use interpolate at offset or similar tricks to generalize the
  // PSX interpolation patterns, but the problem is that vertices sharing an edge will no longer see the same UV (due to
  // different plane derivatives), we end up sampling outside the intended boundary and artifacts are inevitable, so the
  // only case where we can apply this fixup is for "sprites" or similar which should not share edges, which leads to
  // this unfortunate code below.

  // It might be faster to do more direct checking here, but the code below handles primitives in any order and
  // orientation, and is far more SIMD-friendly if needed.
  const float abx = vertices[1].x - vertices[0].x;
  const float aby = vertices[1].y - vertices[0].y;
  const float bcx = vertices[2].x - vertices[1].x;
  const float bcy = vertices[2].y - vertices[1].y;
  const float cax = vertices[0].x - vertices[2].x;
  const float cay = vertices[0].y - vertices[2].y;

  // Compute static derivatives, just assume W is uniform across the primitive and that the plane equation remains the
  // same across the quad. (which it is, there is no Z.. yet).
  const float dudx = -aby * static_cast<float>(vertices[2].u) - bcy * static_cast<float>(vertices[0].u) -
                     cay * static_cast<float>(vertices[1].u);
  const float dvdx = -aby * static_cast<float>(vertices[2].v) - bcy * static_cast<float>(vertices[0].v) -
                     cay * static_cast<float>(vertices[1].v);
  const float dudy = +abx * static_cast<float>(vertices[2].u) + bcx * static_cast<float>(vertices[0].u) +
                     cax * static_cast<float>(vertices[1].u);
  const float dvdy = +abx * static_cast<float>(vertices[2].v) + bcx * static_cast<float>(vertices[0].v) +
                     cax * static_cast<float>(vertices[1].v);
  const float area = bcx * cay - bcy * cax;

  // Detect and reject any triangles with 0 size texture area
  const s32 texArea = (vertices[1].u - vertices[0].u) * (vertices[2].v - vertices[0].v) -
                      (vertices[2].u - vertices[0].u) * (vertices[1].v - vertices[0].v);

  // Shouldn't matter as degenerate primitives will be culled anyways.
  if (area == 0.0f && texArea == 0)
    return;

  // Use floats here as it'll be faster than integer divides.
  const float rcp_area = 1.0f / area;
  const float dudx_area = dudx * rcp_area;
  const float dudy_area = dudy * rcp_area;
  const float dvdx_area = dvdx * rcp_area;
  const float dvdy_area = dvdy * rcp_area;
  const bool neg_dudx = dudx_area < 0.0f;
  const bool neg_dudy = dudy_area < 0.0f;
  const bool neg_dvdx = dvdx_area < 0.0f;
  const bool neg_dvdy = dvdy_area < 0.0f;
  const bool zero_dudx = dudx_area == 0.0f;
  const bool zero_dudy = dudy_area == 0.0f;
  const bool zero_dvdx = dvdx_area == 0.0f;
  const bool zero_dvdy = dvdy_area == 0.0f;

  // If we have negative dU or dV in any direction, increment the U or V to work properly with nearest-neighbor in
  // this impl. If we don't have 1:1 pixel correspondence, this creates a slight "shift" in the sprite, but we
  // guarantee that we don't sample garbage at least. Overall, this is kinda hacky because there can be legitimate,
  // rare cases where 3D meshes hit this scenario, and a single texel offset can pop in, but this is way better than
  // having borked 2D overall.
  //
  // TODO: If perf becomes an issue, we can probably SIMD the 8 comparisons above,
  // create an 8-bit code, and use a LUT to get the offsets.
  // Case 1: U is decreasing in X, but no change in Y.
  // Case 2: U is decreasing in Y, but no change in X.
  // Case 3: V is decreasing in X, but no change in Y.
  // Case 4: V is decreasing in Y, but no change in X.
  if ((neg_dudx && zero_dudy) || (neg_dudy && zero_dudx))
  {
    vertices[0].u++;
    vertices[1].u++;
    vertices[2].u++;
    vertices[3].u++;
  }

  if ((neg_dvdx && zero_dvdy) || (neg_dvdy && zero_dvdx))
  {
    vertices[0].v++;
    vertices[1].v++;
    vertices[2].v++;
    vertices[3].v++;
  }
}

// The PlayStation GPU draws lines from start to end, inclusive. Or, more specifically, inclusive of the greatest delta
// in the x or y direction.
void GPU_HW::FixLineVertexCoordinates(s32& start_x, s32& start_y, s32& end_x, s32& end_y, s32 dx, s32 dy)
{
  // deliberately not else if to catch the equal case
  if (dx >= dy)
  {
    if (start_x > end_x)
      start_x++;
    else
      end_x++;
  }
  if (dx <= dy)
  {
    if (start_y > end_y)
      start_y++;
    else
      end_y++;
  }
}

void GPU_HW::LoadVertices()
{
  const RenderCommand rc{m_render_command.bits};
  const u32 texpage = ZeroExtend32(m_draw_mode.mode_reg.bits) | (ZeroExtend32(m_draw_mode.palette_reg) << 16);
  const float depth = GetCurrentNormalizedVertexDepth();

  if (m_GPUSTAT.check_mask_before_draw)
    m_current_depth++;

  switch (rc.primitive)
  {
    case Primitive::Polygon:
    {
      DebugAssert(GetBatchVertexSpace() >= (rc.quad_polygon ? 6u : 3u));

      const u32 first_color = rc.color_for_first_vertex;
      const bool shaded = rc.shading_enable;
      const bool textured = rc.texture_enable;
      const bool pgxp = g_settings.gpu_pgxp_enable;

      const u32 num_vertices = rc.quad_polygon ? 4 : 3;
      std::array<BatchVertex, 4> vertices;
      std::array<std::array<s32, 2>, 4> native_vertex_positions;
      bool valid_w = g_settings.gpu_pgxp_texture_correction;
      for (u32 i = 0; i < num_vertices; i++)
      {
        const u32 color = (shaded && i > 0) ? (FifoPop() & UINT32_C(0x00FFFFFF)) : first_color;
        const u64 maddr_and_pos = m_fifo.Pop();
        const VertexPosition vp{Truncate32(maddr_and_pos)};
        const u16 texcoord = textured ? Truncate16(FifoPop()) : 0;
        const s32 native_x = m_drawing_offset.x + vp.x;
        const s32 native_y = m_drawing_offset.y + vp.y;
        native_vertex_positions[i][0] = native_x;
        native_vertex_positions[i][1] = native_y;
        vertices[i].Set(static_cast<float>(native_x), static_cast<float>(native_y), depth, 1.0f, color, texpage,
                        texcoord);

        if (pgxp)
        {
          valid_w &=
            PGXP::GetPreciseVertex(Truncate32(maddr_and_pos >> 32), vp.bits, native_x, native_y, m_drawing_offset.x,
                                 m_drawing_offset.y, &vertices[i].x, &vertices[i].y, &vertices[i].w);
        }
      }
      if (!valid_w)
      {
        for (BatchVertex& v : vertices)
          v.w = 1.0f;
      }

      if (rc.quad_polygon && m_resolution_scale > 1)
        HandleFlippedQuadTextureCoordinates(vertices.data());

      if (!IsDrawingAreaIsValid())
        return;

      // Cull polygons which are too large.
      const s32 min_x_12 = std::min(native_vertex_positions[1][0], native_vertex_positions[2][0]);
      const s32 max_x_12 = std::max(native_vertex_positions[1][0], native_vertex_positions[2][0]);
      const s32 min_y_12 = std::min(native_vertex_positions[1][1], native_vertex_positions[2][1]);
      const s32 max_y_12 = std::max(native_vertex_positions[1][1], native_vertex_positions[2][1]);
      const s32 min_x = std::min(min_x_12, native_vertex_positions[0][0]);
      const s32 max_x = std::max(max_x_12, native_vertex_positions[0][0]);
      const s32 min_y = std::min(min_y_12, native_vertex_positions[0][1]);
      const s32 max_y = std::max(max_y_12, native_vertex_positions[0][1]);

      if ((max_x - min_x) >= MAX_PRIMITIVE_WIDTH || (max_y - min_y) >= MAX_PRIMITIVE_HEIGHT)
      {
        Log_DebugPrintf("Culling too-large polygon: %d,%d %d,%d %d,%d", native_vertex_positions[0][0],
                        native_vertex_positions[0][1], native_vertex_positions[1][0], native_vertex_positions[1][1],
                        native_vertex_positions[2][0], native_vertex_positions[2][1]);
      }
      else
      {
        const u32 clip_left = static_cast<u32>(std::clamp<s32>(min_x, m_drawing_area.left, m_drawing_area.right));
        const u32 clip_right = static_cast<u32>(std::clamp<s32>(max_x, m_drawing_area.left, m_drawing_area.right)) + 1u;
        const u32 clip_top = static_cast<u32>(std::clamp<s32>(min_y, m_drawing_area.top, m_drawing_area.bottom));
        const u32 clip_bottom =
          static_cast<u32>(std::clamp<s32>(max_y, m_drawing_area.top, m_drawing_area.bottom)) + 1u;

        m_vram_dirty_rect.Include(clip_left, clip_right, clip_top, clip_bottom);
        AddDrawTriangleTicks(clip_right - clip_left, clip_bottom - clip_top, rc.shading_enable, rc.texture_enable,
                             rc.transparency_enable);

        std::memcpy(m_batch_current_vertex_ptr, vertices.data(), sizeof(BatchVertex) * 3);
        m_batch_current_vertex_ptr += 3;
      }

      // quads
      if (rc.quad_polygon)
      {
        const s32 min_x_123 = std::min(min_x_12, native_vertex_positions[3][0]);
        const s32 max_x_123 = std::max(max_x_12, native_vertex_positions[3][0]);
        const s32 min_y_123 = std::min(min_y_12, native_vertex_positions[3][1]);
        const s32 max_y_123 = std::max(max_y_12, native_vertex_positions[3][1]);

        // Cull polygons which are too large.
        if ((max_x_123 - min_x_123) >= MAX_PRIMITIVE_WIDTH || (max_y_123 - min_y_123) >= MAX_PRIMITIVE_HEIGHT)
        {
          Log_DebugPrintf("Culling too-large polygon (quad second half): %d,%d %d,%d %d,%d",
                          native_vertex_positions[2][0], native_vertex_positions[2][1], native_vertex_positions[1][0],
                          native_vertex_positions[1][1], native_vertex_positions[0][0], native_vertex_positions[0][1]);
        }
        else
        {
          const u32 clip_left = static_cast<u32>(std::clamp<s32>(min_x_123, m_drawing_area.left, m_drawing_area.right));
          const u32 clip_right =
            static_cast<u32>(std::clamp<s32>(max_x_123, m_drawing_area.left, m_drawing_area.right)) + 1u;
          const u32 clip_top = static_cast<u32>(std::clamp<s32>(min_y_123, m_drawing_area.top, m_drawing_area.bottom));
          const u32 clip_bottom =
            static_cast<u32>(std::clamp<s32>(max_y_123, m_drawing_area.top, m_drawing_area.bottom)) + 1u;

          m_vram_dirty_rect.Include(clip_left, clip_right, clip_top, clip_bottom);
          AddDrawTriangleTicks(clip_right - clip_left, clip_bottom - clip_top, rc.shading_enable, rc.texture_enable,
                               rc.transparency_enable);

          AddVertex(vertices[2]);
          AddVertex(vertices[1]);
          AddVertex(vertices[3]);
        }
      }
    }
    break;

    case Primitive::Rectangle:
    {
      const u32 color = rc.color_for_first_vertex;
      const VertexPosition vp{FifoPop()};
      const s32 pos_x = TruncateVertexPosition(m_drawing_offset.x + vp.x);
      const s32 pos_y = TruncateVertexPosition(m_drawing_offset.y + vp.y);

      const auto [texcoord_x, texcoord_y] = UnpackTexcoord(rc.texture_enable ? Truncate16(FifoPop()) : 0);
      u16 orig_tex_left = ZeroExtend16(texcoord_x);
      u16 orig_tex_top = ZeroExtend16(texcoord_y);
      s32 rectangle_width;
      s32 rectangle_height;
      switch (rc.rectangle_size)
      {
        case DrawRectangleSize::R1x1:
          rectangle_width = 1;
          rectangle_height = 1;
          break;
        case DrawRectangleSize::R8x8:
          rectangle_width = 8;
          rectangle_height = 8;
          break;
        case DrawRectangleSize::R16x16:
          rectangle_width = 16;
          rectangle_height = 16;
          break;
        default:
        {
          const u32 width_and_height = FifoPop();
          rectangle_width = static_cast<s32>(width_and_height & VRAM_WIDTH_MASK);
          rectangle_height = static_cast<s32>((width_and_height >> 16) & VRAM_HEIGHT_MASK);

          if (rectangle_width >= MAX_PRIMITIVE_WIDTH || rectangle_height >= MAX_PRIMITIVE_HEIGHT)
          {
            Log_DebugPrintf("Culling too-large rectangle: %d,%d %dx%d", pos_x, pos_y, rectangle_width,
                            rectangle_height);
            return;
          }
        }
        break;
      }

      // we can split the rectangle up into potentially 8 quads
      DebugAssert(GetBatchVertexSpace() >= MAX_VERTICES_FOR_RECTANGLE);

      if (!IsDrawingAreaIsValid())
        return;

      // Split the rectangle into multiple quads if it's greater than 256x256, as the texture page should repeat.
      u16 tex_top = orig_tex_top;
      for (s32 y_offset = 0; y_offset < rectangle_height;)
      {
        const s32 quad_height = std::min<s32>(rectangle_height - y_offset, TEXTURE_PAGE_WIDTH - tex_top);
        const float quad_start_y = static_cast<float>(pos_y + y_offset);
        const float quad_end_y = quad_start_y + static_cast<float>(quad_height);
        const u16 tex_bottom = tex_top + static_cast<u16>(quad_height);

        u16 tex_left = orig_tex_left;
        for (s32 x_offset = 0; x_offset < rectangle_width;)
        {
          const s32 quad_width = std::min<s32>(rectangle_width - x_offset, TEXTURE_PAGE_HEIGHT - tex_left);
          const float quad_start_x = static_cast<float>(pos_x + x_offset);
          const float quad_end_x = quad_start_x + static_cast<float>(quad_width);
          const u16 tex_right = tex_left + static_cast<u16>(quad_width);

          AddNewVertex(quad_start_x, quad_start_y, depth, 1.0f, color, texpage, tex_left, tex_top);
          AddNewVertex(quad_end_x, quad_start_y, depth, 1.0f, color, texpage, tex_right, tex_top);
          AddNewVertex(quad_start_x, quad_end_y, depth, 1.0f, color, texpage, tex_left, tex_bottom);

          AddNewVertex(quad_start_x, quad_end_y, depth, 1.0f, color, texpage, tex_left, tex_bottom);
          AddNewVertex(quad_end_x, quad_start_y, depth, 1.0f, color, texpage, tex_right, tex_top);
          AddNewVertex(quad_end_x, quad_end_y, depth, 1.0f, color, texpage, tex_right, tex_bottom);

          x_offset += quad_width;
          tex_left = 0;
        }

        y_offset += quad_height;
        tex_top = 0;
      }

      const u32 clip_left = static_cast<u32>(std::clamp<s32>(pos_x, m_drawing_area.left, m_drawing_area.right));
      const u32 clip_right =
        static_cast<u32>(std::clamp<s32>(pos_x + rectangle_width, m_drawing_area.left, m_drawing_area.right)) + 1u;
      const u32 clip_top = static_cast<u32>(std::clamp<s32>(pos_y, m_drawing_area.top, m_drawing_area.bottom));
      const u32 clip_bottom =
        static_cast<u32>(std::clamp<s32>(pos_y + rectangle_height, m_drawing_area.top, m_drawing_area.bottom)) + 1u;

      m_vram_dirty_rect.Include(clip_left, clip_right, clip_top, clip_bottom);
      AddDrawRectangleTicks(clip_right - clip_left, clip_bottom - clip_top, rc.texture_enable, rc.transparency_enable);
    }
    break;

    case Primitive::Line:
    {
      if (!rc.polyline)
      {
        DebugAssert(GetBatchVertexSpace() >= 2);

        u32 color0, color1;
        VertexPosition pos0, pos1;
        if (rc.shading_enable)
        {
          color0 = rc.color_for_first_vertex;
          pos0.bits = FifoPop();
          color1 = FifoPop() & UINT32_C(0x00FFFFFF);
          pos1.bits = FifoPop();
        }
        else
        {
          color0 = color1 = rc.color_for_first_vertex;
          pos0.bits = FifoPop();
          pos1.bits = FifoPop();
        }

        if (!IsDrawingAreaIsValid())
          return;

        s32 start_x = pos0.x + m_drawing_offset.x;
        s32 start_y = pos0.y + m_drawing_offset.y;
        s32 end_x = pos1.x + m_drawing_offset.x;
        s32 end_y = pos1.y + m_drawing_offset.y;

        const s32 min_x = std::min(start_x, end_x);
        const s32 max_x = std::max(start_x, end_x);
        const s32 min_y = std::min(start_y, end_y);
        const s32 max_y = std::max(start_y, end_y);
        const s32 dx = max_x - min_x;
        const s32 dy = max_y - min_y;
        if (dx >= MAX_PRIMITIVE_WIDTH || dy >= MAX_PRIMITIVE_HEIGHT)
        {
          Log_DebugPrintf("Culling too-large line: %d,%d - %d,%d", start_x, start_y, end_x, end_y);
          return;
        }

        FixLineVertexCoordinates(start_x, start_y, end_x, end_y, dx, dy);
        AddNewVertex(static_cast<float>(start_x), static_cast<float>(start_y), depth, 1.0f, color0, 0,
                     static_cast<u16>(0));
        AddNewVertex(static_cast<float>(end_x), static_cast<float>(end_y), depth, 1.0f, color1, 0, static_cast<u16>(0));

        const u32 clip_left = static_cast<u32>(std::clamp<s32>(min_x, m_drawing_area.left, m_drawing_area.left));
        const u32 clip_right = static_cast<u32>(std::clamp<s32>(max_x, m_drawing_area.left, m_drawing_area.right)) + 1u;
        const u32 clip_top = static_cast<u32>(std::clamp<s32>(min_y, m_drawing_area.top, m_drawing_area.bottom));
        const u32 clip_bottom =
          static_cast<u32>(std::clamp<s32>(max_y, m_drawing_area.top, m_drawing_area.bottom)) + 1u;

        m_vram_dirty_rect.Include(clip_left, clip_right, clip_top, clip_bottom);
        AddDrawLineTicks(clip_right - clip_left, clip_bottom - clip_top, rc.shading_enable);
      }
      else
      {
        // Multiply by two because we don't use line strips.
        const u32 num_vertices = GetPolyLineVertexCount();
        DebugAssert(GetBatchVertexSpace() >= (num_vertices * 2));

        if (!IsDrawingAreaIsValid())
          return;

        const u32 first_color = rc.color_for_first_vertex;
        const bool shaded = rc.shading_enable;

        s32 last_x, last_y;
        u32 last_color;
        u32 buffer_pos = 0;
        for (u32 i = 0; i < num_vertices; i++)
        {
          const u32 color = (shaded && i > 0) ? (m_blit_buffer[buffer_pos++] & UINT32_C(0x00FFFFFF)) : first_color;
          const VertexPosition vp{m_blit_buffer[buffer_pos++]};
          const s32 x = m_drawing_offset.x + vp.x;
          const s32 y = m_drawing_offset.y + vp.y;

          if (i > 0)
          {
            const s32 min_x = std::min(last_x, x);
            const s32 max_x = std::max(last_x, x);
            const s32 min_y = std::min(last_y, y);
            const s32 max_y = std::max(last_y, y);
            const s32 dx = max_x - min_x;
            const s32 dy = max_y - min_y;

            if (dx >= MAX_PRIMITIVE_WIDTH || dy >= MAX_PRIMITIVE_HEIGHT)
            {
              Log_DebugPrintf("Culling too-large line: %d,%d - %d,%d", last_x, last_y, x, y);
            }
            else
            {
              s32 start_x = last_x, start_y = last_y;
              s32 end_x = x, end_y = y;
              FixLineVertexCoordinates(start_x, start_y, end_x, end_y, dx, dy);
              AddNewVertex(static_cast<float>(start_x), static_cast<float>(start_y), depth, 1.0f, last_color, 0,
                           static_cast<u16>(0));
              AddNewVertex(static_cast<float>(end_x), static_cast<float>(end_y), depth, 1.0f, color, 0,
                           static_cast<u16>(0));

              const u32 clip_left = static_cast<u32>(std::clamp<s32>(min_x, m_drawing_area.left, m_drawing_area.left));
              const u32 clip_right =
                static_cast<u32>(std::clamp<s32>(max_x, m_drawing_area.left, m_drawing_area.right)) + 1u;
              const u32 clip_top = static_cast<u32>(std::clamp<s32>(min_y, m_drawing_area.top, m_drawing_area.bottom));
              const u32 clip_bottom =
                static_cast<u32>(std::clamp<s32>(max_y, m_drawing_area.top, m_drawing_area.bottom)) + 1u;

              m_vram_dirty_rect.Include(clip_left, clip_right, clip_top, clip_bottom);
              AddDrawLineTicks(clip_right - clip_left, clip_bottom - clip_top, rc.shading_enable);
            }
          }

          last_x = x;
          last_y = y;
          last_color = color;
        }
      }
    }
    break;

    default:
      UnreachableCode();
      break;
  }
}

void GPU_HW::CalcScissorRect(int* left, int* top, int* right, int* bottom)
{
  *left = m_drawing_area.left * m_resolution_scale;
  *right = std::max<u32>((m_drawing_area.right + 1) * m_resolution_scale, *left + 1);
  *top = m_drawing_area.top * m_resolution_scale;
  *bottom = std::max<u32>((m_drawing_area.bottom + 1) * m_resolution_scale, *top + 1);
}

GPU_HW::VRAMFillUBOData GPU_HW::GetVRAMFillUBOData(u32 x, u32 y, u32 width, u32 height, u32 color) const
{
  // drop precision unless true colour is enabled
  if (!m_true_color)
    color = RGBA5551ToRGBA8888(RGBA8888ToRGBA5551(color));

  VRAMFillUBOData uniforms;
  std::tie(uniforms.u_fill_color[0], uniforms.u_fill_color[1], uniforms.u_fill_color[2], uniforms.u_fill_color[3]) =
    RGBA8ToFloat(color);
  uniforms.u_interlaced_displayed_field = GetActiveLineLSB();
  return uniforms;
}

Common::Rectangle<u32> GPU_HW::GetVRAMTransferBounds(u32 x, u32 y, u32 width, u32 height) const
{
  Common::Rectangle<u32> out_rc = Common::Rectangle<u32>::FromExtents(x % VRAM_WIDTH, y % VRAM_HEIGHT, width, height);
  if (out_rc.right > VRAM_WIDTH)
  {
    out_rc.left = 0;
    out_rc.right = VRAM_WIDTH;
  }
  if (out_rc.bottom > VRAM_HEIGHT)
  {
    out_rc.top = 0;
    out_rc.bottom = VRAM_HEIGHT;
  }
  return out_rc;
}

GPU_HW::VRAMWriteUBOData GPU_HW::GetVRAMWriteUBOData(u32 x, u32 y, u32 width, u32 height, u32 buffer_offset) const
{
  const VRAMWriteUBOData uniforms = {(x % VRAM_WIDTH),
                                     (y % VRAM_HEIGHT),
                                     ((x + width) % VRAM_WIDTH),
                                     ((y + height) % VRAM_HEIGHT),
                                     width,
                                     height,
                                     buffer_offset,
                                     m_GPUSTAT.set_mask_while_drawing ? 0x8000u : 0x00,
                                     GetCurrentNormalizedVertexDepth()};
  return uniforms;
}

bool GPU_HW::UseVRAMCopyShader(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height) const
{
  // masking enabled, oversized, or overlapping
  return (m_GPUSTAT.IsMaskingEnabled() || ((src_x % VRAM_WIDTH) + width) > VRAM_WIDTH ||
          ((src_y % VRAM_HEIGHT) + height) > VRAM_HEIGHT || ((dst_x % VRAM_WIDTH) + width) > VRAM_WIDTH ||
          ((dst_y % VRAM_HEIGHT) + height) > VRAM_HEIGHT ||
          Common::Rectangle<u32>::FromExtents(src_x, src_y, width, height)
            .Intersects(Common::Rectangle<u32>::FromExtents(dst_x, dst_y, width, height)));
}

GPU_HW::VRAMCopyUBOData GPU_HW::GetVRAMCopyUBOData(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width,
                                                   u32 height) const
{
  const VRAMCopyUBOData uniforms = {(src_x % VRAM_WIDTH) * m_resolution_scale,
                                    (src_y % VRAM_HEIGHT) * m_resolution_scale,
                                    (dst_x % VRAM_WIDTH) * m_resolution_scale,
                                    (dst_y % VRAM_HEIGHT) * m_resolution_scale,
                                    ((dst_x + width) % VRAM_WIDTH) * m_resolution_scale,
                                    ((dst_y + height) % VRAM_HEIGHT) * m_resolution_scale,
                                    width * m_resolution_scale,
                                    height * m_resolution_scale,
                                    m_GPUSTAT.set_mask_while_drawing ? 1u : 0u,
                                    GetCurrentNormalizedVertexDepth()};

  return uniforms;
}

GPU_HW::BatchPrimitive GPU_HW::GetPrimitiveForCommand(RenderCommand rc)
{
  if (rc.primitive == Primitive::Line)
    return BatchPrimitive::Lines;
  else
    return BatchPrimitive::Triangles;
}

void GPU_HW::IncludeVRAMDityRectangle(const Common::Rectangle<u32>& rect)
{
  m_vram_dirty_rect.Include(rect);

  // the vram area can include the texture page, but the game can leave it as-is. in this case, set it as dirty so the
  // shadow texture is updated
  if (!m_draw_mode.IsTexturePageChanged() &&
      (m_draw_mode.GetTexturePageRectangle().Intersects(rect) ||
       (m_draw_mode.IsUsingPalette() && m_draw_mode.GetTexturePaletteRectangle().Intersects(rect))))
  {
    m_draw_mode.SetTexturePageChanged();
  }
}

void GPU_HW::EnsureVertexBufferSpace(u32 required_vertices)
{
  if (m_batch_current_vertex_ptr)
  {
    if (GetBatchVertexSpace() >= required_vertices)
      return;

    FlushRender();
  }

  MapBatchVertexPointer(required_vertices);
}

void GPU_HW::EnsureVertexBufferSpaceForCurrentCommand()
{
  u32 required_vertices;
  switch (m_render_command.primitive)
  {
    case Primitive::Polygon:
      required_vertices = m_render_command.quad_polygon ? 6 : 3;
      break;
    case Primitive::Rectangle:
      required_vertices = MAX_VERTICES_FOR_RECTANGLE;
      break;
    case Primitive::Line:
    default:
      required_vertices = m_render_command.polyline ? (GetPolyLineVertexCount() * 2u) : 2u;
      break;
  }

  // can we fit these vertices in the current depth buffer range?
  if ((m_current_depth + required_vertices) > MAX_BATCH_VERTEX_COUNTER_IDS)
  {
    // implies FlushRender()
    ResetBatchVertexDepth();
  }
  else if (m_batch_current_vertex_ptr)
  {
    if (GetBatchVertexSpace() >= required_vertices)
      return;

    FlushRender();
  }

  MapBatchVertexPointer(required_vertices);
}

void GPU_HW::ResetBatchVertexDepth()
{
  Log_PerfPrint("Resetting batch vertex depth");
  FlushRender();
  UpdateDepthBufferFromMaskBit();

  m_current_depth = 1;
}

void GPU_HW::FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color)
{
  IncludeVRAMDityRectangle(
    Common::Rectangle<u32>::FromExtents(x, y, width, height).Clamped(0, 0, VRAM_WIDTH, VRAM_HEIGHT));
}

void GPU_HW::UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data)
{
  DebugAssert((x + width) <= VRAM_WIDTH && (y + height) <= VRAM_HEIGHT);
  IncludeVRAMDityRectangle(Common::Rectangle<u32>::FromExtents(x, y, width, height));

  if (m_GPUSTAT.check_mask_before_draw)
  {
    // set new vertex counter since we want this to take into consideration previous masked pixels
    m_current_depth++;
  }
}

void GPU_HW::CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height)
{
  IncludeVRAMDityRectangle(
    Common::Rectangle<u32>::FromExtents(dst_x, dst_y, width, height).Clamped(0, 0, VRAM_WIDTH, VRAM_HEIGHT));

  if (m_GPUSTAT.check_mask_before_draw)
  {
    // set new vertex counter since we want this to take into consideration previous masked pixels
    m_current_depth++;
  }
}

void GPU_HW::DispatchRenderCommand()
{
  const RenderCommand rc{m_render_command.bits};

  TextureMode texture_mode;
  if (rc.IsTexturingEnabled())
  {
    // texture page changed - check that the new page doesn't intersect the drawing area
    if (m_draw_mode.IsTexturePageChanged())
    {
      m_draw_mode.ClearTexturePageChangedFlag();
      if (m_vram_dirty_rect.Valid() &&
          (m_draw_mode.GetTexturePageRectangle().Intersects(m_vram_dirty_rect) ||
           (m_draw_mode.IsUsingPalette() && m_draw_mode.GetTexturePaletteRectangle().Intersects(m_vram_dirty_rect))))
      {
        // Log_DevPrintf("Invalidating VRAM read cache due to drawing area overlap");
        if (!IsFlushed())
          FlushRender();

        UpdateVRAMReadTexture();
      }
    }

    texture_mode = m_draw_mode.GetTextureMode();
    if (rc.raw_texture_enable)
    {
      texture_mode =
        static_cast<TextureMode>(static_cast<u8>(texture_mode) | static_cast<u8>(TextureMode::RawTextureBit));
    }
  }
  else
  {
    texture_mode = TextureMode::Disabled;
  }

  // has any state changed which requires a new batch?
  const TransparencyMode transparency_mode =
    rc.transparency_enable ? m_draw_mode.GetTransparencyMode() : TransparencyMode::Disabled;
  const BatchPrimitive rc_primitive = GetPrimitiveForCommand(rc);
  const bool dithering_enable = (!m_true_color && rc.IsDitheringEnabled()) ? m_GPUSTAT.dither_enable : false;
  if (m_batch.texture_mode != texture_mode || m_batch.transparency_mode != transparency_mode ||
      m_batch.primitive != rc_primitive || dithering_enable != m_batch.dithering)
  {
    FlushRender();
  }

  EnsureVertexBufferSpaceForCurrentCommand();

  // transparency mode change
  if (m_batch.transparency_mode != transparency_mode && transparency_mode != TransparencyMode::Disabled)
  {
    static constexpr float transparent_alpha[4][2] = {{0.5f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {0.25f, 1.0f}};
    m_batch_ubo_data.u_src_alpha_factor = transparent_alpha[static_cast<u32>(transparency_mode)][0];
    m_batch_ubo_data.u_dst_alpha_factor = transparent_alpha[static_cast<u32>(transparency_mode)][1];
    m_batch_ubo_dirty = true;
  }

  if (m_batch.check_mask_before_draw != m_GPUSTAT.check_mask_before_draw ||
      m_batch.set_mask_while_drawing != m_GPUSTAT.set_mask_while_drawing)
  {
    m_batch.check_mask_before_draw = m_GPUSTAT.check_mask_before_draw;
    m_batch.set_mask_while_drawing = m_GPUSTAT.set_mask_while_drawing;
    m_batch_ubo_data.u_set_mask_while_drawing = BoolToUInt32(m_batch.set_mask_while_drawing);
    m_batch_ubo_dirty = true;
  }

  m_batch.interlacing = IsInterlacedRenderingEnabled();
  if (m_batch.interlacing)
  {
    const u32 displayed_field = GetActiveLineLSB();
    m_batch_ubo_dirty |= (m_batch_ubo_data.u_interlaced_displayed_field != displayed_field);
    m_batch_ubo_data.u_interlaced_displayed_field = displayed_field;
  }

  // update state
  m_batch.primitive = rc_primitive;
  m_batch.texture_mode = texture_mode;
  m_batch.transparency_mode = transparency_mode;
  m_batch.dithering = dithering_enable;

  if (m_draw_mode.IsTextureWindowChanged())
  {
    m_draw_mode.ClearTextureWindowChangedFlag();

    m_batch_ubo_data.u_texture_window_mask[0] = ZeroExtend32(m_draw_mode.texture_window_mask_x);
    m_batch_ubo_data.u_texture_window_mask[1] = ZeroExtend32(m_draw_mode.texture_window_mask_y);
    m_batch_ubo_data.u_texture_window_offset[0] = ZeroExtend32(m_draw_mode.texture_window_offset_x);
    m_batch_ubo_data.u_texture_window_offset[1] = ZeroExtend32(m_draw_mode.texture_window_offset_y);
    m_batch_ubo_dirty = true;
  }

  LoadVertices();
}

void GPU_HW::FlushRender()
{
  if (!m_batch_current_vertex_ptr)
    return;

  const u32 vertex_count = GetBatchVertexCount();
  UnmapBatchVertexPointer(vertex_count);

  if (vertex_count == 0)
    return;

  if (m_drawing_area_changed)
  {
    m_drawing_area_changed = false;
    SetScissorFromDrawingArea();
  }

  if (m_batch_ubo_dirty)
  {
    UploadUniformBuffer(&m_batch_ubo_data, sizeof(m_batch_ubo_data));
    m_batch_ubo_dirty = false;
  }

  if (m_batch.NeedsTwoPassRendering())
  {
    m_renderer_stats.num_batches += 2;
    DrawBatchVertices(BatchRenderMode::OnlyTransparent, m_batch_base_vertex, vertex_count);
    DrawBatchVertices(BatchRenderMode::OnlyOpaque, m_batch_base_vertex, vertex_count);
  }
  else
  {
    m_renderer_stats.num_batches++;
    DrawBatchVertices(m_batch.GetRenderMode(), m_batch_base_vertex, vertex_count);
  }
}

void GPU_HW::DrawRendererStats(bool is_idle_frame)
{
  if (!is_idle_frame)
  {
    m_last_renderer_stats = m_renderer_stats;
    m_renderer_stats = {};
  }

  if (ImGui::CollapsingHeader("Renderer Statistics", ImGuiTreeNodeFlags_DefaultOpen))
  {
    const auto& stats = m_last_renderer_stats;

    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, 200.0f * ImGui::GetIO().DisplayFramebufferScale.x);

    ImGui::TextUnformatted("Batches Drawn:");
    ImGui::NextColumn();
    ImGui::Text("%u", stats.num_batches);
    ImGui::NextColumn();

    ImGui::TextUnformatted("VRAM Read Texture Updates:");
    ImGui::NextColumn();
    ImGui::Text("%u", stats.num_vram_read_texture_updates);
    ImGui::NextColumn();

    ImGui::TextUnformatted("Uniform Buffer Updates: ");
    ImGui::NextColumn();
    ImGui::Text("%u", stats.num_uniform_buffer_updates);
    ImGui::NextColumn();

    ImGui::Columns(1);
  }
}
