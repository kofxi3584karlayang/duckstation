#include "cpu_recompiler_register_cache.h"
#include "YBaseLib/Log.h"
#include "cpu_recompiler_code_generator.h"
#include <cinttypes>
Log_SetChannel(CPU::Recompiler);

namespace CPU::Recompiler {

Value::Value() = default;

Value::Value(RegisterCache* regcache_, u64 constant_, RegSize size_, ValueFlags flags_)
  : regcache(regcache_), constant_value(constant_), size(size_), flags(flags_)
{
}

Value::Value(const Value& other)
  : regcache(other.regcache), constant_value(other.constant_value), host_reg(other.host_reg), size(other.size),
    flags(other.flags)
{
  AssertMsg(!other.IsScratch(), "Can't copy a temporary register");
}

Value::Value(Value&& other)
  : regcache(other.regcache), constant_value(other.constant_value), host_reg(other.host_reg), size(other.size),
    flags(other.flags)
{
  other.Clear();
}

Value::Value(RegisterCache* regcache_, HostReg reg_, RegSize size_, ValueFlags flags_)
  : regcache(regcache_), host_reg(reg_), size(size_), flags(flags_)
{
}

Value::~Value()
{
  Release();
}

Value& Value::operator=(const Value& other)
{
  AssertMsg(!other.IsScratch(), "Can't copy a temporary register");

  Release();
  regcache = other.regcache;
  constant_value = other.constant_value;
  host_reg = other.host_reg;
  size = other.size;
  flags = other.flags;

  return *this;
}

Value& Value::operator=(Value&& other)
{
  Release();
  regcache = other.regcache;
  constant_value = other.constant_value;
  host_reg = other.host_reg;
  size = other.size;
  flags = other.flags;
  other.Clear();
  return *this;
}

void Value::Clear()
{
  regcache = nullptr;
  constant_value = 0;
  host_reg = {};
  size = RegSize_8;
  flags = ValueFlags::None;
}

void Value::Release()
{
  if (IsScratch())
  {
    DebugAssert(IsInHostRegister() && regcache);
    regcache->FreeHostReg(host_reg);
  }
}

void Value::ReleaseAndClear()
{
  Release();
  Clear();
}

void Value::Discard()
{
  DebugAssert(IsInHostRegister());
  regcache->DiscardHostReg(host_reg);
}

void Value::Undiscard()
{
  DebugAssert(IsInHostRegister());
  regcache->UndiscardHostReg(host_reg);
}

RegisterCache::RegisterCache(CodeGenerator& code_generator) : m_code_generator(code_generator)
{
  m_guest_register_order.fill(Reg::count);
}

RegisterCache::~RegisterCache() = default;

void RegisterCache::SetHostRegAllocationOrder(std::initializer_list<HostReg> regs)
{
  size_t index = 0;
  for (HostReg reg : regs)
  {
    m_host_register_state[reg] = HostRegState::Usable;
    m_host_register_allocation_order[index++] = reg;
  }
  m_host_register_available_count = static_cast<u32>(index);
}

void RegisterCache::SetCallerSavedHostRegs(std::initializer_list<HostReg> regs)
{
  for (HostReg reg : regs)
    m_host_register_state[reg] |= HostRegState::CallerSaved;
}

void RegisterCache::SetCalleeSavedHostRegs(std::initializer_list<HostReg> regs)
{
  for (HostReg reg : regs)
    m_host_register_state[reg] |= HostRegState::CalleeSaved;
}

void RegisterCache::SetCPUPtrHostReg(HostReg reg)
{
  m_cpu_ptr_host_register = reg;
}

bool RegisterCache::IsUsableHostReg(HostReg reg) const
{
  return (m_host_register_state[reg] & HostRegState::Usable) != HostRegState::None;
}

bool RegisterCache::IsHostRegInUse(HostReg reg) const
{
  return (m_host_register_state[reg] & HostRegState::InUse) != HostRegState::None;
}

bool RegisterCache::HasFreeHostRegister() const
{
  for (const HostRegState state : m_host_register_state)
  {
    if ((state & (HostRegState::Usable | HostRegState::InUse)) == (HostRegState::Usable))
      return true;
  }

  return false;
}

u32 RegisterCache::GetUsedHostRegisters() const
{
  u32 count = 0;
  for (const HostRegState state : m_host_register_state)
  {
    if ((state & (HostRegState::Usable | HostRegState::InUse)) == (HostRegState::Usable | HostRegState::InUse))
      count++;
  }

  return count;
}

u32 RegisterCache::GetFreeHostRegisters() const
{
  u32 count = 0;
  for (const HostRegState state : m_host_register_state)
  {
    if ((state & (HostRegState::Usable | HostRegState::InUse)) == (HostRegState::Usable))
      count++;
  }

  return count;
}

HostReg RegisterCache::AllocateHostReg(HostRegState state /* = HostRegState::InUse */)
{
  // try for a free register in allocation order
  for (u32 i = 0; i < m_host_register_available_count; i++)
  {
    const HostReg reg = m_host_register_allocation_order[i];
    if ((m_host_register_state[reg] & (HostRegState::Usable | HostRegState::InUse)) == HostRegState::Usable)
    {
      if (AllocateHostReg(reg, state))
        return reg;
    }
  }

  // evict one of the cached guest registers
  if (!EvictOneGuestRegister())
    Panic("Failed to evict guest register for new allocation");

  return AllocateHostReg(state);
}

bool RegisterCache::AllocateHostReg(HostReg reg, HostRegState state /*= HostRegState::InUse*/)
{
  if ((m_host_register_state[reg] & HostRegState::InUse) == HostRegState::InUse)
    return false;

  m_host_register_state[reg] |= state;

  if ((m_host_register_state[reg] & (HostRegState::CalleeSaved | HostRegState::CalleeSavedAllocated)) ==
      HostRegState::CalleeSaved)
  {
    // new register we need to save..
    DebugAssert(m_host_register_callee_saved_order_count < HostReg_Count);
    m_host_register_callee_saved_order[m_host_register_callee_saved_order_count++] = reg;
    m_host_register_state[reg] |= HostRegState::CalleeSavedAllocated;
    m_code_generator.EmitPushHostReg(reg);
  }

  return reg;
}

void RegisterCache::DiscardHostReg(HostReg reg)
{
  DebugAssert(IsHostRegInUse(reg));
  Log_DebugPrintf("Discarding host register %s", m_code_generator.GetHostRegName(reg));
  m_host_register_state[reg] |= HostRegState::Discarded;
}

void RegisterCache::UndiscardHostReg(HostReg reg)
{
  DebugAssert(IsHostRegInUse(reg));
  Log_DebugPrintf("Undiscarding host register %s", m_code_generator.GetHostRegName(reg));
  m_host_register_state[reg] &= ~HostRegState::Discarded;
}

void RegisterCache::FreeHostReg(HostReg reg)
{
  DebugAssert(IsHostRegInUse(reg));
  Log_DebugPrintf("Freeing host register %s", m_code_generator.GetHostRegName(reg));
  m_host_register_state[reg] &= ~HostRegState::InUse;
}

void RegisterCache::EnsureHostRegFree(HostReg reg)
{
  if (!IsHostRegInUse(reg))
    return;

  for (u8 i = 0; i < static_cast<u8>(Reg::count); i++)
  {
    if (m_guest_reg_cache[i].IsInHostRegister() && m_guest_reg_cache[i].GetHostRegister() == reg)
      FlushGuestRegister(m_guest_reg_cache[i], static_cast<Reg>(i), true, true);
  }
}

Value RegisterCache::GetCPUPtr()
{
  return Value::FromHostReg(this, m_cpu_ptr_host_register, HostPointerSize);
}

Value RegisterCache::AllocateScratch(RegSize size, HostReg reg /* = HostReg_Invalid */)
{
  if (reg == HostReg_Invalid)
  {
    reg = AllocateHostReg();
  }
  else
  {
    Assert(!IsHostRegInUse(reg));
    if (!AllocateHostReg(reg))
      Panic("Failed to allocate specific host register");
  }

  Log_DebugPrintf("Allocating host register %s as scratch", m_code_generator.GetHostRegName(reg));
  return Value::FromScratch(this, reg, size);
}

u32 RegisterCache::PushCallerSavedRegisters() const
{
  u32 count = 0;
  for (u32 i = 0; i < HostReg_Count; i++)
  {
    if ((m_host_register_state[i] & (HostRegState::CallerSaved | HostRegState::InUse | HostRegState::Discarded)) ==
        (HostRegState::CallerSaved | HostRegState::InUse))
    {
      m_code_generator.EmitPushHostReg(static_cast<HostReg>(i));
      count++;
    }
  }

  return count;
}

u32 RegisterCache::PopCallerSavedRegisters() const
{
  u32 count = 0;
  u32 i = (HostReg_Count - 1);
  do
  {
    if ((m_host_register_state[i] & (HostRegState::CallerSaved | HostRegState::InUse | HostRegState::Discarded)) ==
        (HostRegState::CallerSaved | HostRegState::InUse))
    {
      m_code_generator.EmitPopHostReg(static_cast<HostReg>(i));
      count++;
    }
    i--;
  } while (i > 0);
  return count;
}

u32 RegisterCache::PopCalleeSavedRegisters()
{
  if (m_host_register_callee_saved_order_count == 0)
    return 0;

  u32 count = 0;
  u32 i = m_host_register_callee_saved_order_count;
  do
  {
    const HostReg reg = m_host_register_callee_saved_order[i - 1];
    DebugAssert((m_host_register_state[reg] & (HostRegState::CalleeSaved | HostRegState::CalleeSavedAllocated)) ==
                (HostRegState::CalleeSaved | HostRegState::CalleeSavedAllocated));

    m_code_generator.EmitPopHostReg(reg);
    m_host_register_state[reg] &= ~HostRegState::CalleeSavedAllocated;
    count++;
    i--;
  } while (i > 0);
  return count;
}

Value RegisterCache::ReadGuestRegister(Reg guest_reg, bool cache /* = true */, bool force_host_register /* = false */,
                                       HostReg forced_host_reg /* = HostReg_Invalid */)
{
  return ReadGuestRegister(m_guest_reg_cache[static_cast<u8>(guest_reg)], guest_reg, cache, force_host_register,
                           forced_host_reg);
}

Value RegisterCache::ReadGuestRegister(Value& cache_value, Reg guest_reg, bool cache, bool force_host_register,
                                       HostReg forced_host_reg)
{
  // register zero is always zero
  if (guest_reg == Reg::zero)
    return Value::FromConstantU32(0);

  if (cache_value.IsValid())
  {
    if (cache_value.IsInHostRegister())
    {
      PushRegisterToOrder(guest_reg);

      // if it's in the wrong register, return it as scratch
      if (forced_host_reg == HostReg_Invalid || cache_value.GetHostRegister() == forced_host_reg)
        return cache_value;

      Value temp = AllocateScratch(RegSize_32, forced_host_reg);
      m_code_generator.EmitCopyValue(forced_host_reg, cache_value);
      return temp;
    }
    else if (force_host_register)
    {
      // if it's not in a register, it should be constant
      DebugAssert(cache_value.IsConstant());

      HostReg host_reg;
      if (forced_host_reg == HostReg_Invalid)
      {
        host_reg = AllocateHostReg();
      }
      else
      {
        Assert(!IsHostRegInUse(forced_host_reg));
        if (!AllocateHostReg(forced_host_reg))
          Panic("Failed to allocate specific host register");
        host_reg = forced_host_reg;
      }

      Log_DebugPrintf("Allocated host register %s for constant guest register %s (0x%" PRIX64 ")",
                      m_code_generator.GetHostRegName(host_reg), GetRegName(guest_reg), cache_value.constant_value);

      m_code_generator.EmitCopyValue(host_reg, cache_value);
      cache_value.AddHostReg(this, host_reg);
      AppendRegisterToOrder(guest_reg);

      // if we're forcing a host register, we're probably going to be changing the value,
      // in which case the constant won't be correct anyway. so just drop it.
      cache_value.ClearConstant();
      return cache_value;
    }
    else
    {
      // constant
      return cache_value;
    }
  }

  HostReg host_reg;
  if (forced_host_reg == HostReg_Invalid)
  {
    host_reg = AllocateHostReg();
  }
  else
  {
    Assert(!IsHostRegInUse(forced_host_reg));
    if (!AllocateHostReg(forced_host_reg))
      Panic("Failed to allocate specific host register");
    host_reg = forced_host_reg;
  }

  m_code_generator.EmitLoadGuestRegister(host_reg, guest_reg);

  Log_DebugPrintf("Loading guest register %s to host register %s%s", GetRegName(guest_reg),
                  m_code_generator.GetHostRegName(host_reg, RegSize_32), cache ? " (cached)" : "");

  if (cache)
  {
    // Now in cache.
    cache_value.SetHostReg(this, host_reg, RegSize_32);
    AppendRegisterToOrder(guest_reg);
    return cache_value;
  }
  else
  {
    // Skip caching, return the register as a value.
    return Value::FromScratch(this, host_reg, RegSize_32);
  }
}

Value RegisterCache::WriteGuestRegister(Reg guest_reg, Value&& value)
{
  return WriteGuestRegister(m_guest_reg_cache[static_cast<u8>(guest_reg)], guest_reg, std::move(value));
}

Value RegisterCache::WriteGuestRegister(Value& cache_value, Reg guest_reg, Value&& value)
{
  // ignore writes to register zero
  if (guest_reg == Reg::zero)
    return std::move(value);

  DebugAssert(value.size == RegSize_32);
  if (cache_value.IsInHostRegister() && value.IsInHostRegister() && cache_value.host_reg == value.host_reg)
  {
    // updating the register value.
    Log_DebugPrintf("Updating guest register %s (in host register %s)", GetRegName(guest_reg),
                    m_code_generator.GetHostRegName(value.host_reg, RegSize_32));
    cache_value = std::move(value);
    cache_value.SetDirty();
    return cache_value;
  }

  InvalidateGuestRegister(cache_value, guest_reg);
  DebugAssert(!cache_value.IsValid());

  if (value.IsConstant())
  {
    // No need to allocate a host register, and we can defer the store.
    cache_value = value;
    cache_value.SetDirty();
    return cache_value;
  }

  AppendRegisterToOrder(guest_reg);

  // If it's a temporary, we can bind that to the guest register.
  if (value.IsScratch())
  {
    Log_DebugPrintf("Binding scratch register %s to guest register %s",
                    m_code_generator.GetHostRegName(value.host_reg, RegSize_32), GetRegName(guest_reg));

    cache_value = std::move(value);
    cache_value.flags &= ~ValueFlags::Scratch;
    cache_value.SetDirty();
    return Value::FromHostReg(this, cache_value.host_reg, RegSize_32);
  }

  // Allocate host register, and copy value to it.
  HostReg host_reg = AllocateHostReg();
  m_code_generator.EmitCopyValue(host_reg, value);
  cache_value.SetHostReg(this, host_reg, RegSize_32);
  cache_value.SetDirty();

  Log_DebugPrintf("Copying non-scratch register %s to %s to guest register %s",
                  m_code_generator.GetHostRegName(value.host_reg, RegSize_32),
                  m_code_generator.GetHostRegName(host_reg, RegSize_32), GetRegName(guest_reg));

  return Value::FromHostReg(this, cache_value.host_reg, RegSize_32);
}

void RegisterCache::FlushGuestRegister(Reg guest_reg, bool invalidate, bool clear_dirty)
{
  FlushGuestRegister(m_guest_reg_cache[static_cast<u8>(guest_reg)], guest_reg, invalidate, clear_dirty);
}

void RegisterCache::FlushGuestRegister(Value& cache_value, Reg guest_reg, bool invalidate, bool clear_dirty)
{
  if (cache_value.IsDirty())
  {
    if (cache_value.IsInHostRegister())
    {
      Log_DebugPrintf("Flushing guest register %s from host register %s", GetRegName(guest_reg),
                      m_code_generator.GetHostRegName(cache_value.host_reg, RegSize_32));
    }
    else if (cache_value.IsConstant())
    {
      Log_DebugPrintf("Flushing guest register %s from constant 0x%" PRIX64, GetRegName(guest_reg),
                      cache_value.constant_value);
    }
    m_code_generator.EmitStoreGuestRegister(guest_reg, cache_value);
    if (clear_dirty)
      cache_value.ClearDirty();
  }

  if (invalidate)
    InvalidateGuestRegister(cache_value, guest_reg);
}

void RegisterCache::InvalidateGuestRegister(Reg guest_reg)
{
  InvalidateGuestRegister(m_guest_reg_cache[static_cast<u8>(guest_reg)], guest_reg);
}

void RegisterCache::InvalidateGuestRegister(Value& cache_value, Reg guest_reg)
{
  if (!cache_value.IsValid())
    return;

  if (cache_value.IsInHostRegister())
  {
    FreeHostReg(cache_value.host_reg);
    ClearRegisterFromOrder(guest_reg);
  }

  Log_DebugPrintf("Invalidating guest register %s", GetRegName(guest_reg));
  cache_value.Clear();
}

void RegisterCache::FlushAllGuestRegisters(bool invalidate, bool clear_dirty)
{
  for (u8 reg = 0; reg < static_cast<u8>(Reg::count); reg++)
    FlushGuestRegister(static_cast<Reg>(reg), invalidate, clear_dirty);
}

bool RegisterCache::EvictOneGuestRegister()
{
  if (m_guest_register_order_count == 0)
    return false;

  // evict the register used the longest time ago
  Reg evict_reg = m_guest_register_order[m_guest_register_order_count - 1];
  Log_ProfilePrintf("Evicting guest register %s", GetRegName(evict_reg));
  FlushGuestRegister(evict_reg, true, true);

  return HasFreeHostRegister();
}

void RegisterCache::ClearRegisterFromOrder(Reg reg)
{
  for (u32 i = 0; i < m_guest_register_order_count; i++)
  {
    if (m_guest_register_order[i] == reg)
    {
      // move the registers after backwards into this spot
      const u32 count_after = m_guest_register_order_count - i - 1;
      if (count_after > 0)
        std::memmove(&m_guest_register_order[i], &m_guest_register_order[i + 1], sizeof(Reg) * count_after);
      else
        m_guest_register_order[i] = Reg::count;

      m_guest_register_order_count--;
      return;
    }
  }

  Panic("Clearing register from order not in order");
}

void RegisterCache::PushRegisterToOrder(Reg reg)
{
  for (u32 i = 0; i < m_guest_register_order_count; i++)
  {
    if (m_guest_register_order[i] == reg)
    {
      // move the registers after backwards into this spot
      const u32 count_before = i;
      if (count_before > 0)
        std::memmove(&m_guest_register_order[1], &m_guest_register_order[0], sizeof(Reg) * count_before);

      m_guest_register_order[0] = reg;
      return;
    }
  }

  Panic("Attempt to push register which is not ordered");
}

void RegisterCache::AppendRegisterToOrder(Reg reg)
{
  DebugAssert(m_guest_register_order_count < HostReg_Count);
  if (m_guest_register_order_count > 0)
    std::memmove(&m_guest_register_order[1], &m_guest_register_order[0], sizeof(Reg) * m_guest_register_order_count);
  m_guest_register_order[0] = reg;
  m_guest_register_order_count++;
}

} // namespace CPU::Recompiler
