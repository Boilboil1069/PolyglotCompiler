# Common ABI Layer

The common ABI layer (`polyglot::backends::common::abi`) provides a single,
template-based implementation of the calling-convention machinery and
relocation taxonomy that is shared by every native backend (currently
x86_64 and arm64). It replaces the two previously trivial header stubs
`backends/common/include/abi.h` and `backends/common/include/relocation.h`,
which are now retained as forwarding shims so that legacy includes keep
compiling.

## 1. Module layout

```
backends/common/include/
    abi.h                       (forwarding shim → abi/abi.h)
    relocation.h                (forwarding shim → abi/relocation.h)
    abi/
        abi.h                   (umbrella header)
        calling_convention.h    (CallingConvention<Traits>, StackFrame<Traits>,
                                 ComputeStackFrame template)
        relocation.h            (RelocationKind, RelocationEntry,
                                 AbiDescriptor, mappers, ToString/Parse)
backends/common/src/
    abi.cpp                     (ELF / Mach-O code-point mappers,
                                 ToString / ParseRelocationKind)
```

## 2. CallingConvention&lt;Traits&gt;

`CallingConvention` is a thin façade over the static register tables that
each `TargetTraits` exposes. To wire a new target into the common layer,
its `Traits` must publish:

```cpp
struct MyTargetTraits {
    // ... existing MachineIR contract (Register, RegisterClass, ...) ...

    inline static const std::vector<Register> kIntegerArgRegs   = { /* ABI order */ };
    inline static const std::vector<Register> kFloatArgRegs     = { /* ABI order */ };
    inline static const std::vector<Register> kCalleeSavedRegs  = { /* ABI order */ };
    inline static const std::vector<Register> kVolatileRegs     = { /* caller-saved */ };

    static constexpr int kStackAlignment = 16;   // bytes
    static constexpr int kPointerSize    =  8;
    static constexpr int kRedZoneSize    =  0;   // SysV x86_64 = 128
};
```

Eight read-only accessors expose those tables and constants:
`IntegerArgRegs`, `FloatArgRegs`, `CalleeSavedRegs`, `VolatileRegs`,
`StackAlignment`, `PointerSize`, `RedZoneSize`, and the convenience
`AvailableRegisters()` which concatenates volatile + callee-saved sets in
allocator-preferred order.

## 3. StackFrame&lt;Traits&gt; and ComputeStackFrame

```cpp
template <typename Traits>
struct StackFrame {
    int                                    total_size{0};
    int                                    spill_area_size{0};
    int                                    local_area_size{0};
    int                                    arg_area_size{0};
    std::vector<typename Traits::Register> saved_regs;
};
```

`ComputeStackFrame(cc, fn, alloc, call_opcode)` materialises the layout
in a target-independent way:

1. **Spill area**: `alloc.stack_slots * cc.PointerSize()` bytes.
2. **Callee-saved set**: walks `cc.CalleeSavedRegs()` in ABI order and
   keeps each register that `alloc` actually uses, preserving stable
   prologue/epilogue order.
3. **Outgoing argument area**: scans every `MachineInstr` in `fn` whose
   opcode equals `call_opcode`; if any call has more operands than
   `cc.IntegerArgRegs().size()`, allocates room for the overflow.
4. **Total size**: rounds the sum (spill + locals + args + saved-reg
   slots) up to `cc.StackAlignment()`.

The algorithm is byte-for-byte equivalent to the legacy x86 frame
computation that lived in `backends/x86_64/src/calling_convention.cpp`,
so existing `polyc` output is unchanged.

## 4. RelocationKind and per-format mappers

`RelocationKind` is an opaque, format-neutral enumeration with 14 values
covering the union of x86_64 and AArch64 needs (absolute, PC-relative,
GOT/PLT, ADRP/ADD page+offset, B/Bcond branches, MOVZ/MOVK groups). The
canonical encoding for each object-file format is supplied by four pure
functions:

| Mapper | Negative result | Notes |
|--------|----------------|-------|
| `MapToElfX86_64`   | `kUnsupportedRelocation` for AArch64-only kinds | psABI x86_64 §4.4 |
| `MapToElfAArch64`  | `kUnsupportedRelocation` for x86_64-only kinds  | ELF for the Arm 64-bit Architecture |
| `MapToMachOX86_64` | `kUnsupportedRelocation` for AArch64-only kinds | `<mach-o/x86_64/reloc.h>` |
| `MapToMachOArm64`  | `kUnsupportedRelocation` for x86_64-only kinds  | `<mach-o/arm64/reloc.h>` |

`kUnsupportedRelocation == 0xFFFFFFFFu` is a single sentinel that
backends route through their diagnostic pipeline whenever a chosen
combination is illegal. `ToString(RelocationKind)` and
`ParseRelocationKind(const std::string&, RelocationKind*)` provide
stable text serialisation for tooling and tests.

`AbiDescriptor` is a value-type fingerprint (`name`, `pointer_size`,
`stack_alignment`, `red_zone_size`) used by the linker and by
diagnostics that need to compare two ABIs without cracking open the
templated `CallingConvention`.

## 5. Forwarding shims for legacy includes

Two pre-existing headers are preserved per project policy:

- `backends/common/include/abi.h` now contains
  `using ABI = polyglot::backends::common::abi::AbiDescriptor;` plus the
  matching include.
- `backends/common/include/relocation.h` exposes
  `using RelocType = polyglot::backends::common::abi::RelocationKind;`.

Notably, neither shim aliases the type name `Relocation`. The
`polyglot::backends::Relocation` defined in `object_file.h` (used by
`tools/polyc/src/compilation_pipeline.cpp`) keeps its meaning unchanged.

## 6. AbiContract and verifier rules

`MachineIRVerifier` accepts an optional `AbiContract<Traits, OpcodeT>`
that encodes the target's call opcode, its operand-arity ceiling, and
the volatile register set:

```cpp
template <typename Traits, typename OpcodeT>
struct AbiContract {
    OpcodeT                                       call_opcode;
    std::size_t                                   max_call_operands;
    std::vector<typename Traits::Register>        volatile_regs;
};
```

When `Verify(fn, &contract)` is invoked, two ABI rules run in addition
to the three structural rules:

| Diagnostic code | Trigger |
|-----------------|---------|
| `kAbiCallArityExceeded` | A `call_opcode` instruction whose operand count exceeds `max_call_operands`. |
| `kAbiVolatileRegLeak`   | A read of a `volatile_regs` register in the instruction immediately following a call, with no intervening definition. |

The single-argument overload `Verify(fn)` continues to behave exactly
like before (delegates with `nullptr`), so existing callers are unaffected.

## 7. Per-target adoption

Both `backends/x86_64/src/calling_convention.cpp` and
`backends/arm64/src/calling_convention.cpp` now instantiate
`abi::CallingConvention<…TargetTraits>` and call `abi::ComputeStackFrame`,
keeping only the genuinely target-specific bits — prologue / epilogue
emission, call-setup sequencing, and the `RenderOperand` helper. The
SysV-x86_64 and AAPCS64 register tables live as `Traits` constants, so
adding a new ABI variant (e.g. Windows x64, ILP32 AArch64) is purely a
matter of supplying a new `Traits` type.

## 8. Troubleshooting

- **Unresolved `RelocationKind` in linker output** — almost always a
  mapper returning `kUnsupportedRelocation`. Cross-check the kind with
  the target's psABI table; if the kind is genuinely unsupported for
  that format, the caller should lower it to a supported sequence
  before reaching the mapper.
- **Stack-frame size differs from previous releases for the same input**
  — verify that `kPointerSize` and `kStackAlignment` match the legacy
  values, and that `kCalleeSavedRegs` is listed in the same ABI order
  as before. The algorithm is order-stable.
- **Verifier reports `kAbiVolatileRegLeak` after lowering** — insert
  explicit clobbers / reloads at the call site, or extend the live
  range of the relevant virtual register so the post-call read sees a
  defined value.
