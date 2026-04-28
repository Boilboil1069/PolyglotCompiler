/**
 * @file     verifier.h
 * @brief    Target-independent MachineIR structural / def-use verifier.
 *
 * The verifier enforces the invariants every backend code generator is
 * expected to maintain after instruction selection (and again after register
 * allocation, where physical-register and stack-slot assignments must not
 * have invalidated the def-use shape):
 *
 *   1. Every basic block must end with a `terminator==true` instruction.
 *   2. Every `use` must be reachable from a prior definition — either an
 *      earlier instruction in the same basic block, or a definition that
 *      appears in some block of the function (the verifier intentionally
 *      treats the function-wide definition set as the cross-block oracle so
 *      that SSA-after-isel inputs are accepted; full SSA-aware data-flow
 *      validation is out of scope for the structural verifier).
 *   3. The instruction at `def` >= 0 contributes a new virtual register; the
 *      verifier flags duplicate definitions of the same vreg in the same
 *      block as a soundness violation.
 *
 * ABI / register-class / stack-slot-size checks require an explicit ABI model
 * and are out of scope here.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "backends/common/include/machine_ir/machine_ir.h"

namespace polyglot::backends::common::machine_ir {

/// @brief One verifier finding.
struct VerifierDiagnostic {
    enum class Severity { kWarning, kError };

    /// @brief Categorical reason for the diagnostic.  ABI-aware codes
    ///        (kAbi*) are only emitted when an `AbiContract` is supplied.
    enum class Code {
        kStructural,           ///< Default for the original three rules.
        kAbiCallArityExceeded, ///< Rule (d): call has more args than the ABI
                               ///< call site can plausibly carry.
        kAbiVolatileRegLeak,   ///< Rule (e): a volatile physical register is
                               ///< read after a call without an intervening def.
    };

    Severity     severity{Severity::kError};
    Code         code{Code::kStructural};
    std::string  message;
    std::string  function_name;
    std::string  block_name;
    std::size_t  block_index{0};
    std::size_t  instruction_index{0};
    /// Snapshot rendering of the offending function (only populated on the
    /// first error to keep diagnostic payloads bounded).
    std::string  snapshot;
};

/// @brief Result of running the verifier.
struct VerifierResult {
    std::vector<VerifierDiagnostic> diagnostics;

    /// Convenience: true when no diagnostic of `kError` severity is present.
    bool ok() const {
        for (const auto& d : diagnostics) {
            if (d.severity == VerifierDiagnostic::Severity::kError) {
                return false;
            }
        }
        return true;
    }
};

/// @brief ABI-side context that enables verifier rules (d) and (e).
///
/// Callers populate this struct with the per-target opcode value that means
/// "call instruction" and with the volatile / argument register tables of
/// the active ABI (typically the same vectors a backend's
/// `common::abi::CallingConvention<TargetTraits>` returns).  When the
/// pointer passed to `Verify` is null, only the original structural rules
/// (1-3) execute and no `kAbi*` diagnostics are produced — guaranteeing
/// existing callers that do not opt in observe zero behaviour change.
template <typename TargetTraits, typename OpcodeT>
struct AbiContract {
    using Register = typename TargetTraits::Register;

    OpcodeT               call_opcode{};
    /// Maximum number of total operands a single call instruction may carry
    /// before rule (d) fires.  Sized as `IntegerArgRegs.size() +
    /// FloatArgRegs.size() + kStackArgSlack + 1` (the trailing +1 accounts
    /// for the callee operand the back-end appends to every call).
    std::size_t           max_call_operands{0};
    /// Volatile physical-register set; rule (e) flags any `kPhysReg` read
    /// of a register in this set on the first instruction of the
    /// current call's continuation that itself does not redefine it.
    std::vector<Register> volatile_regs;
};

/// @brief Stateless verifier; safe to instantiate per call.
template <typename TargetTraits, typename OpcodeT>
class MachineIRVerifier {
 public:
    using Abi = AbiContract<TargetTraits, OpcodeT>;

    /// @brief Run the structural rules (1-3) over @p fn.
    VerifierResult Verify(const MachineFunction<TargetTraits, OpcodeT>& fn) const {
        return Verify(fn, /*abi=*/nullptr);
    }

    /// @brief Run the structural rules and, when @p abi is non-null, the
    ///        ABI-aware rules (d) and (e) on top.
    VerifierResult Verify(const MachineFunction<TargetTraits, OpcodeT>& fn,
                          const Abi*                                    abi) const {
        VerifierResult result;
        std::string    snapshot;  // lazy; built on first error.
        bool           snapshot_ready = false;

        auto append = [&](VerifierDiagnostic::Severity severity,
                          VerifierDiagnostic::Code     code,
                          std::string                  message,
                          const std::string&           bb_name,
                          std::size_t                  bb_index,
                          std::size_t                  instr_index) {
            VerifierDiagnostic diag;
            diag.severity          = severity;
            diag.code              = code;
            diag.message           = std::move(message);
            diag.function_name     = fn.name;
            diag.block_name        = bb_name;
            diag.block_index       = bb_index;
            diag.instruction_index = instr_index;
            if (severity == VerifierDiagnostic::Severity::kError && !snapshot_ready) {
                snapshot       = Print(fn);
                snapshot_ready = true;
            }
            diag.snapshot = snapshot;
            result.diagnostics.push_back(std::move(diag));
        };

        // First pass: collect the set of vregs ever defined anywhere in the
        // function. Used so a use that references a vreg defined in another
        // block is accepted (BBs are reached by control flow, which the
        // structural verifier does not model precisely).
        std::unordered_set<int> defined_anywhere;
        for (const auto& bb : fn.blocks) {
            for (const auto& mi : bb.instructions) {
                if (mi.def >= 0) {
                    defined_anywhere.insert(mi.def);
                }
            }
        }

        for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
            const auto& bb = fn.blocks[bi];

            // Rule 1: BB must end with a terminator. An empty BB is also a
            // structural error.
            if (bb.instructions.empty()) {
                append(VerifierDiagnostic::Severity::kError,
                       VerifierDiagnostic::Code::kStructural,
                       "basic block is empty (no terminator)",
                       bb.name, bi, 0);
            } else if (!bb.instructions.back().terminator) {
                append(VerifierDiagnostic::Severity::kError,
                       VerifierDiagnostic::Code::kStructural,
                       "basic block does not end with a terminator instruction",
                       bb.name, bi, bb.instructions.size() - 1);
            }

            // Rules 2 & 3: walk instructions, tracking same-block defs.
            std::unordered_set<int> defined_in_block;
            for (std::size_t ii = 0; ii < bb.instructions.size(); ++ii) {
                const auto& mi = bb.instructions[ii];

                for (int use : mi.uses) {
                    if (use < 0) {
                        append(VerifierDiagnostic::Severity::kError,
                               VerifierDiagnostic::Code::kStructural,
                               "instruction uses negative virtual-register id",
                               bb.name, bi, ii);
                        continue;
                    }
                    if (defined_in_block.count(use) == 0 &&
                        defined_anywhere.count(use) == 0) {
                        append(VerifierDiagnostic::Severity::kError,
                               VerifierDiagnostic::Code::kStructural,
                               "use of virtual register that has no definition "
                               "anywhere in the function",
                               bb.name, bi, ii);
                    }
                }

                if (mi.def >= 0) {
                    if (!defined_in_block.insert(mi.def).second) {
                        append(VerifierDiagnostic::Severity::kError,
                               VerifierDiagnostic::Code::kStructural,
                               "duplicate definition of virtual register inside "
                               "the same basic block",
                               bb.name, bi, ii);
                    }
                }
            }
        }

        // ----- ABI-aware rules (only when an AbiContract is supplied) -----
        if (abi != nullptr) {
            using Register = typename TargetTraits::Register;

            for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
                const auto& bb = fn.blocks[bi];
                bool        in_call_window = false;  // post-call, pre-redef.

                for (std::size_t ii = 0; ii < bb.instructions.size(); ++ii) {
                    const auto& mi = bb.instructions[ii];

                    // Rule (d): call instructions must not exceed the ABI
                    // call-site capacity. The capacity already includes
                    // the trailing callee operand.
                    if (mi.opcode == abi->call_opcode &&
                        abi->max_call_operands > 0 &&
                        mi.operands.size() > abi->max_call_operands) {
                        append(VerifierDiagnostic::Severity::kError,
                               VerifierDiagnostic::Code::kAbiCallArityExceeded,
                               "call instruction has more operands than the "
                               "active ABI calling convention can carry",
                               bb.name, bi, ii);
                    }

                    // Rule (e): a volatile physical register is read on the
                    // first instruction of the call's continuation.  Only
                    // meaningful after register allocation, so the check is
                    // restricted to `Operand::Kind::kPhysReg` operands.
                    if (in_call_window) {
                        for (const auto& op : mi.operands) {
                            if (op.kind != Operand<TargetTraits>::Kind::kPhysReg) {
                                continue;
                            }
                            const Register r = op.phys;
                            const bool is_volatile =
                                std::find(abi->volatile_regs.begin(),
                                          abi->volatile_regs.end(), r) !=
                                abi->volatile_regs.end();
                            if (is_volatile) {
                                append(VerifierDiagnostic::Severity::kError,
                                       VerifierDiagnostic::Code::kAbiVolatileRegLeak,
                                       "physical volatile register read in the "
                                       "continuation of a call without an "
                                       "intervening definition",
                                       bb.name, bi, ii);
                                break;
                            }
                        }
                        in_call_window = false;  // window covers exactly one instr.
                    }

                    if (mi.opcode == abi->call_opcode) {
                        in_call_window = true;
                    }
                }
            }
        }

        return result;
    }
};

}  // namespace polyglot::backends::common::machine_ir
