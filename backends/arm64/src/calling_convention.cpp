#include "backends/arm64/include/arm64_target.h"
#include "backends/arm64/include/machine_ir.h"

#include <algorithm>
#include <ostream>
#include <vector>

namespace polyglot::backends::arm64 {
namespace {

// AAPCS64 calling convention for ARM64
const Register kIntegerArgRegs[] = {
    Register::kX0, Register::kX1, Register::kX2, Register::kX3,
    Register::kX4, Register::kX5, Register::kX6, Register::kX7
};

const Register kFloatArgRegs[] = {
    Register::kV0, Register::kV1, Register::kV2, Register::kV3,
    Register::kV4, Register::kV5, Register::kV6, Register::kV7
};

const Register kCalleeSavedRegs[] = {
    Register::kX19, Register::kX20, Register::kX21, Register::kX22,
    Register::kX23, Register::kX24, Register::kX25, Register::kX26,
    Register::kX27, Register::kX28, Register::kFp,  Register::kLr
};

const Register kVolatileRegs[] = {
    Register::kX0,  Register::kX1,  Register::kX2,  Register::kX3,
    Register::kX4,  Register::kX5,  Register::kX6,  Register::kX7,
    Register::kX8,  Register::kX9,  Register::kX10, Register::kX11,
    Register::kX12, Register::kX13, Register::kX14, Register::kX15,
    Register::kX16, Register::kX17
};

struct StackFrame {
    int total_size{0};
    int spill_area_size{0};
    int local_area_size{0};
    int arg_area_size{0};
    std::vector<Register> saved_regs;
};

} // namespace

struct CallingConvention {
    std::vector<Register> GetIntegerArgRegs() const {
        return std::vector<Register>(std::begin(kIntegerArgRegs), std::end(kIntegerArgRegs));
    }

    std::vector<Register> GetFloatArgRegs() const {
        return std::vector<Register>(std::begin(kFloatArgRegs), std::end(kFloatArgRegs));
    }

    std::vector<Register> GetCalleeSavedRegs() const {
        return std::vector<Register>(std::begin(kCalleeSavedRegs), std::end(kCalleeSavedRegs));
    }

    std::vector<Register> GetVolatileRegs() const {
        return std::vector<Register>(std::begin(kVolatileRegs), std::end(kVolatileRegs));
    }

    StackFrame ComputeStackFrame(const MachineFunction &fn, const AllocationResult &alloc) {
        StackFrame frame;
        
        frame.spill_area_size = alloc.stack_slots * 8;
        
        for (Register reg : GetCalleeSavedRegs()) {
            for (const auto &pair : alloc.vreg_to_phys) {
                if (pair.second == reg) {
                    if (std::find(frame.saved_regs.begin(), frame.saved_regs.end(), reg)
                        == frame.saved_regs.end()) {
                        frame.saved_regs.push_back(reg);
                    }
                    break;
                }
            }
        }
        
        int saved_regs_size = static_cast<int>(frame.saved_regs.size() * 8);
        
        int max_args = 0;
        for (const auto &bb : fn.blocks) {
            for (const auto &instr : bb.instructions) {
                if (instr.opcode == Opcode::kCall) {
                    int arg_count = static_cast<int>(instr.operands.size()) - 1;
                    max_args = std::max(max_args, arg_count);
                }
            }
        }
        
        int reg_arg_count = static_cast<int>(GetIntegerArgRegs().size());
        if (max_args > reg_arg_count) {
            frame.arg_area_size = (max_args - reg_arg_count) * 8;
        }
        
        // Align to 16 bytes (required by AAPCS64)
        frame.total_size = frame.spill_area_size + saved_regs_size + frame.arg_area_size;
        frame.total_size = (frame.total_size + 15) & ~15;
        
        return frame;
    }

    void EmitPrologue(std::ostream &os, const StackFrame &frame) {
        // Save FP and LR
        os << "  stp fp, lr, [sp, #-16]!\n";
        os << "  mov fp, sp\n";
        
        if (frame.total_size > 0) {
            if (frame.total_size < 4096) {
                os << "  sub sp, sp, #" << frame.total_size << "\n";
            } else {
                // Large stack frames need multiple instructions
                os << "  mov x16, #" << (frame.total_size & 0xFFFF) << "\n";
                if (frame.total_size > 0xFFFF) {
                    os << "  movk x16, #" << ((frame.total_size >> 16) & 0xFFFF) << ", lsl #16\n";
                }
                os << "  sub sp, sp, x16\n";
            }
        }
        
        // Save callee-saved registers
        int offset = frame.total_size;
        for (size_t i = 0; i < frame.saved_regs.size(); i += 2) {
            offset -= 16;
            if (i + 1 < frame.saved_regs.size()) {
                os << "  stp " << RegisterName(frame.saved_regs[i]) << ", "
                   << RegisterName(frame.saved_regs[i + 1])
                   << ", [sp, #" << offset << "]\n";
            } else {
                os << "  str " << RegisterName(frame.saved_regs[i])
                   << ", [sp, #" << offset << "]\n";
            }
        }
    }

    void EmitEpilogue(std::ostream &os, const StackFrame &frame) {
        // Restore callee-saved registers
        int offset = frame.total_size;
        for (size_t i = 0; i < frame.saved_regs.size(); i += 2) {
            offset -= 16;
            if (i + 1 < frame.saved_regs.size()) {
                os << "  ldp " << RegisterName(frame.saved_regs[i]) << ", "
                   << RegisterName(frame.saved_regs[i + 1])
                   << ", [sp, #" << offset << "]\n";
            } else {
                os << "  ldr " << RegisterName(frame.saved_regs[i])
                   << ", [sp, #" << offset << "]\n";
            }
        }
        
        os << "  mov sp, fp\n";
        os << "  ldp fp, lr, [sp], #16\n";
        os << "  ret\n";
    }

    void EmitCallSetup(std::ostream &os, const MachineInstr &call_instr,
                       const AllocationResult &alloc) {
        auto int_regs = GetIntegerArgRegs();
        auto float_regs = GetFloatArgRegs();
        
        int num_args = static_cast<int>(call_instr.operands.size()) - 1;
        int int_arg_idx = 0;
        int float_arg_idx = 0;
        int stack_offset = 0;
        
        for (int i = 0; i < num_args; ++i) {
            const auto &arg = call_instr.operands[i];
            bool is_float = arg.is_float;
            
            std::string arg_str;
            switch (arg.kind) {
                case Operand::Kind::kImm:
                    arg_str = "#" + std::to_string(arg.imm);
                    break;
                case Operand::Kind::kVReg: {
                    auto phys_it = alloc.vreg_to_phys.find(arg.vreg);
                    if (phys_it != alloc.vreg_to_phys.end()) {
                        arg_str = RegisterName(phys_it->second);
                    } else {
                        auto slot_it = alloc.vreg_to_slot.find(arg.vreg);
                        if (slot_it != alloc.vreg_to_slot.end()) {
                            int offset = (slot_it->second + 1) * 8;
                            arg_str = "[sp, #" + std::to_string(offset) + "]";
                        }
                    }
                    break;
                }
                default:
                    arg_str = "x0";
                    break;
            }
            
            if (is_float) {
                if (float_arg_idx < static_cast<int>(float_regs.size())) {
                    if (arg.kind == Operand::Kind::kImm || arg_str.find("[") == 0) {
                        os << "  ldr " << RegisterName(float_regs[float_arg_idx])
                           << ", " << arg_str << "\n";
                    } else {
                        os << "  fmov " << RegisterName(float_regs[float_arg_idx])
                           << ", " << arg_str << "\n";
                    }
                    ++float_arg_idx;
                } else {
                    os << "  str " << arg_str << ", [sp, #" << stack_offset << "]\n";
                    stack_offset += 8;
                }
            } else {
                if (int_arg_idx < static_cast<int>(int_regs.size())) {
                    if (arg.kind == Operand::Kind::kImm) {
                        os << "  mov " << RegisterName(int_regs[int_arg_idx])
                           << ", " << arg_str << "\n";
                    } else if (arg_str.find("[") == 0) {
                        os << "  ldr " << RegisterName(int_regs[int_arg_idx])
                           << ", " << arg_str << "\n";
                    } else {
                        os << "  mov " << RegisterName(int_regs[int_arg_idx])
                           << ", " << arg_str << "\n";
                    }
                    ++int_arg_idx;
                } else {
                    os << "  str " << arg_str << ", [sp, #" << stack_offset << "]\n";
                    stack_offset += 8;
                }
            }
        }
    }
};

CallingConvention &GetAAPCS64CallingConvention() {
    static CallingConvention cc;
    return cc;
}

std::vector<Register> GetAvailableRegisters() {
    CallingConvention cc;
    auto volatile_regs = cc.GetVolatileRegs();
    auto callee_saved = cc.GetCalleeSavedRegs();
    
    std::vector<Register> available = volatile_regs;
    available.insert(available.end(), callee_saved.begin(), callee_saved.end());
    
    return available;
}

} // namespace polyglot::backends::arm64
