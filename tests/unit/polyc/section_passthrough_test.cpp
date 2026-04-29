/**
 * @file     section_passthrough_test.cpp
 * @brief    Validate per-section pass-through, stable ordering, and offset
 *           shifting inside the polyc packaging stage.
 *
 * @ingroup  Tests / Unit / polyc
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "tools/polyc/include/compilation_pipeline.h"
#include "frontends/common/include/diagnostics.h"

using polyglot::compilation::BackendOutput;
using polyglot::compilation::CompilationContext;
using polyglot::compilation::CompiledObject;
using polyglot::compilation::CreatePackagingStage;
using polyglot::frontends::Diagnostics;
using polyglot::linker::Relocation;
using polyglot::linker::Symbol;
using polyglot::linker::SymbolBinding;
using polyglot::linker::SymbolType;

namespace {

#pragma pack(push, 1)
struct PobjFileHeaderV {
    char magic[4];
    std::uint16_t version;
    std::uint16_t section_count;
    std::uint16_t symbol_count;
    std::uint16_t reloc_count;
    std::uint64_t strtab_offset;
};
struct PobjSectionRecordV {
    std::uint32_t name_offset;
    std::uint32_t flags;
    std::uint64_t offset;
    std::uint64_t size;
};
struct PobjSymbolRecordV {
    std::uint32_t name_offset;
    std::uint32_t section_index;
    std::uint64_t value;
    std::uint64_t size;
    std::uint8_t binding;
    std::uint8_t reserved[3];
};
struct PobjRelocRecordV {
    std::uint32_t section_index;
    std::uint64_t offset;
    std::uint32_t type;
    std::uint32_t symbol_index;
    std::int64_t addend;
};
#pragma pack(pop)

struct DecodedSection {
    std::string name;
    std::uint64_t size{0};
    std::vector<std::uint8_t> data;
};

struct DecodedPobj {
    std::vector<DecodedSection> sections;
    std::vector<PobjRelocRecordV> relocs;
    bool valid_magic{false};
};

DecodedPobj DecodePobj(const std::vector<std::uint8_t> &buf) {
    DecodedPobj out;
    if (buf.size() < sizeof(PobjFileHeaderV)) return out;
    PobjFileHeaderV hdr{};
    std::memcpy(&hdr, buf.data(), sizeof(hdr));
    if (std::string(hdr.magic, 4) != "POBJ") return out;
    out.valid_magic = true;

    const std::size_t sec_table_off = sizeof(PobjFileHeaderV);
    const std::size_t sym_table_off =
        sec_table_off + hdr.section_count * sizeof(PobjSectionRecordV);
    const std::size_t reloc_table_off =
        sym_table_off + hdr.symbol_count * sizeof(PobjSymbolRecordV);
    const char *strtab = reinterpret_cast<const char *>(buf.data() + hdr.strtab_offset);

    for (std::uint16_t i = 0; i < hdr.section_count; ++i) {
        PobjSectionRecordV rec{};
        std::memcpy(&rec, buf.data() + sec_table_off + i * sizeof(PobjSectionRecordV),
                    sizeof(rec));
        DecodedSection ds;
        ds.name = std::string(strtab + rec.name_offset);
        ds.size = rec.size;
        if (rec.size > 0 && rec.offset + rec.size <= buf.size()) {
            ds.data.assign(buf.data() + rec.offset, buf.data() + rec.offset + rec.size);
        }
        out.sections.push_back(std::move(ds));
    }
    for (std::uint16_t i = 0; i < hdr.reloc_count; ++i) {
        PobjRelocRecordV rec{};
        std::memcpy(&rec, buf.data() + reloc_table_off + i * sizeof(PobjRelocRecordV),
                    sizeof(rec));
        out.relocs.push_back(rec);
    }
    return out;
}

CompiledObject MakeTextObject(std::vector<std::uint8_t> bytes,
                              const std::string &reloc_symbol = "",
                              std::uint64_t reloc_at = 0) {
    CompiledObject obj;
    obj.name = ".text";
    obj.code = std::move(bytes);
    if (!reloc_symbol.empty()) {
        Relocation r;
        r.section = ".text";
        r.offset = reloc_at;
        r.type = 1;
        r.symbol = reloc_symbol;
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        obj.relocations.push_back(r);
    }
    return obj;
}

CompiledObject MakeRdataObject(std::vector<std::uint8_t> bytes,
                               const std::string &symbol_name) {
    CompiledObject obj;
    obj.name = ".rdata";
    obj.data = std::move(bytes);
    Symbol s;
    s.name = symbol_name;
    s.section = ".rdata";
    s.offset = 0;
    s.size = obj.data.size();
    s.binding = SymbolBinding::kGlobal;
    s.type = SymbolType::kObject;
    s.is_defined = true;
    obj.symbols.push_back(std::move(s));
    return obj;
}

CompilationContext::Config BareConfig() {
    CompilationContext::Config cfg;
    cfg.target_arch = "x86_64";
    cfg.object_format = "pobj";
    cfg.output_file = "";
    cfg.mode = "compile";
    return cfg;
}

}  // namespace

TEST_CASE("PE-7-D packaging passes every backend section through to the POBJ image",
          "[polyc][packaging][pe7]") {
    BackendOutput backend_out;
    backend_out.success = true;
    backend_out.target_arch = "x86_64";
    backend_out.objects.push_back(MakeTextObject({0x90, 0x90, 0x90, 0x90}));
    backend_out.objects.push_back(MakeRdataObject({'h', 'i', '\n', 0}, "str0"));

    auto stage = CreatePackagingStage();
    Diagnostics diags;
    auto cfg = BareConfig();
    auto pkg = stage->Run(backend_out, cfg, diags);
    REQUIRE_FALSE(diags.HasErrors());
    REQUIRE_FALSE(pkg.binary_data.empty());

    auto decoded = DecodePobj(pkg.binary_data);
    REQUIRE(decoded.valid_magic);
    bool saw_text = false;
    bool saw_rdata = false;
    for (const auto &s : decoded.sections) {
        if (s.name == ".text") {
            saw_text = true;
            CHECK(s.size == 4u);
        } else if (s.name == ".rdata") {
            saw_rdata = true;
            CHECK(s.size == 4u);
            REQUIRE(s.data.size() == 4u);
            CHECK(s.data[0] == static_cast<std::uint8_t>('h'));
            CHECK(s.data[1] == static_cast<std::uint8_t>('i'));
            CHECK(s.data[2] == static_cast<std::uint8_t>('\n'));
            CHECK(s.data[3] == 0u);
        }
    }
    CHECK(saw_text);
    CHECK(saw_rdata);
}

TEST_CASE("PE-7-D packaging stable-sorts sections into .text -> .rdata -> .data order",
          "[polyc][packaging][pe7]") {
    BackendOutput backend_out;
    backend_out.success = true;
    backend_out.target_arch = "x86_64";
    backend_out.objects.push_back(MakeRdataObject({1, 2, 3, 4}, "ro"));
    {
        CompiledObject data_obj;
        data_obj.name = ".data";
        data_obj.data = {0x10, 0x20};
        backend_out.objects.push_back(std::move(data_obj));
    }
    backend_out.objects.push_back(MakeTextObject({0x55, 0x48, 0x89, 0xE5, 0xC3}));

    auto stage = CreatePackagingStage();
    Diagnostics diags;
    auto cfg = BareConfig();
    auto pkg = stage->Run(backend_out, cfg, diags);
    REQUIRE_FALSE(diags.HasErrors());

    auto decoded = DecodePobj(pkg.binary_data);
    REQUIRE(decoded.valid_magic);
    REQUIRE(decoded.sections.size() == 3);
    CHECK(decoded.sections[0].name == ".text");
    CHECK(decoded.sections[1].name == ".rdata");
    CHECK(decoded.sections[2].name == ".data");
}

TEST_CASE("PE-7-D packaging shifts relocation offsets when two .text objects merge",
          "[polyc][packaging][pe7]") {
    BackendOutput backend_out;
    backend_out.success = true;
    backend_out.target_arch = "x86_64";
    backend_out.objects.push_back(MakeTextObject({0x90, 0x90, 0x90, 0x90,
                                                  0x90, 0x90, 0x90, 0x90}));
    backend_out.objects.push_back(MakeTextObject({0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3},
                                                 "polyrt_println", 1));

    auto stage = CreatePackagingStage();
    Diagnostics diags;
    auto cfg = BareConfig();
    auto pkg = stage->Run(backend_out, cfg, diags);
    REQUIRE_FALSE(diags.HasErrors());

    auto decoded = DecodePobj(pkg.binary_data);
    REQUIRE(decoded.valid_magic);
    REQUIRE(decoded.sections.size() == 1);
    CHECK(decoded.sections[0].name == ".text");
    CHECK(decoded.sections[0].size == 14u);

    REQUIRE(decoded.relocs.size() == 1);
    CHECK(decoded.relocs[0].offset == 9u);
    CHECK(decoded.relocs[0].section_index == 0u);
    CHECK(decoded.relocs[0].addend == -4);
}
