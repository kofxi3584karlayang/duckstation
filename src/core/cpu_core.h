#pragma once
#include "common/bitfield.h"
#include "cpu_types.h"
#include "gte.h"
#include "types.h"
#include <array>
#include <optional>

class StateWrapper;

class Bus;

namespace CPU {

class CodeCache;

namespace Recompiler
{
class CodeGenerator;
class Thunks;
}

class Core
{
public:
  static constexpr VirtualMemoryAddress RESET_VECTOR = UINT32_C(0xBFC00000);
  static constexpr PhysicalMemoryAddress DCACHE_LOCATION = UINT32_C(0x1F800000);
  static constexpr PhysicalMemoryAddress DCACHE_LOCATION_MASK = UINT32_C(0xFFFFFC00);
  static constexpr PhysicalMemoryAddress DCACHE_OFFSET_MASK = UINT32_C(0x000003FF);
  static constexpr PhysicalMemoryAddress DCACHE_SIZE = UINT32_C(0x00000400);
  
  friend CodeCache;
  friend Recompiler::CodeGenerator;
  friend Recompiler::Thunks;

  Core();
  ~Core();

  void Initialize(Bus* bus);
  void Reset();
  bool DoState(StateWrapper& sw);

  void Execute();

  const Registers& GetRegs() const { return m_regs; }
  Registers& GetRegs() { return m_regs; }

  TickCount GetPendingTicks() const { return m_pending_ticks; }
  void ResetPendingTicks() { m_pending_ticks = 0; }
  void AddPendingTicks(TickCount ticks)
  {
    m_pending_ticks += ticks;
    m_downcount -= ticks;
  }

  void SetDowncount(TickCount downcount) { m_downcount = (downcount < m_downcount) ? downcount : m_downcount; }
  void ResetDowncount() { m_downcount = MAX_SLICE_SIZE; }

  // Sets the PC and flushes the pipeline.
  void SetPC(u32 new_pc);

  // Memory reads variants which do not raise exceptions.
  bool SafeReadMemoryByte(VirtualMemoryAddress addr, u8* value);
  bool SafeReadMemoryHalfWord(VirtualMemoryAddress addr, u16* value);
  bool SafeReadMemoryWord(VirtualMemoryAddress addr, u32* value);
  bool SafeWriteMemoryByte(VirtualMemoryAddress addr, u8 value);
  bool SafeWriteMemoryHalfWord(VirtualMemoryAddress addr, u16 value);
  bool SafeWriteMemoryWord(VirtualMemoryAddress addr, u32 value);

  // External IRQs
  void SetExternalInterrupt(u8 bit);
  void ClearExternalInterrupt(u8 bit);

private:
  template<MemoryAccessType type, MemoryAccessSize size>
  TickCount DoMemoryAccess(VirtualMemoryAddress address, u32& value);

  template<MemoryAccessType type, MemoryAccessSize size>
  bool DoAlignmentCheck(VirtualMemoryAddress address);

  template<MemoryAccessType type, MemoryAccessSize size>
  void DoScratchpadAccess(PhysicalMemoryAddress address, u32& value);

  bool ReadMemoryByte(VirtualMemoryAddress addr, u8* value);
  bool ReadMemoryHalfWord(VirtualMemoryAddress addr, u16* value);
  bool ReadMemoryWord(VirtualMemoryAddress addr, u32* value);
  bool WriteMemoryByte(VirtualMemoryAddress addr, u8 value);
  bool WriteMemoryHalfWord(VirtualMemoryAddress addr, u16 value);
  bool WriteMemoryWord(VirtualMemoryAddress addr, u32 value);

  // state helpers
  bool InUserMode() const { return m_cop0_regs.sr.KUc; }
  bool InKernelMode() const { return !m_cop0_regs.sr.KUc; }

  // timing
  void AddTicks(TickCount ticks)
  {
    m_pending_ticks += ticks;
    m_downcount -= ticks;
  }

  void DisassembleAndPrint(u32 addr);
  void DisassembleAndLog(u32 addr);
  void DisassembleAndPrint(u32 addr, u32 instructions_before, u32 instructions_after);

  // Fetches the instruction at m_regs.npc
  bool FetchInstruction();
  void ExecuteInstruction();
  void ExecuteCop0Instruction();
  void ExecuteCop2Instruction();
  void Branch(u32 target);

  // exceptions
  u32 GetExceptionVector(Exception excode) const;
  void RaiseException(Exception excode);
  void RaiseException(Exception excode, u32 EPC, bool BD, bool BT, u8 CE);
  bool HasPendingInterrupt();
  void DispatchInterrupt();

  // flushes any load delays if present
  void FlushLoadDelay();

  // clears pipeline of load/branch delays
  void FlushPipeline();

  // helper functions for registers which aren't writable
  u32 ReadReg(Reg rs);
  void WriteReg(Reg rd, u32 value);

  // helper for generating a load delay write
  void WriteRegDelayed(Reg rd, u32 value);

  // write to cache control register
  void WriteCacheControl(u32 value);

  // read/write cop0 regs
  std::optional<u32> ReadCop0Reg(Cop0Reg reg);
  void WriteCop0Reg(Cop0Reg reg, u32 value);

  Bus* m_bus = nullptr;

  // ticks the CPU has executed
  TickCount m_pending_ticks = 0;
  TickCount m_downcount = MAX_SLICE_SIZE;

  Registers m_regs = {};
  Cop0Registers m_cop0_regs = {};
  Instruction m_next_instruction = {};

  // address of the instruction currently being executed
  Instruction m_current_instruction = {};
  u32 m_current_instruction_pc = 0;
  bool m_current_instruction_in_branch_delay_slot = false;
  bool m_current_instruction_was_branch_taken = false;
  bool m_next_instruction_is_branch_delay_slot = false;
  bool m_branch_was_taken = false;
  bool m_exception_raised = false;

  // load delays
  Reg m_load_delay_reg = Reg::count;
  u32 m_load_delay_old_value = 0;
  Reg m_next_load_delay_reg = Reg::count;
  u32 m_next_load_delay_old_value = 0;

  u32 m_cache_control = 0;

  // data cache (used as scratchpad)
  std::array<u8, DCACHE_SIZE> m_dcache = {};

  GTE::Core m_cop2;
};

extern bool TRACE_EXECUTION;
extern bool LOG_EXECUTION;

// Write to CPU execution log file.
void WriteToExecutionLog(const char* format, ...);

} // namespace CPU

#include "cpu_core.inl"
