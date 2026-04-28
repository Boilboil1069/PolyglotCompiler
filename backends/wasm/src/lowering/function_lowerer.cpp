/**
 * @file     function_lowerer.cpp
 * @brief    IR function → WASM function body lowering
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "middle/include/ir/cfg.h"

#include "backends/wasm/include/internal/wasm_constants.h"
#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

using internal::kBlockTypeVoid;
using internal::kOpBlock;
using internal::kOpEnd;

void WasmTarget::LowerFunction(const ir::Function &fn, std::vector<std::uint8_t> &body) {
  // Build the locals declaration: count params as local.get indices
  // then declare additional locals for named SSA values.
  std::vector<WasmValType> local_types;

  // Collect additional locals from instructions that define values
  for (auto &blk : fn.blocks) {
    for (auto &inst : blk->instructions) {
      if (inst->HasResult()) {
        local_types.push_back(IRTypeToWasm(inst->type));
      }
    }
    for (auto &phi : blk->phis) {
      if (phi->HasResult()) {
        local_types.push_back(IRTypeToWasm(phi->type));
      }
    }
  }

  // Compress locals: consecutive runs of the same type
  std::vector<std::pair<std::uint32_t, WasmValType>> compressed;
  for (auto vt : local_types) {
    if (!compressed.empty() && compressed.back().second == vt) {
      compressed.back().first++;
    } else {
      compressed.push_back({1, vt});
    }
  }

  // Local declarations
  EmitU32Leb128(body, static_cast<std::uint32_t>(compressed.size()));
  for (auto &[count, vt] : compressed) {
    EmitU32Leb128(body, count);
    body.push_back(static_cast<std::uint8_t>(vt));
  }

  // Build block depth map: each IR basic block gets a WASM block wrapper
  // so that branch targets can be resolved to relative depths.
  block_depth_map_.clear();
  current_block_depth_ = 0;
  for (size_t i = 0; i < fn.blocks.size(); ++i) {
    block_depth_map_[fn.blocks[i]->name] = static_cast<std::uint32_t>(i);
  }

  // Emit instruction bytecode.  Each block is wrapped in a WASM block
  // so that br/br_if can target them by relative depth.
  for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
    body.push_back(kOpBlock);
    body.push_back(kBlockTypeVoid);
    current_block_depth_ = static_cast<std::uint32_t>(bi + 1);
  }
  for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
    auto &blk = fn.blocks[bi];
    for (auto &inst : blk->instructions) {
      LowerInstruction(inst, body);
    }
    if (blk->terminator) {
      LowerInstruction(blk->terminator, body);
    }
    body.push_back(kOpEnd); // close this block
  }

  // End marker for function body
  body.push_back(kOpEnd);
}

}  // namespace polyglot::backends::wasm
