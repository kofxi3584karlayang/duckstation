#include "cpu_types.h"
#include "YBaseLib/Assert.h"
#include <array>

namespace CPU {
static const std::array<const char*, 32> s_reg_names = {
  {"$zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
   "s0",    "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"}};

const char* GetRegName(Reg reg)
{
  DebugAssert(reg < Reg::count);
  return s_reg_names[static_cast<u8>(reg)];
}

bool IsBranchInstruction(const Instruction& instruction)
{
  switch (instruction.op)
  {
    case InstructionOp::j:
    case InstructionOp::jal:
    case InstructionOp::b:
    case InstructionOp::beq:
    case InstructionOp::bgtz:
    case InstructionOp::blez:
    case InstructionOp::bne:
      return true;

    case InstructionOp::funct:
    {
      switch (instruction.r.funct)
      {
        case InstructionFunct::jr:
        case InstructionFunct::jalr:
          return true;

        default:
          return false;
      }
    }

    default:
      return false;
  }
}

bool IsExitBlockInstruction(const Instruction& instruction)
{
  switch (instruction.op)
  {
    case InstructionOp::funct:
    {
      switch (instruction.r.funct)
      {
        case InstructionFunct::syscall:
        case InstructionFunct::break_:
          return true;

        default:
          return false;
      }
    }

    default:
      return false;
  }
}

bool CanInstructionTrap(const Instruction& instruction, bool in_user_mode)
{
  switch (instruction.op)
  {
    case InstructionOp::lui:
    case InstructionOp::andi:
    case InstructionOp::ori:
    case InstructionOp::xori:
    case InstructionOp::addiu:
    case InstructionOp::slti:
    case InstructionOp::sltiu:
      return false;

    case InstructionOp::cop0:
    case InstructionOp::cop2:
    case InstructionOp::lwc2:
    case InstructionOp::swc2:
      return in_user_mode;

      // swc0/lwc0/cop1/cop3 are essentially no-ops
    case InstructionOp::cop1:
    case InstructionOp::cop3:
    case InstructionOp::lwc0:
    case InstructionOp::lwc1:
    case InstructionOp::lwc3:
    case InstructionOp::swc0:
    case InstructionOp::swc1:
    case InstructionOp::swc3:
      return false;

    case InstructionOp::addi:
    case InstructionOp::lb:
    case InstructionOp::lh:
    case InstructionOp::lw:
    case InstructionOp::lbu:
    case InstructionOp::lhu:
    case InstructionOp::lwl:
    case InstructionOp::lwr:
    case InstructionOp::sb:
    case InstructionOp::sh:
    case InstructionOp::sw:
    case InstructionOp::swl:
    case InstructionOp::swr:
      return true;

      // These can fault on the branch address. Perhaps we should move this to the next instruction?
    case InstructionOp::j:
    case InstructionOp::jal:
    case InstructionOp::b:
    case InstructionOp::beq:
    case InstructionOp::bgtz:
    case InstructionOp::blez:
    case InstructionOp::bne:
      return true;

    case InstructionOp::funct:
    {
      switch (instruction.r.funct)
      {
        case InstructionFunct::sll:
        case InstructionFunct::srl:
        case InstructionFunct::sra:
        case InstructionFunct::sllv:
        case InstructionFunct::srlv:
        case InstructionFunct::srav:
        case InstructionFunct::and_:
        case InstructionFunct::or_:
        case InstructionFunct::xor_:
        case InstructionFunct::nor:
        case InstructionFunct::addu:
        case InstructionFunct::subu:
        case InstructionFunct::slt:
        case InstructionFunct::sltu:
        case InstructionFunct::mfhi:
        case InstructionFunct::mthi:
        case InstructionFunct::mflo:
        case InstructionFunct::mtlo:
        case InstructionFunct::mult:
        case InstructionFunct::multu:
        case InstructionFunct::div:
        case InstructionFunct::divu:
          return false;

        case InstructionFunct::jr:
        case InstructionFunct::jalr:
          return true;

        case InstructionFunct::add:
        case InstructionFunct::sub:
        case InstructionFunct::syscall:
        case InstructionFunct::break_:
        default:
          return true;
      }
    }

    default:
      return true;
  }
}

bool IsLoadDelayingInstruction(const Instruction& instruction)
{
  switch (instruction.op)
  {
    case InstructionOp::lb:
    case InstructionOp::lh:
    case InstructionOp::lw:
    case InstructionOp::lbu:
    case InstructionOp::lhu:
      return true;

    case InstructionOp::lwl:
    case InstructionOp::lwr:
      return false;

    default:
      return false;
  }
}

bool IsInvalidInstruction(const Instruction& instruction)
{
  // TODO
  return true;
}

} // namespace CPU