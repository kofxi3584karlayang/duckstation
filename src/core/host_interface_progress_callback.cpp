// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "host_interface_progress_callback.h"
#include "common/log.h"
#include "host.h"
Log_SetChannel(HostInterfaceProgressCallback);

HostInterfaceProgressCallback::HostInterfaceProgressCallback() : BaseProgressCallback()
{
}

void HostInterfaceProgressCallback::PushState()
{
  BaseProgressCallback::PushState();
}

void HostInterfaceProgressCallback::PopState()
{
  BaseProgressCallback::PopState();
  Redraw(true);
}

void HostInterfaceProgressCallback::SetCancellable(bool cancellable)
{
  BaseProgressCallback::SetCancellable(cancellable);
  Redraw(true);
}

void HostInterfaceProgressCallback::SetTitle(const char* title)
{
  // todo?
}

void HostInterfaceProgressCallback::SetStatusText(const char* text)
{
  BaseProgressCallback::SetStatusText(text);
  Redraw(true);
}

void HostInterfaceProgressCallback::SetProgressRange(u32 range)
{
  u32 last_range = m_progress_range;

  BaseProgressCallback::SetProgressRange(range);

  if (m_progress_range != last_range)
    Redraw(false);
}

void HostInterfaceProgressCallback::SetProgressValue(u32 value)
{
  u32 lastValue = m_progress_value;

  BaseProgressCallback::SetProgressValue(value);

  if (m_progress_value != lastValue)
    Redraw(false);
}

void HostInterfaceProgressCallback::Redraw(bool force)
{
  if (m_last_progress_percent < 0 && m_open_time.GetTimeSeconds() < m_open_delay)
    return;

  const int percent =
    static_cast<int>((static_cast<float>(m_progress_value) / static_cast<float>(m_progress_range)) * 100.0f);
  if (percent == m_last_progress_percent && !force)
    return;

  m_last_progress_percent = percent;
  Host::DisplayLoadingScreen(m_status_text.c_str(), 0, static_cast<int>(m_progress_range),
                             static_cast<int>(m_progress_value));
}

void HostInterfaceProgressCallback::DisplayError(const char* message)
{
  ERROR_LOG(message);
}

void HostInterfaceProgressCallback::DisplayWarning(const char* message)
{
  WARNING_LOG(message);
}

void HostInterfaceProgressCallback::DisplayInformation(const char* message)
{
  INFO_LOG(message);
}

void HostInterfaceProgressCallback::DisplayDebugMessage(const char* message)
{
  DEV_LOG(message);
}

void HostInterfaceProgressCallback::ModalError(const char* message)
{
  ERROR_LOG(message);
  Host::ReportErrorAsync("Error", message);
}

bool HostInterfaceProgressCallback::ModalConfirmation(const char* message)
{
  INFO_LOG(message);
  return Host::ConfirmMessage("Confirm", message);
}

void HostInterfaceProgressCallback::ModalInformation(const char* message)
{
  INFO_LOG(message);
}
