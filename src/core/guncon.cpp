// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "guncon.h"
#include "gpu.h"
#include "host.h"
#include "system.h"

#include "util/input_manager.h"
#include "util/state_wrapper.h"

#include "common/assert.h"
#include "common/path.h"

#include <array>

#ifdef _DEBUG
#include "common/log.h"
Log_SetChannel(GunCon);
#endif

static constexpr std::array<u8, static_cast<size_t>(GunCon::Button::Count)> s_button_indices = {{13, 3, 14}};

GunCon::GunCon(u32 index) : Controller(index)
{
}

GunCon::~GunCon() = default;

ControllerType GunCon::GetType() const
{
  return ControllerType::GunCon;
}

void GunCon::Reset()
{
  m_transfer_state = TransferState::Idle;
}

bool GunCon::DoState(StateWrapper& sw, bool apply_input_state)
{
  if (!Controller::DoState(sw, apply_input_state))
    return false;

  u16 button_state = m_button_state;
  u16 position_x = m_position_x;
  u16 position_y = m_position_y;
  sw.Do(&button_state);
  sw.Do(&position_x);
  sw.Do(&position_y);
  if (apply_input_state)
  {
    m_button_state = button_state;
    m_position_x = position_x;
    m_position_y = position_y;
  }

  sw.Do(&m_transfer_state);
  return true;
}

float GunCon::GetBindState(u32 index) const
{
  if (index >= s_button_indices.size())
    return 0.0f;

  const u32 bit = s_button_indices[index];
  return static_cast<float>(((m_button_state >> bit) & 1u) ^ 1u);
}

void GunCon::SetBindState(u32 index, float value)
{
  const bool pressed = (value >= 0.5f);
  if (index == static_cast<u32>(Button::ShootOffscreen))
  {
    if (m_shoot_offscreen != pressed)
    {
      m_shoot_offscreen = pressed;
      SetBindState(static_cast<u32>(Button::Trigger), pressed);
    }

    return;
  }

  if (pressed)
    m_button_state &= ~(u16(1) << s_button_indices[static_cast<u8>(index)]);
  else
    m_button_state |= u16(1) << s_button_indices[static_cast<u8>(index)];
}

void GunCon::ResetTransferState()
{
  m_transfer_state = TransferState::Idle;
}

bool GunCon::Transfer(const u8 data_in, u8* data_out)
{
  static constexpr u16 ID = 0x5A63;

  switch (m_transfer_state)
  {
    case TransferState::Idle:
    {
      *data_out = 0xFF;

      if (data_in == 0x01)
      {
        m_transfer_state = TransferState::Ready;
        return true;
      }
      return false;
    }

    case TransferState::Ready:
    {
      if (data_in == 0x42)
      {
        *data_out = Truncate8(ID);
        m_transfer_state = TransferState::IDMSB;
        return true;
      }

      *data_out = 0xFF;
      return false;
    }

    case TransferState::IDMSB:
    {
      *data_out = Truncate8(ID >> 8);
      m_transfer_state = TransferState::ButtonsLSB;
      return true;
    }

    case TransferState::ButtonsLSB:
    {
      *data_out = Truncate8(m_button_state);
      m_transfer_state = TransferState::ButtonsMSB;
      return true;
    }

    case TransferState::ButtonsMSB:
    {
      *data_out = Truncate8(m_button_state >> 8);
      m_transfer_state = TransferState::XLSB;
      return true;
    }

    case TransferState::XLSB:
    {
      UpdatePosition();
      *data_out = Truncate8(m_position_x);
      m_transfer_state = TransferState::XMSB;
      return true;
    }

    case TransferState::XMSB:
    {
      *data_out = Truncate8(m_position_x >> 8);
      m_transfer_state = TransferState::YLSB;
      return true;
    }

    case TransferState::YLSB:
    {
      *data_out = Truncate8(m_position_y);
      m_transfer_state = TransferState::YMSB;
      return true;
    }

    case TransferState::YMSB:
    {
      *data_out = Truncate8(m_position_y >> 8);
      m_transfer_state = TransferState::Idle;
      return false;
    }

    default:
      UnreachableCode();
  }
}

void GunCon::UpdatePosition()
{
  // get screen coordinates
  const auto& [fmouse_x, fmouse_y] = InputManager::GetPointerAbsolutePosition(0);
  const s32 mouse_x = static_cast<s32>(fmouse_x);
  const s32 mouse_y = static_cast<s32>(fmouse_y);

  // are we within the active display area?
  u32 tick, line;
  if (mouse_x < 0 || mouse_y < 0 ||
      !g_gpu->ConvertScreenCoordinatesToBeamTicksAndLines(mouse_x, mouse_y, m_x_scale, &tick, &line) ||
      m_shoot_offscreen)
  {
    Log_DebugPrintf("Lightgun out of range for window coordinates %d,%d", mouse_x, mouse_y);
    m_position_x = 0x01;
    m_position_y = 0x0A;
    return;
  }

  // 8MHz units for X = 44100*768*11/7 = 53222400 / 8000000 = 6.6528
  const double divider = static_cast<double>(g_gpu->GetCRTCFrequency()) / 8000000.0;
  m_position_x = static_cast<u16>(static_cast<float>(tick) / static_cast<float>(divider));
  m_position_y = static_cast<u16>(line);
  Log_DebugPrintf("Lightgun window coordinates %d,%d -> tick %u line %u 8mhz ticks %u", mouse_x, mouse_y, tick, line,
                  m_position_x);
}

std::unique_ptr<GunCon> GunCon::Create(u32 index)
{
  return std::make_unique<GunCon>(index);
}

static const Controller::ControllerBindingInfo s_binding_info[] = {
#define BUTTON(name, display_name, button, genb)                                                                       \
  {                                                                                                                    \
    name, display_name, static_cast<u32>(button), InputBindingInfo::Type::Button, genb                                 \
  }

  // clang-format off
  BUTTON("Trigger", TRANSLATE_NOOP("GunCon", "Trigger"), GunCon::Button::Trigger, GenericInputBinding::R2),
  BUTTON("ShootOffscreen", TRANSLATE_NOOP("GunCon", "Shoot Offscreen"), GunCon::Button::ShootOffscreen, GenericInputBinding::L2),
  BUTTON("A", TRANSLATE_NOOP("GunCon", "A"), GunCon::Button::A, GenericInputBinding::Cross),
  BUTTON("B", TRANSLATE_NOOP("GunCon", "B"), GunCon::Button::B, GenericInputBinding::Circle),
// clang-format on

#undef BUTTON
};

static const SettingInfo s_settings[] = {
  {SettingInfo::Type::Path, "CrosshairImagePath", TRANSLATE_NOOP("GunCon", "Crosshair Image Path"),
   TRANSLATE_NOOP("GunCon", "Path to an image to use as a crosshair/cursor."), nullptr, nullptr, nullptr, nullptr,
   nullptr, nullptr, 0.0f},
  {SettingInfo::Type::Float, "CrosshairScale", TRANSLATE_NOOP("GunCon", "Crosshair Image Scale"),
   TRANSLATE_NOOP("GunCon", "Scale of crosshair image on screen."), "1.0", "0.0001", "100.0", "0.10", "%.0f%%", nullptr,
   100.0f},
  {SettingInfo::Type::Float, "XScale", TRANSLATE_NOOP("GunCon", "X Scale"),
   TRANSLATE_NOOP("GunCon", "Scales X coordinates relative to the center of the screen."), "1.0", "0.01", "2.0", "0.01",
   "%.0f%%", nullptr, 100.0f}};

const Controller::ControllerInfo GunCon::INFO = {ControllerType::GunCon,
                                                 "GunCon",
                                                 TRANSLATE_NOOP("ControllerType", "GunCon"),
                                                 s_binding_info,
                                                 countof(s_binding_info),
                                                 s_settings,
                                                 countof(s_settings),
                                                 Controller::VibrationCapabilities::NoVibration};

void GunCon::LoadSettings(SettingsInterface& si, const char* section)
{
  Controller::LoadSettings(si, section);

  m_crosshair_image_path = si.GetStringValue(section, "CrosshairImagePath");
#ifndef __ANDROID__
  if (m_crosshair_image_path.empty())
    m_crosshair_image_path = Path::Combine(EmuFolders::Resources, "images/crosshair.png");
#endif

  m_crosshair_image_scale = si.GetFloatValue(section, "CrosshairScale", 1.0f);

  m_x_scale = si.GetFloatValue(section, "XScale", 1.0f);
}

bool GunCon::GetSoftwareCursor(std::string* image_path, float* image_scale, bool* relative_mode)
{
  if (m_crosshair_image_path.empty())
    return false;

  *image_path = m_crosshair_image_path;
  *image_scale = m_crosshair_image_scale;
  *relative_mode = false;
  return true;
}
