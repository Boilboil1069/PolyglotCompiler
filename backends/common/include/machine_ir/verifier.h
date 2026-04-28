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

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "backends/common/include/machine_ir/machine_ir.h"

namespace polyglot::backends::common::machine_ir {

/// @brief One verifier finding.
struct VerifierDiagnostic {
    enum class Severity { kWarning, kError };

    Severity     severity{Severity::kError};
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

/// @brief Stateless verifier; safe to instantiate per call.
template <typename TargetTraits, typename OpcodeT>
class MachineIRVerifier {
 public:
    VerifierResult Verify(const MachineFunction<TargetTraits, OpcodeT>& fn) const {
        VerifierResult result;
        std::string    snapshot;  // lazy; built on first error.
        bool           snapshot_ready = false;

        auto append = [&](VerifierDiagnostic::Severity severity,
                          std::string                  message,
                          const std::string&           bb_name,
                          std::size_t                  bb_index,
                          std::size_t                  instr_index) {
            VerifierDiagnostic diag;
            diag.severity          = severity;
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
                       "basic block is empty (no terminator)",
                       bb.name, bi, 0);
            } else if (!bb.instructions.back().terminator) {
                append(VerifierDiagnostic::Severity::kError,
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
                               "instruction uses negative virtual-register id",
                               bb.name, bi, ii);
                        continue;
                    }
                    if (defined_in_block.count(use) == 0 &&
                        defined_anywhere.count(use) == 0) {
                        append(VerifierDiagnostic::Severity::kError,
                               "use of virtual register that has no definition "
                               "anywhere in the function",
                               bb.name, bi, ii);
                    }
                }

                if (mi.def >= 0) {
                    if (!defined_in_block.insert(mi.def).second) {
                        append(VerifierDiagnostic::Severity::kError,
                               "duplicate definition of virtual register inside "
                               "the same basic block",
                               bb.name, bi, ii);
                    }
                }
            }
        }

        return result;
    }
};

}  // namespace polyglot::backends::common::machine_ir
