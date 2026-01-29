#include "backends/x86_64/include/machine_ir.h"
#include "backends/x86_64/include/x86_register.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace polyglot::backends::x86_64 {
namespace {

// SysV ABI calling convention for x86-64
const Register kIntegerArgRegs[] = {
    Register::kRdi, Register::kRsi, Register::kRdx,
    Register::kRcx, Register::kR8,  Register::kR9
};

const Register kFloatArgRegs[] = {
    Register::kXmm0, Register::kXmm1, Register::kXmm2, Register::kXmm3,
    Register::kXmm4, Register::kXmm5, Register::kXmm6, Register::kXmm7
};

const Register kCalleeSavedRegs[] = {
    Register::kRbx, Register::kRbp, Register::kR12,
    Register::kR13, Register::kR14, Register::kR15
};

const Register kVolatileRegs[] = {
    Register::kRax, Register::kRdi, Register::kRsi, Register::kRdx,
    Register::kRcx, Register::kR8,  Register::kR9,  Register::kR10,
    Register::kR11
};

struct StackFrame {
    int total_size{0};         // Total stack frame size
    int spill_area_size{0};    // Size for spilled registers
    int local_area_size{0};    // Size for local variables
    int arg_area_size{0};      // Size for outgoing call arguments
    std::vector<Register> saved_regs;  // Callee-saved registers to save/restore
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

    // Compute stack frame layout
    StackFrame ComputeStackFrame(const MachineFunction &fn, const AllocationResult &alloc) {
        StackFrame frame;
        
        // Calculate spill area
        frame.spill_area_size = alloc.stack_slots * 8;
        
        // Determine which callee-saved registers are used
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
        
        // Add space for saved registers
        int saved_regs_size = static_cast<int>(frame.saved_regs.size() * 8);
        
        // Calculate maximum outgoing argument size
        int max_args = 0;
        for (const auto &bb : fn.blocks) {
            for (const auto &instr : bb.instructions) {
                if (instr.opcode == Opcode::kCall) {
                    int arg_count = static_cast<int>(instr.operands.size()) - 1; // subtract callee name
                    max_args = std::max(max_args, arg_count);
                }
            }
        }
        
        // Reserve space for arguments beyond registers (if any)
        int reg_arg_count = static_cast<int>(GetIntegerArgRegs().size());
        if (max_args > reg_arg_count) {
            frame.arg_area_size = (max_args - reg_arg_count) * 8;
        }
        
        // Align to 16 bytes (required by SysV ABI)
        frame.total_size = frame.spill_area_size + saved_regs_size + frame.arg_area_size;
        frame.total_size = (frame.total_size + 15) & ~15;
        
        return frame;
    }

    // Emit function prologue
    void EmitPrologue(std::ostream &os, const StackFrame &frame) {
        os << "  push rbp\n";
        os << "  mov rbp, rsp\n";
        
        if (frame.total_size > 0) {
            os << "  sub rsp, " << frame.total_size << "\n";
        }
        
        // Save callee-saved registers
        int offset = frame.total_size;
        for (Register reg : frame.saved_regs) {
            offset -= 8;
            os << "  mov [rbp - " << (frame.total_size - offset) << "], " 
               << RegisterName(reg) << "\n";
        }
    }

    // Emit function epilogue
    void EmitEpilogue(std::ostream &os, const StackFrame &frame) {
        // Restore callee-saved registers
        int offset = frame.total_size;
        for (Register reg : frame.saved_regs) {
            offset -= 8;
            os << "  mov " << RegisterName(reg) << ", [rbp - " 
               << (frame.total_size - offset) << "]\n";
        }
        
        os << "  mov rsp, rbp\n";
        os << "  pop rbp\n";
        os << "  ret\n";
    }

    // Emit call setup (move arguments to registers/stack)
    void EmitCallSetup(std::ostream &os, const MachineInstr &call_instr, 
                       const AllocationResult &alloc) {
        auto int_regs = GetIntegerArgRegs();
        auto float_regs = GetFloatArgRegs();
        
        int num_args = static_cast<int>(call_instr.operands.size()) - 1; // last is callee
        int int_arg_idx = 0;
        int float_arg_idx = 0;
        int stack_offset = 0;
        
        for (int i = 0; i < num_args; ++i) {
            const auto &arg = call_instr.operands[i];
            bool is_float = arg.is_float;
            
            std::ostringstream pre;
            std::string arg_str;
            switch (arg.kind) {
                case Operand::Kind::kImm:
                    arg_str = std::to_string(arg.imm);
                    break;
                case Operand::Kind::kVReg: {
                    auto phys_it = alloc.vreg_to_phys.find(arg.vreg);
                    if (phys_it != alloc.vreg_to_phys.end()) {
                        arg_str = RegisterName(phys_it->second);
                    } else {
                        auto slot_it = alloc.vreg_to_slot.find(arg.vreg);
                        if (slot_it != alloc.vreg_to_slot.end()) {
                            int offset = (slot_it->second + 1) * 8;
                            arg_str = "[rbp - " + std::to_string(offset) + "]";
                        }
                    }
                    break;
                }
                default:
                    arg_str = "rax";  // fallback
                    break;
            }
            
            if (is_float) {
                if (float_arg_idx < static_cast<int>(float_regs.size())) {
                    os << "  movsd " << RegisterName(float_regs[float_arg_idx]) 
                       << ", " << arg_str << "\n";
                    ++float_arg_idx;
                } else {
                    os << "  movsd [rsp + " << stack_offset << "], " << arg_str << "\n";
                    stack_offset += 8;
                }
            } else {
                if (int_arg_idx < static_cast<int>(int_regs.size())) {
                    os << "  mov " << RegisterName(int_regs[int_arg_idx]) 
                       << ", " << arg_str << "\n";
                    ++int_arg_idx;
                } else {
                    os << "  mov [rsp + " << stack_offset << "], " << arg_str << "\n";
                    stack_offset += 8;
                }
            }
        }
    }
};

// Export calling convention utilities
CallingConvention &GetSysVCallingConvention() {
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

} // namespace polyglot::backends::x86_64
