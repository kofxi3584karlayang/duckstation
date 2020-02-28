#include "timers.h"
#include "common/log.h"
#include "common/state_wrapper.h"
#include "gpu.h"
#include "interrupt_controller.h"
#include "system.h"
#include <imgui.h>
Log_SetChannel(Timers);

Timers::Timers() = default;

Timers::~Timers() = default;

void Timers::Initialize(System* system, InterruptController* interrupt_controller, GPU* gpu)
{
  m_system = system;
  m_interrupt_controller = interrupt_controller;
  m_gpu = gpu;
  m_sysclk_event = system->CreateTimingEvent("Timer SysClk Interrupt", 1, 1,
                                             std::bind(&Timers::AddSysClkTicks, this, std::placeholders::_1), false);
}

void Timers::Reset()
{
  for (CounterState& cs : m_states)
  {
    cs.mode.bits = 0;
    cs.counter = 0;
    cs.target = 0;
    cs.gate = false;
    cs.external_counting_enabled = false;
    cs.counting_enabled = true;
    cs.irq_done = false;
  }

  m_sysclk_div_8_carry = 0;
  UpdateSysClkEvent();
}

bool Timers::DoState(StateWrapper& sw)
{
  for (CounterState& cs : m_states)
  {
    sw.Do(&cs.mode.bits);
    sw.Do(&cs.counter);
    sw.Do(&cs.target);
    sw.Do(&cs.gate);
    sw.Do(&cs.use_external_clock);
    sw.Do(&cs.external_counting_enabled);
    sw.Do(&cs.counting_enabled);
    sw.Do(&cs.irq_done);
  }

  sw.Do(&m_sysclk_div_8_carry);

  if (sw.IsReading())
    UpdateSysClkEvent();

  return !sw.HasError();
}

void Timers::SetGate(u32 timer, bool state)
{
  CounterState& cs = m_states[timer];
  if (cs.gate == state)
    return;

  cs.gate = state;

  if (cs.mode.sync_enable)
  {
    if (state)
    {
      switch (cs.mode.sync_mode)
      {
        case SyncMode::ResetOnGate:
        case SyncMode::ResetAndRunOnGate:
          cs.counter = 0;
          break;

        case SyncMode::FreeRunOnGate:
          cs.mode.sync_enable = false;
          break;
      }
    }

    UpdateCountingEnabled(cs);
  }
}

void Timers::AddTicks(u32 timer, TickCount count)
{
  CounterState& cs = m_states[timer];
  const u32 old_counter = cs.counter;
  cs.counter += static_cast<u32>(count);

  bool interrupt_request = false;
  if (cs.counter >= cs.target && old_counter < cs.target)
  {
    interrupt_request |= cs.mode.irq_at_target;
    cs.mode.reached_target = true;
  }
  if (cs.counter >= 0xFFFF)
  {
    interrupt_request |= cs.mode.irq_on_overflow;
    cs.mode.reached_overflow = true;
  }

  if (interrupt_request)
  {
    if (!cs.mode.irq_pulse_n)
    {
      // this is actually low for a few cycles
      cs.mode.interrupt_request_n = false;
      UpdateIRQ(timer);
      cs.mode.interrupt_request_n = true;
    }
    else
    {
      cs.mode.interrupt_request_n ^= true;
      UpdateIRQ(timer);
    }
  }

  if (cs.mode.reset_at_target)
  {
    if (cs.target > 0)
      cs.counter %= cs.target;
    else
      cs.counter = 0;
  }
  else
  {
    cs.counter %= 0xFFFF;
  }
}

void Timers::AddSysClkTicks(TickCount sysclk_ticks)
{
  if (!m_states[0].external_counting_enabled && m_states[0].counting_enabled)
    AddTicks(0, sysclk_ticks);
  if (!m_states[1].external_counting_enabled && m_states[1].counting_enabled)
    AddTicks(1, sysclk_ticks);
  if (m_states[2].external_counting_enabled)
  {
    TickCount sysclk_div_8_ticks = (sysclk_ticks + m_sysclk_div_8_carry) / 8;
    m_sysclk_div_8_carry = (sysclk_ticks + m_sysclk_div_8_carry) % 8;
    AddTicks(2, sysclk_div_8_ticks);
  }
  else if (m_states[2].counting_enabled)
  {
    AddTicks(2, sysclk_ticks);
  }

  UpdateSysClkEvent();
}

u32 Timers::ReadRegister(u32 offset)
{
  const u32 timer_index = (offset >> 4) & u32(0x03);
  const u32 port_offset = offset & u32(0x0F);

  CounterState& cs = m_states[timer_index];

  switch (port_offset)
  {
    case 0x00:
    {
      if (timer_index < 2)
      {
        // timers 0/1 depend on the GPU
        if (cs.external_counting_enabled)
          m_gpu->Synchronize();
      }

      m_sysclk_event->InvokeEarly();

      return cs.counter;
    }

    case 0x04:
    {
      if (timer_index < 2)
      {
        // timers 0/1 depend on the GPU
        if (cs.external_counting_enabled)
          m_gpu->Synchronize();
      }

      m_sysclk_event->InvokeEarly();

      const u32 bits = cs.mode.bits;
      cs.mode.reached_overflow = false;
      cs.mode.reached_target = false;
      return bits;
    }

    case 0x08:
      return cs.target;

    default:
      Log_ErrorPrintf("Read unknown register in timer %u (offset 0x%02X)", offset);
      return UINT32_C(0xFFFFFFFF);
  }
}

void Timers::WriteRegister(u32 offset, u32 value)
{
  const u32 timer_index = (offset >> 4) & u32(0x03);
  const u32 port_offset = offset & u32(0x0F);

  CounterState& cs = m_states[timer_index];

  switch (port_offset)
  {
    case 0x00:
    {
      Log_DebugPrintf("Timer %u write counter %u", timer_index, value);
      m_sysclk_event->InvokeEarly();
      cs.counter = value & u32(0xFFFF);
    }
    break;

    case 0x04:
    {
      Log_DebugPrintf("Timer %u write mode register 0x%04X", timer_index, value);
      m_sysclk_event->InvokeEarly();
      cs.mode.bits = value & u32(0x1FFF);
      cs.use_external_clock = (cs.mode.clock_source & (timer_index == 2 ? 2 : 1)) != 0;
      cs.counter = 0;
      cs.irq_done = false;
      if (cs.mode.irq_pulse_n)
        cs.mode.interrupt_request_n = true;

      UpdateCountingEnabled(cs);
      UpdateIRQ(timer_index);
      UpdateSysClkEvent();
    }
    break;

    case 0x08:
    {
      Log_DebugPrintf("Timer %u write target 0x%04X", timer_index, ZeroExtend32(Truncate16(value)));
      m_sysclk_event->InvokeEarly();
      cs.target = value & u32(0xFFFF);
    }
    break;

    default:
      Log_ErrorPrintf("Write unknown register in timer %u (offset 0x%02X, value 0x%X)", offset, value);
      break;
  }
}

void Timers::UpdateCountingEnabled(CounterState& cs)
{
  if (cs.mode.sync_enable)
  {
    switch (cs.mode.sync_mode)
    {
      case SyncMode::PauseOnGate:
        cs.counting_enabled = !cs.gate;
        break;

      case SyncMode::ResetOnGate:
        cs.counting_enabled = true;
        break;

      case SyncMode::ResetAndRunOnGate:
      case SyncMode::FreeRunOnGate:
        cs.counting_enabled = cs.gate;
        break;
    }
  }
  else
  {
    cs.counting_enabled = true;
  }

  cs.external_counting_enabled = cs.use_external_clock && cs.counting_enabled;
}

void Timers::UpdateIRQ(u32 index)
{
  CounterState& cs = m_states[index];
  if (cs.mode.interrupt_request_n || (!cs.mode.irq_repeat && cs.irq_done))
    return;

  Log_DebugPrintf("Raising timer %u IRQ", index);
  cs.irq_done = true;
  m_interrupt_controller->InterruptRequest(
    static_cast<InterruptController::IRQ>(static_cast<u32>(InterruptController::IRQ::TMR0) + index));
}

TickCount Timers::GetTicksUntilNextInterrupt() const
{
  TickCount min_ticks = std::numeric_limits<TickCount>::max();
  for (u32 i = 0; i < NUM_TIMERS; i++)
  {
    const CounterState& cs = m_states[i];
    if (!cs.counting_enabled || (i < 2 && cs.external_counting_enabled) ||
        (!cs.mode.irq_at_target && !cs.mode.irq_on_overflow))
    {
      continue;
    }

    TickCount min_ticks_for_this_timer = std::numeric_limits<TickCount>::max();
    if (cs.mode.irq_at_target && cs.counter < cs.target)
      min_ticks_for_this_timer = static_cast<TickCount>(cs.target - cs.counter);
    if (cs.mode.irq_on_overflow && cs.counter < cs.target)
      min_ticks_for_this_timer = std::min(min_ticks_for_this_timer, static_cast<TickCount>(0xFFFF - cs.counter));

    if (cs.external_counting_enabled) // sysclk/8 for timer 2
      min_ticks_for_this_timer = std::max<TickCount>(1, min_ticks_for_this_timer / 8);

    min_ticks = std::min(min_ticks, min_ticks_for_this_timer);
  }

  return min_ticks;
}

void Timers::UpdateSysClkEvent()
{
  // Still update once every 100ms. If we get polled we'll execute sooner.
  const TickCount ticks = GetTicksUntilNextInterrupt();
  if (ticks == std::numeric_limits<TickCount>::max())
    m_sysclk_event->Schedule(MAX_SLICE_SIZE);
  else
    m_sysclk_event->Schedule(ticks);
}

void Timers::DrawDebugStateWindow()
{
  static constexpr u32 NUM_COLUMNS = 10;
  static constexpr std::array<const char*, NUM_COLUMNS> column_names = {
    {"#", "Value", "Target", "Sync", "Reset", "IRQ", "IRQRepeat", "IRQToggle", "Clock Source", "Reached"}};
  static constexpr std::array<const char*, 4> sync_mode_names = {
    {"PauseOnGate", "ResetOnGate", "ResetAndRunOnGate", "FreeRunOnGate"}};
  static constexpr std::array<std::array<const char*, 4>, 3> clock_source_names = {
    {{{"SysClk", "DotClk", "SysClk", "DotClk"}},
     {{"SysClk", "HBlank", "SysClk", "HBlank"}},
     {{"SysClk", "DotClk", "SysClk/8", "SysClk/8"}}}};

  const float framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale.x;

  ImGui::SetNextWindowSize(ImVec2(800.0f * framebuffer_scale, 100.0f * framebuffer_scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Timer State", &m_system->GetSettings().debugging.show_timers_state))
  {
    ImGui::End();
    return;
  }

  ImGui::Columns(NUM_COLUMNS);
  ImGui::SetColumnWidth(0, 20.0f * framebuffer_scale);
  ImGui::SetColumnWidth(1, 50.0f * framebuffer_scale);
  ImGui::SetColumnWidth(2, 50.0f * framebuffer_scale);
  ImGui::SetColumnWidth(3, 100.0f * framebuffer_scale);
  ImGui::SetColumnWidth(4, 80.0f * framebuffer_scale);
  ImGui::SetColumnWidth(5, 80.0f * framebuffer_scale);
  ImGui::SetColumnWidth(6, 80.0f * framebuffer_scale);
  ImGui::SetColumnWidth(7, 80.0f * framebuffer_scale);
  ImGui::SetColumnWidth(8, 80.0f * framebuffer_scale);
  ImGui::SetColumnWidth(9, 80.0f * framebuffer_scale);

  for (const char* title : column_names)
  {
    ImGui::TextUnformatted(title);
    ImGui::NextColumn();
  }

  for (u32 i = 0; i < NUM_TIMERS; i++)
  {
    const CounterState& cs = m_states[i];
    ImGui::PushStyleColor(ImGuiCol_Text,
                          cs.counting_enabled ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::Text("%u", i);
    ImGui::NextColumn();
    ImGui::Text("%u", cs.counter);
    ImGui::NextColumn();
    ImGui::Text("%u", cs.target);
    ImGui::NextColumn();
    ImGui::Text("%s",
                cs.mode.sync_enable ? sync_mode_names[static_cast<u8>(cs.mode.sync_mode.GetValue())] : "Disabled");
    ImGui::NextColumn();
    ImGui::Text("%s", cs.mode.reset_at_target ? "@Target" : "@Overflow");
    ImGui::NextColumn();
    ImGui::Text("%s%s", cs.mode.irq_at_target ? "Target " : "", cs.mode.irq_on_overflow ? "Overflow" : "");
    ImGui::NextColumn();
    ImGui::Text("%s", cs.mode.irq_repeat ? "Yes" : "No");
    ImGui::NextColumn();
    ImGui::Text("%s", cs.mode.irq_pulse_n ? "Yes" : "No");
    ImGui::NextColumn();
    ImGui::Text("%s%s", clock_source_names[i][cs.mode.clock_source], cs.external_counting_enabled ? " (External)" : "");
    ImGui::NextColumn();
    ImGui::Text("%s%s", cs.mode.reached_target ? "Target " : "", cs.mode.reached_overflow ? "Overflow" : "");
    ImGui::NextColumn();
    ImGui::PopStyleColor();
  }

  ImGui::Columns(1);
  ImGui::End();
}
