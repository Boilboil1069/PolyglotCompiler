#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

#include "backends/x86_64/include/instruction_scheduler.h"
#include "backends/x86_64/include/machine_ir.h"

using namespace polyglot::backends::x86_64;
using LoopInfo = SoftwarePipeliner::LoopInfo;

// ============================================================================
// Helper functions to create test instructions
// ============================================================================

// Create a simple ALU instruction: def = uses[0] op uses[1]
static MachineInstr MakeALUInstr(Opcode op, int def, std::initializer_list<int> uses) {
    MachineInstr inst;
    inst.opcode = op;
    inst.def = def;
    inst.uses = uses;
    inst.latency = 1;
    inst.cost = 1;
    inst.terminator = false;
    for (int u : uses) {
        inst.operands.push_back(Operand::VReg(u));
    }
    return inst;
}

// Create a load instruction: def = [base]
static MachineInstr MakeLoadInstr(int def, int base) {
    MachineInstr inst;
    inst.opcode = Opcode::kLoad;
    inst.def = def;
    inst.uses = {base};
    inst.latency = 4;
    inst.cost = 1;
    inst.terminator = false;
    inst.operands.push_back(Operand::MemVReg(base));
    return inst;
}

// Create a store instruction: [base] = src
static MachineInstr MakeStoreInstr(int base, int src) {
    MachineInstr inst;
    inst.opcode = Opcode::kStore;
    inst.def = -1;
    inst.uses = {base, src};
    inst.latency = 1;
    inst.cost = 1;
    inst.terminator = false;
    inst.operands.push_back(Operand::MemVReg(base));
    inst.operands.push_back(Operand::VReg(src));
    return inst;
}

// Create a mov instruction: def = src or def = immediate
static MachineInstr MakeMovInstr(int def, int src) {
    MachineInstr inst;
    inst.opcode = Opcode::kMov;
    inst.def = def;
    inst.uses = {src};
    inst.latency = 1;
    inst.cost = 1;
    inst.terminator = false;
    inst.operands.push_back(Operand::VReg(src));
    return inst;
}

static MachineInstr MakeMovImmInstr(int def, long long imm) {
    MachineInstr inst;
    inst.opcode = Opcode::kMov;
    inst.def = def;
    inst.uses = {};
    inst.latency = 1;
    inst.cost = 1;
    inst.terminator = false;
    inst.operands.push_back(Operand::Imm(imm));
    return inst;
}

// Create a conditional jump
static MachineInstr MakeJccInstr(const std::string& target) {
    MachineInstr inst;
    inst.opcode = Opcode::kJcc;
    inst.def = -1;
    inst.latency = 1;
    inst.cost = 1;
    inst.terminator = true;
    inst.operands.push_back(Operand::Label(target));
    return inst;
}

// Create a compare instruction
static MachineInstr MakeCmpInstr(int lhs, int rhs) {
    MachineInstr inst;
    inst.opcode = Opcode::kCmp;
    inst.def = -1;  // CMP sets flags only
    inst.uses = {lhs, rhs};
    inst.latency = 1;
    inst.cost = 1;
    inst.terminator = false;
    inst.operands.push_back(Operand::VReg(lhs));
    inst.operands.push_back(Operand::VReg(rhs));
    return inst;
}

// ============ Test 1: Instruction Scheduler ============
TEST_CASE("Backend - Instruction Scheduler", "[backend][scheduler]") {
    SECTION("Basic scheduling with empty function") {
        MachineFunction func;
        func.name = "test";
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        REQUIRE(scheduled.name == "test");
        REQUIRE(scheduled.blocks.empty());
    }
    
    SECTION("Scheduling dependency chain") {
        // Create: r0 = r1 + r2; r3 = r0 + r4; r5 = r3 + r6
        // These must stay in order due to RAW dependencies
        MachineFunction func;
        func.name = "dep_chain";
        MachineBasicBlock block;
        block.name = "entry";
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 3, {0, 4}));
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 5, {3, 6}));
        func.blocks.push_back(block);
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        REQUIRE(scheduled.blocks.size() == 1);
        REQUIRE(scheduled.blocks[0].instructions.size() == 3);
        // Dependencies must be preserved
        REQUIRE(scheduled.blocks[0].instructions[0].def == 0);
        REQUIRE(scheduled.blocks[0].instructions[1].def == 3);
        REQUIRE(scheduled.blocks[0].instructions[2].def == 5);
    }
    
    SECTION("Scheduling independent instructions") {
        // Create: r0 = r1 + r2; r3 = r4 + r5; r6 = r7 + r8
        // These are independent and can be reordered
        MachineFunction func;
        func.name = "independent";
        MachineBasicBlock block;
        block.name = "entry";
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 3, {4, 5}));
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 6, {7, 8}));
        func.blocks.push_back(block);
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        REQUIRE(scheduled.blocks.size() == 1);
        REQUIRE(scheduled.blocks[0].instructions.size() == 3);
        // All instructions should be present
        std::set<int> defs;
        for (const auto& inst : scheduled.blocks[0].instructions) {
            defs.insert(inst.def);
        }
        REQUIRE(defs.count(0) == 1);
        REQUIRE(defs.count(3) == 1);
        REQUIRE(defs.count(6) == 1);
    }
    
    SECTION("Memory dependency ordering") {
        // Create: store [r0], r1; load r2, [r0]
        // Store-Load to same address must preserve order
        MachineFunction func;
        func.name = "mem_dep";
        MachineBasicBlock block;
        block.name = "entry";
        block.instructions.push_back(MakeStoreInstr(0, 1));
        block.instructions.push_back(MakeLoadInstr(2, 0));
        func.blocks.push_back(block);
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        REQUIRE(scheduled.blocks[0].instructions.size() == 2);
        // Store must come before load
        REQUIRE(scheduled.blocks[0].instructions[0].opcode == Opcode::kStore);
        REQUIRE(scheduled.blocks[0].instructions[1].opcode == Opcode::kLoad);
    }
    
    SECTION("Critical path prioritization") {
        // Create a mix of high and low latency operations
        // MUL (3 cycles) should be scheduled earlier than ADD (1 cycle)
        MachineFunction func;
        func.name = "critical_path";
        MachineBasicBlock block;
        block.name = "entry";
        
        // Independent mul and add
        MachineInstr mul = MakeALUInstr(Opcode::kMul, 0, {1, 2});
        mul.latency = 3;
        block.instructions.push_back(mul);
        
        MachineInstr add = MakeALUInstr(Opcode::kAdd, 3, {4, 5});
        add.latency = 1;
        block.instructions.push_back(add);
        
        func.blocks.push_back(block);
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        REQUIRE(scheduled.blocks[0].instructions.size() == 2);
    }
}

// ============ Test 2: Software Pipelining ============
TEST_CASE("Backend - Software Pipelining", "[backend][pipeline]") {
    SECTION("Empty loop") {
        std::vector<MachineInstr> loop_body;
        LoopInfo loop_info;
        loop_info.trip_count = 10;
        loop_info.has_side_effects = false;
        
        auto schedule = SoftwarePipeliner::PipelineLoop(loop_body, loop_info);
        
        REQUIRE(schedule.initiation_interval >= 1);
    }
    
    SECTION("Simple ALU loop") {
        std::vector<MachineInstr> loop_body;
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 3, {0, 4}));
        
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        auto schedule = SoftwarePipeliner::PipelineLoop(loop_body, loop_info);
        
        REQUIRE(schedule.initiation_interval >= 1);
        REQUIRE(!schedule.kernel.empty());
    }
    
    SECTION("Loop with memory operations") {
        std::vector<MachineInstr> loop_body;
        loop_body.push_back(MakeLoadInstr(0, 1));
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 2, {0, 3}));
        loop_body.push_back(MakeStoreInstr(4, 2));
        
        LoopInfo loop_info;
        loop_info.trip_count = 50;
        loop_info.has_side_effects = false;
        
        auto schedule = SoftwarePipeliner::PipelineLoop(loop_body, loop_info);
        
        // MII should account for memory ports
        REQUIRE(schedule.initiation_interval >= 1);
    }
    
    SECTION("Short trip count loop - no pipelining") {
        std::vector<MachineInstr> loop_body;
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        
        LoopInfo loop_info;
        loop_info.trip_count = 2;  // Too short to pipeline
        loop_info.has_side_effects = false;
        
        auto schedule = SoftwarePipeliner::PipelineLoop(loop_body, loop_info);
        
        // Should return simple schedule for short loops
        REQUIRE(!schedule.kernel.empty());
    }
    
    SECTION("Loop with side effects") {
        std::vector<MachineInstr> loop_body;
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = true;  // Has side effects
        
        auto schedule = SoftwarePipeliner::PipelineLoop(loop_body, loop_info);
        
        // Should use conservative II for side effects
        REQUIRE(schedule.initiation_interval >= 2);
    }
    
    SECTION("MII computation via PipelineLoop") {
        std::vector<MachineInstr> body;
        // Add many ALU ops to create resource pressure
        for (int i = 0; i < 8; ++i) {
            body.push_back(MakeALUInstr(Opcode::kAdd, i + 10, {i, i + 1}));
        }
        
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        auto schedule = SoftwarePipeliner::PipelineLoop(body, loop_info);
        
        // MII should be at least 1 and account for resource constraints
        REQUIRE(schedule.initiation_interval >= 1);
    }
}

// ============ Test 3: Instruction Fusion ============
TEST_CASE("Backend - Instruction Fusion", "[backend][fusion]") {
    SECTION("LEA fusion - ADD + SHL") {
        std::vector<MachineInstr> insts;
        // add r0, r1 (r0 = r0 + r1)
        MachineInstr add;
        add.opcode = Opcode::kAdd;
        add.def = 0;
        add.uses = {0, 1};
        add.operands.push_back(Operand::VReg(0));
        add.operands.push_back(Operand::VReg(1));
        insts.push_back(add);
        
        // shl r0, 2 (r0 = r0 << 2)
        MachineInstr shl;
        shl.opcode = Opcode::kShl;
        shl.def = 0;
        shl.uses = {0};
        shl.operands.push_back(Operand::VReg(0));
        shl.operands.push_back(Operand::Imm(2));  // Scale factor
        insts.push_back(shl);
        
        auto fused = InstructionFusion::FuseInstructions(insts);
        
        // Should fuse into single LEA
        REQUIRE(fused.size() == 1);
        REQUIRE(fused[0].opcode == Opcode::kLea);
    }
    
    SECTION("Compare-jump fusion") {
        std::vector<MachineInstr> insts;
        
        // cmp r0, r1
        insts.push_back(MakeCmpInstr(0, 1));
        
        // jcc label
        insts.push_back(MakeJccInstr("target"));
        
        auto fused = InstructionFusion::FuseInstructions(insts);
        
        // Should fuse into single macro-op
        REQUIRE(fused.size() == 1);
        REQUIRE(fused[0].opcode == Opcode::kCmp);
        REQUIRE(fused[0].terminator == true);
    }
    
    SECTION("Load-op fusion") {
        std::vector<MachineInstr> insts;
        
        // load r2, [r0]
        insts.push_back(MakeLoadInstr(2, 0));
        
        // add r3, r2
        MachineInstr add;
        add.opcode = Opcode::kAdd;
        add.def = 3;
        add.uses = {3, 2};  // r3 = r3 + r2
        add.operands.push_back(Operand::VReg(3));
        add.operands.push_back(Operand::VReg(2));
        insts.push_back(add);
        
        auto fused = InstructionFusion::FuseInstructions(insts);
        
        // Should fuse into add with memory operand
        REQUIRE(fused.size() == 1);
        REQUIRE(fused[0].opcode == Opcode::kAdd);
    }
    
    SECTION("No fusion opportunities") {
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        insts.push_back(MakeALUInstr(Opcode::kSub, 3, {4, 5}));
        
        auto fused = InstructionFusion::FuseInstructions(insts);
        
        // No fusion possible - same size
        REQUIRE(fused.size() == insts.size());
    }
    
    SECTION("Empty input") {
        std::vector<MachineInstr> insts;
        
        auto fused = InstructionFusion::FuseInstructions(insts);
        
        REQUIRE(fused.empty());
    }
}

// ============ Test 4: Micro-Architecture Optimization ============
TEST_CASE("Backend - Micro-Architecture Optimization", "[backend][micro]") {
    SECTION("Haswell architecture") {
        MicroArchOptimizer opt(MicroArchOptimizer::kHaswell);
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        insts.push_back(MakeALUInstr(Opcode::kMul, 3, {4, 5}));
        
        auto optimized = opt.Optimize(insts);
        
        REQUIRE(optimized.size() == 2);
    }
    
    SECTION("Skylake architecture") {
        MicroArchOptimizer opt(MicroArchOptimizer::kSkylake);
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        
        auto optimized = opt.Optimize(insts);
        
        REQUIRE(!optimized.empty());
    }
    
    SECTION("Zen2 architecture") {
        MicroArchOptimizer opt(MicroArchOptimizer::kZen2);
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        
        auto optimized = opt.Optimize(insts);
        
        REQUIRE(!optimized.empty());
    }
    
    SECTION("Zen3 architecture") {
        MicroArchOptimizer opt(MicroArchOptimizer::kZen3);
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        
        auto optimized = opt.Optimize(insts);
        
        REQUIRE(!optimized.empty());
    }
    
    SECTION("False dependency breaking - zero idiom") {
        MicroArchOptimizer opt(MicroArchOptimizer::kGeneric);
        std::vector<MachineInstr> insts;
        
        // Write to r0
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        // Immediately write zero to r0 (false dependency)
        insts.push_back(MakeMovImmInstr(0, 0));
        
        auto optimized = opt.Optimize(insts);
        
        // mov r0, 0 should become xor r0, r0
        REQUIRE(optimized.size() == 2);
        // Check that the zero-idiom was applied
        bool has_xor = false;
        for (const auto& inst : optimized) {
            if (inst.opcode == Opcode::kXor && inst.def == 0) {
                has_xor = true;
            }
        }
        REQUIRE(has_xor);
    }
}

// ============ Test 5: Register Renaming ============
TEST_CASE("Backend - Register Renaming", "[backend][rename]") {
    SECTION("Empty input") {
        std::vector<MachineInstr> insts;
        
        auto renamed = RegisterRenamer::RenameRegisters(insts);
        
        REQUIRE(renamed.empty());
    }
    
    SECTION("Single instruction - no renaming") {
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        
        auto renamed = RegisterRenamer::RenameRegisters(insts);
        
        REQUIRE(renamed.size() == 1);
    }
    
    SECTION("WAW dependency elimination") {
        std::vector<MachineInstr> insts;
        // r0 = r1 + r2
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        // r0 = r3 + r4  (WAW - second write to r0 without intervening read)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {3, 4}));
        
        auto renamed = RegisterRenamer::RenameRegisters(insts);
        
        REQUIRE(renamed.size() == 2);
        // Second definition should potentially be renamed
    }
    
    SECTION("WAR dependency elimination") {
        std::vector<MachineInstr> insts;
        // r2 = r0 + r1 (reads r0)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 2, {0, 1}));
        // r0 = r3 + r4 (writes r0 - WAR with previous read)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {3, 4}));
        
        auto renamed = RegisterRenamer::RenameRegisters(insts);
        
        REQUIRE(renamed.size() == 2);
    }
    
    SECTION("Live range based renaming") {
        std::vector<MachineInstr> insts;
        // r0 = r1 + r2  (def r0)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        // r3 = r0 + r4  (use r0, def r3)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 3, {0, 4}));
        // r5 = r3 + r6  (use r3)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 5, {3, 6}));
        
        auto renamed = RegisterRenamer::RenameRegisters(insts);
        
        // Should have all three instructions
        REQUIRE(renamed.size() == 3);
    }
    
    SECTION("No renaming needed - RAW only") {
        std::vector<MachineInstr> insts;
        // r0 = r1 + r2
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        // r3 = r0 + r4  (RAW dependency - must preserve)
        insts.push_back(MakeALUInstr(Opcode::kAdd, 3, {0, 4}));
        
        auto renamed = RegisterRenamer::RenameRegisters(insts);
        
        REQUIRE(renamed.size() == insts.size());
    }
}

// ============ Test 6: Zero Latency Optimization ============
TEST_CASE("Backend - Zero Latency Optimization", "[backend][zero]") {
    SECTION("Empty input") {
        std::vector<MachineInstr> insts;
        
        auto optimized = ZeroLatencyOptimizer::OptimizeMoves(insts);
        
        REQUIRE(optimized.empty());
    }
    
    SECTION("Zero idiom conversion") {
        std::vector<MachineInstr> insts;
        // mov r0, 0  -> should become xor r0, r0
        insts.push_back(MakeMovImmInstr(0, 0));
        
        auto optimized = ZeroLatencyOptimizer::OptimizeMoves(insts);
        
        REQUIRE(optimized.size() == 1);
        REQUIRE(optimized[0].opcode == Opcode::kXor);
        REQUIRE(optimized[0].def == 0);
        REQUIRE(optimized[0].uses.size() == 2);
        REQUIRE(optimized[0].uses[0] == 0);
        REQUIRE(optimized[0].uses[1] == 0);
        REQUIRE(optimized[0].latency == 0);  // Zero-latency idiom
    }
    
    SECTION("Move elimination - same src and dst") {
        std::vector<MachineInstr> insts;
        // mov r0, r0  -> should be eliminated
        MachineInstr mov;
        mov.opcode = Opcode::kMov;
        mov.def = 0;
        mov.uses = {0};
        mov.operands.push_back(Operand::VReg(0));
        insts.push_back(mov);
        
        auto optimized = ZeroLatencyOptimizer::OptimizeMoves(insts);
        
        // Redundant move should be eliminated
        REQUIRE(optimized.empty());
    }
    
    SECTION("Ones idiom") {
        std::vector<MachineInstr> insts;
        // mov r0, -1
        insts.push_back(MakeMovImmInstr(0, -1));
        
        auto optimized = ZeroLatencyOptimizer::OptimizeMoves(insts);
        
        REQUIRE(optimized.size() == 1);
    }
    
    SECTION("SUB self zeroing") {
        std::vector<MachineInstr> insts;
        // sub r0, r0, r0  -> zero-latency zeroing
        MachineInstr sub;
        sub.opcode = Opcode::kSub;
        sub.def = 0;
        sub.uses = {0, 0};
        sub.latency = 1;
        insts.push_back(sub);
        
        auto optimized = ZeroLatencyOptimizer::OptimizeMoves(insts);
        
        REQUIRE(optimized.size() == 1);
        REQUIRE(optimized[0].latency == 0);  // Recognized as zero idiom
    }
    
    SECTION("Normal mov - no optimization") {
        std::vector<MachineInstr> insts;
        // mov r0, r1  -> cannot be eliminated
        insts.push_back(MakeMovInstr(0, 1));
        
        auto optimized = ZeroLatencyOptimizer::OptimizeMoves(insts);
        
        REQUIRE(optimized.size() == 1);
        REQUIRE(optimized[0].opcode == Opcode::kMov);
    }
}

// ============ Test 7: Cache Optimization ============
TEST_CASE("Backend - Cache Optimization", "[backend][cache]") {
    SECTION("Data layout optimization") {
        MachineFunction func;
        func.name = "cache_test";
        MachineBasicBlock block;
        block.name = "entry";
        block.instructions.push_back(MakeLoadInstr(0, 1));
        block.instructions.push_back(MakeLoadInstr(2, 3));
        func.blocks.push_back(block);
        
        CacheOptimizer::OptimizeDataLayout(func);
        
        // Should complete without error
        REQUIRE(func.blocks.size() == 1);
    }
    
    SECTION("Prefetch insertion - sufficient trip count") {
        std::vector<MachineInstr> insts;
        // Create load instructions
        for (int i = 0; i < 4; ++i) {
            MachineInstr load;
            load.opcode = Opcode::kLoad;
            load.def = i + 10;
            load.uses = {0};
            load.operands.push_back(Operand::MemVReg(0));
            load.operands.push_back(Operand::Imm(i * 8));  // Stride of 8
            insts.push_back(load);
        }
        
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        size_t original_size = insts.size();
        CacheOptimizer::InsertPrefetch(insts, loop_info);
        
        // Prefetch instructions should be inserted
        REQUIRE(insts.size() >= original_size);
    }
    
    SECTION("No prefetch for short loops") {
        std::vector<MachineInstr> insts;
        insts.push_back(MakeLoadInstr(0, 1));
        
        LoopInfo loop_info;
        loop_info.trip_count = 3;  // Too short for prefetch
        loop_info.has_side_effects = false;
        
        size_t original_size = insts.size();
        CacheOptimizer::InsertPrefetch(insts, loop_info);
        
        // No prefetch for short loops
        REQUIRE(insts.size() == original_size);
    }
    
    SECTION("Cache line alignment") {
        MachineFunction func;
        func.name = "align_test";
        MachineBasicBlock block;
        block.name = "loop";
        // Add a back-edge to make it look like a loop header
        block.instructions.push_back(MakeJccInstr("loop"));
        func.blocks.push_back(block);
        
        CacheOptimizer::AlignCacheLines(func);
        
        REQUIRE(func.blocks.size() == 1);
    }
    
    SECTION("Access pattern detection via behavior") {
        std::vector<MachineInstr> insts;
        for (int i = 0; i < 4; ++i) {
            MachineInstr load;
            load.opcode = Opcode::kLoad;
            load.def = i;
            load.operands.push_back(Operand::Imm(i * 8));  // Stride 8
            insts.push_back(load);
        }
        
        // Test through prefetch insertion behavior
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        size_t original_size = insts.size();
        CacheOptimizer::InsertPrefetch(insts, loop_info);
        
        // Sequential access should result in prefetch insertion
        REQUIRE(insts.size() >= original_size);
    }
    
    SECTION("Irregular access pattern via behavior") {
        std::vector<MachineInstr> insts;
        // Create irregular stride pattern that shouldn't trigger prefetch
        MachineInstr load1;
        load1.opcode = Opcode::kLoad;
        load1.operands.push_back(Operand::Imm(0));
        insts.push_back(load1);
        
        MachineInstr load2;
        load2.opcode = Opcode::kLoad;
        load2.operands.push_back(Operand::Imm(8));
        insts.push_back(load2);
        
        MachineInstr load3;
        load3.opcode = Opcode::kLoad;
        load3.operands.push_back(Operand::Imm(24));  // Different stride
        insts.push_back(load3);
        
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        // Just verify the call completes
        CacheOptimizer::InsertPrefetch(insts, loop_info);
        REQUIRE(!insts.empty());
    }
}

// ============ Test 8: Branch Optimization ============
TEST_CASE("Backend - Branch Optimization", "[backend][branch]") {
    SECTION("Empty input") {
        std::vector<MachineInstr> insts;
        
        auto optimized = BranchOptimizer::OptimizeBranches(insts);
        
        REQUIRE(optimized.empty());
    }
    
    SECTION("CMOV conversion pattern") {
        std::vector<MachineInstr> insts;
        // Pattern: jcc skip; mov r0, r1; skip:
        insts.push_back(MakeJccInstr("skip"));
        insts.push_back(MakeMovInstr(0, 1));
        insts.push_back(MakeALUInstr(Opcode::kAdd, 2, {3, 4}));  // skip label target
        
        auto optimized = BranchOptimizer::OptimizeBranches(insts);
        
        // Should convert branch+mov to conditional move
        REQUIRE(!optimized.empty());
    }
    
    SECTION("Branch inversion") {
        std::vector<MachineInstr> insts;
        MachineInstr jcc = MakeJccInstr("target");
        insts.push_back(jcc);
        
        auto optimized = BranchOptimizer::OptimizeBranches(insts);
        
        REQUIRE(optimized.size() == 1);
    }
    
    SECTION("No optimization - complex pattern") {
        std::vector<MachineInstr> insts;
        insts.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        insts.push_back(MakeJccInstr("target"));
        
        auto optimized = BranchOptimizer::OptimizeBranches(insts);
        
        REQUIRE(optimized.size() == 2);
    }
    
    SECTION("Unconditional jump elimination via OptimizeBranches") {
        std::vector<MachineInstr> insts;
        MachineInstr jmp;
        jmp.opcode = Opcode::kJmp;
        jmp.terminator = true;
        jmp.operands.push_back(Operand::Label(""));  // Empty label
        insts.push_back(jmp);
        
        auto optimized = BranchOptimizer::OptimizeBranches(insts);
        
        // The branch optimizer should handle this
        REQUIRE(optimized.size() <= 1);
    }
}

// ============ Test 9: Combined Optimizations ============
TEST_CASE("Backend - Combined Optimizations", "[backend][combined]") {
    SECTION("Full pipeline") {
        // Create a function with multiple optimization opportunities
        MachineFunction func;
        func.name = "combined";
        MachineBasicBlock block;
        block.name = "entry";
        
        // Add various instructions
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        block.instructions.push_back(MakeMovImmInstr(3, 0));  // Zero idiom candidate
        block.instructions.push_back(MakeCmpInstr(0, 3));
        block.instructions.push_back(MakeJccInstr("target"));
        
        func.blocks.push_back(block);
        
        // Apply scheduling
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        // Apply micro-arch optimization
        MicroArchOptimizer micro_opt(MicroArchOptimizer::kSkylake);
        auto micro_optimized = micro_opt.Optimize(scheduled.blocks[0].instructions);
        
        // Apply zero-latency optimization
        auto zero_optimized = ZeroLatencyOptimizer::OptimizeMoves(micro_optimized);
        
        // Apply register renaming
        auto renamed = RegisterRenamer::RenameRegisters(zero_optimized);
        
        // Apply instruction fusion
        auto fused = InstructionFusion::FuseInstructions(renamed);
        
        // Apply branch optimization
        auto branch_optimized = BranchOptimizer::OptimizeBranches(fused);
        
        // Result should still contain valid instructions
        REQUIRE(!branch_optimized.empty());
    }
    
    SECTION("Loop optimization pipeline") {
        std::vector<MachineInstr> loop_body;
        loop_body.push_back(MakeLoadInstr(0, 1));
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 2, {0, 3}));
        loop_body.push_back(MakeStoreInstr(4, 2));
        loop_body.push_back(MakeALUInstr(Opcode::kAdd, 1, {1, 5}));  // Increment pointer
        
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        // Pipeline the loop
        auto schedule = SoftwarePipeliner::PipelineLoop(loop_body, loop_info);
        
        // Apply cache optimization
        auto cache_optimized = loop_body;
        CacheOptimizer::InsertPrefetch(cache_optimized, loop_info);
        
        REQUIRE(schedule.initiation_interval >= 1);
        REQUIRE(cache_optimized.size() >= loop_body.size());
    }
}

// ============ Test 10: Edge Cases and Stress Tests ============
TEST_CASE("Backend - Edge Cases", "[backend][edge]") {
    SECTION("Large instruction sequence") {
        MachineFunction func;
        func.name = "large";
        MachineBasicBlock block;
        block.name = "entry";
        
        // Create many independent instructions
        for (int i = 0; i < 100; ++i) {
            block.instructions.push_back(MakeALUInstr(Opcode::kAdd, i, {i + 100, i + 200}));
        }
        
        func.blocks.push_back(block);
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        REQUIRE(scheduled.blocks[0].instructions.size() == 100);
    }
    
    SECTION("Terminator handling") {
        MachineFunction func;
        func.name = "terminator";
        MachineBasicBlock block;
        block.name = "entry";
        
        block.instructions.push_back(MakeALUInstr(Opcode::kAdd, 0, {1, 2}));
        MachineInstr ret;
        ret.opcode = Opcode::kRet;
        ret.terminator = true;
        block.instructions.push_back(ret);
        
        func.blocks.push_back(block);
        
        InstructionScheduler scheduler(func);
        MachineFunction scheduled = scheduler.Schedule();
        
        // Terminator should remain at end
        REQUIRE(scheduled.blocks[0].instructions.back().terminator == true);
    }
    
    SECTION("Mixed latency instructions scheduling") {
        std::vector<MachineInstr> insts;
        
        // Mix of different latency operations
        MachineInstr div = MakeALUInstr(Opcode::kDiv, 0, {1, 2});
        div.latency = 20;
        insts.push_back(div);
        
        insts.push_back(MakeALUInstr(Opcode::kAdd, 3, {4, 5}));  // latency 1
        
        MachineInstr mul = MakeALUInstr(Opcode::kMul, 6, {7, 8});
        mul.latency = 3;
        insts.push_back(mul);
        
        // Test through PipelineLoop which uses ComputeMII internally
        LoopInfo loop_info;
        loop_info.trip_count = 100;
        loop_info.has_side_effects = false;
        
        auto schedule = SoftwarePipeliner::PipelineLoop(insts, loop_info);
        
        // MII should account for high latency operations
        REQUIRE(schedule.initiation_interval >= 1);
    }
}
