/**
 * @file     viewers_test.cpp
 * @brief    Unit tests for image / hex / binary viewers and the
 *           SQL console.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/dbclient/sql_console.h"
#include "tools/ui/common/viewer/binary_inspector.h"
#include "tools/ui/common/viewer/hex_viewer.h"
#include "tools/ui/common/viewer/image_viewer.h"

using namespace polyglot::tools::ui;

TEST_CASE("Image format detection by magic + extension fallback",
          "[polyui][viewer][image]") {
  CHECK(viewer::DetectImageFormat(
            "a.png",
            {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}) ==
        viewer::ImageFormat::kPng);
  CHECK(viewer::DetectImageFormat("a.jpg", {0xFF, 0xD8, 0xFF, 0xE0}) ==
        viewer::ImageFormat::kJpeg);
  CHECK(viewer::DetectImageFormat(
            "a.webp",
            {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'}) ==
        viewer::ImageFormat::kWebp);
  CHECK(viewer::DetectImageFormat("a.gif", {'G', 'I', 'F', '8', '9'}) ==
        viewer::ImageFormat::kGif);
  CHECK(viewer::DetectImageFormat("a.bmp", {'B', 'M', 0, 0}) ==
        viewer::ImageFormat::kBmp);
  CHECK(viewer::DetectImageFormat("a.svg", {'<', '?', 'x'}) ==
        viewer::ImageFormat::kSvg);
  CHECK(viewer::DetectImageFormat("noext", {0, 0}) ==
        viewer::ImageFormat::kUnknown);
}

TEST_CASE("ImageViewer zoom clamp / pan / pick / channel split",
          "[polyui][viewer][image]") {
  viewer::ImageViewer iv(2, 2);
  iv.SetZoom(0.0);
  CHECK(iv.view().zoom > 0.0);                       // clamped to 0.05
  iv.SetZoom(1000.0);
  CHECK(iv.view().zoom == 64.0);                     // clamped to max
  iv.ResetView();
  iv.ZoomBy(2.0);
  CHECK(iv.view().zoom == 2.0);
  iv.Pan(5.0, -3.0);
  CHECK(iv.view().pan_x == 5.0);
  CHECK(iv.view().pan_y == -3.0);

  std::vector<uint8_t> px = {
      255,   0,   0, 255,    0, 255,   0, 255,
        0,   0, 255, 255,  128, 128, 128, 200,
  };
  auto p = iv.Pick(px, 1, 1);
  REQUIRE(p);
  CHECK(p->r == 128);
  CHECK(p->a == 200);
  CHECK_FALSE(iv.Pick(px, 5, 5));

  iv.SetChannel(viewer::ChannelMask::kRed);
  auto split = iv.ApplyChannelSplit(px);
  CHECK(split[0] == 255);
  CHECK(split[1] == 0);
  CHECK(split[3] == 255);
  CHECK(split[5] == 0);                              // green pixel zeroed
}

TEST_CASE("HexViewer chunked find + jump + highlights",
          "[polyui][viewer][hex]") {
  std::vector<uint8_t> blob;
  blob.reserve(50000);
  for (int i = 0; i < 50000; ++i) blob.push_back(static_cast<uint8_t>(i & 0xFF));
  // Hide the needle straddling a chunk boundary at offset 8190.
  std::vector<uint8_t> needle = {0xDE, 0xAD, 0xBE, 0xEF};
  for (size_t i = 0; i < needle.size(); ++i)
    blob[8190 + i] = needle[i];

  viewer::HexReader reader = [&](uint64_t off, uint64_t len,
                                 std::vector<uint8_t> &out) {
    if (off >= blob.size()) return size_t{0};
    size_t n = static_cast<size_t>(std::min<uint64_t>(len,
                                          blob.size() - off));
    out.assign(blob.begin() + off, blob.begin() + off + n);
    return n;
  };
  viewer::HexViewer hv(blob.size(), 4096, reader);

  auto found = hv.Find(needle);
  REQUIRE(found);
  CHECK(*found == 8190);

  CHECK(hv.JumpTo(33, 16) == 32);
  CHECK(hv.JumpTo(blob.size() + 100, 16) ==
        (static_cast<uint64_t>(blob.size()) / 16) * 16);

  auto chunk = hv.Read(8188, 8);
  REQUIRE(chunk.size() == 8);
  CHECK(chunk[2] == 0xDE);

  hv.AddHighlight({8190, 4, "deadbeef"});
  CHECK(hv.HighlightsCovering(8000, 500).size() == 1);
  CHECK(hv.HighlightsCovering(0, 100).empty());

  CHECK_FALSE(hv.Find({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST_CASE("Binary identification: ELF / PE / Mach-O / WASM",
          "[polyui][viewer][binary]") {
  std::vector<uint8_t> elf(64, 0);
  elf[0]=0x7F; elf[1]='E'; elf[2]='L'; elf[3]='F';
  elf[4]=2;                                          // 64-bit
  elf[5]=1;                                          // little endian
  elf[16]=2; elf[17]=0;                              // ET_EXEC
  elf[18]=0x3E; elf[19]=0x00;                        // EM_X86_64
  auto a = viewer::IdentifyBinary(elf);
  CHECK(a.kind == viewer::BinaryKind::kElf);
  CHECK(a.bits == 64);
  CHECK(a.arch == "x86_64");
  CHECK(a.subsystem == "executable");

  std::vector<uint8_t> pe(0x100, 0);
  pe[0]='M'; pe[1]='Z';
  pe[0x3C]=0x80; pe[0x3D]=0; pe[0x3E]=0; pe[0x3F]=0;
  pe.resize(0x90);
  pe[0x80]='P'; pe[0x81]='E'; pe[0x82]=0; pe[0x83]=0;
  pe[0x84]=0x64; pe[0x85]=0x86;                      // x86_64
  auto b = viewer::IdentifyBinary(pe);
  CHECK(b.kind == viewer::BinaryKind::kPe);
  CHECK(b.bits == 64);
  CHECK(b.arch == "x86_64");

  std::vector<uint8_t> mach = {0xCF, 0xFA, 0xED, 0xFE,
                               0x07, 0x00, 0x00, 0x01};
  auto c = viewer::IdentifyBinary(mach);
  CHECK(c.kind == viewer::BinaryKind::kMachO);
  CHECK(c.bits == 64);
  CHECK(c.arch == "x86_64");

  std::vector<uint8_t> wasm = {0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00};
  auto d = viewer::IdentifyBinary(wasm);
  CHECK(d.kind == viewer::BinaryKind::kWasm);
  CHECK(d.arch == "wasm32");

  CHECK(viewer::IdentifyBinary({1, 2, 3, 4}).kind ==
        viewer::BinaryKind::kUnknown);
}

TEST_CASE("Disassembler facade emits one row per byte",
          "[polyui][viewer][disasm]") {
  viewer::DisassemblerFacade d;
  auto rows = d.Disassemble({0x90, 0xC3}, 0x1000);
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].address == 0x1000);
  CHECK(rows[0].mnemonic == ".byte");
  CHECK(rows[1].address == 0x1001);
  CHECK(rows[1].operands.find("0xc3") != std::string::npos);
}

namespace {

class FakeDriver : public dbclient::SqlDriver {
 public:
  dbclient::ResultSet Execute(const std::string &sql) override {
    last_sql = sql;
    if (sql.find("ERR") != std::string::npos) {
      dbclient::ResultSet r;
      r.error = "syntax error";
      return r;
    }
    dbclient::ResultSet r;
    r.columns = {{"id", "INTEGER"}, {"name", "TEXT"}};
    for (int i = 0; i < 5; ++i) {
      r.rows.push_back({{std::to_string(i),
                         "row,with\"quote\n" + std::to_string(i)}});
    }
    r.affected_rows = 5;
    return r;
  }
  std::vector<std::string> Tables() override { return {"users", "logs"}; }
  std::vector<dbclient::Column> ColumnsOf(const std::string &t) override {
    if (t == "users") return {{"id", "INTEGER"}, {"name", "TEXT"}};
    return {};
  }
  std::string last_sql;
};

}  // namespace

TEST_CASE("SQL console executes / errors / paginates / exports CSV",
          "[polyui][dbclient][sql]") {
  auto drv = std::make_shared<FakeDriver>();
  dbclient::SqlConsole console(drv, /*history_capacity=*/2);
  auto rs = console.Execute("SELECT * FROM users");
  CHECK(rs.error.empty());
  CHECK(rs.rows.size() == 5);
  CHECK(rs.affected_rows == 5);

  auto err = console.Execute("ERR");
  CHECK_FALSE(err.error.empty());
  console.Execute("SELECT 1");
  CHECK(console.history().size() == 2);              // capacity-trimmed

  CHECK(console.Tables() == std::vector<std::string>{"users", "logs"});
  CHECK(console.ColumnsOf("users").size() == 2);

  dbclient::ResultPager pager(rs, 2);
  CHECK(pager.page_count() == 3);
  CHECK(pager.Page(0).size() == 2);
  CHECK(pager.Page(2).size() == 1);
  CHECK(pager.Page(99).empty());

  auto csv = dbclient::ExportCsv(rs);
  CHECK(csv.find("id,name") == 0);
  // Quotes / commas / newlines in values must be escaped.
  CHECK(csv.find("\"row,with\"\"quote\n0\"") != std::string::npos);
}
