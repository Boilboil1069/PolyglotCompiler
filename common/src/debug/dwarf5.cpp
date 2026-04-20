/**
 * @file     dwarf5.cpp
 * @brief    Shared implementation
 *
 * @ingroup  Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cstring>

#include "common/include/debug/dwarf5.h"
#include "common/include/version.h"

namespace polyglot::debug {

// ============================================================================
// LocationExpr Implementation
// ============================================================================

void LocationExpr::AddOp(Op op, int64_t operand) {
  operations_.push_back({op, operand});
}

std::vector<uint8_t> LocationExpr::Encode() const {
  std::vector<uint8_t> result;

  for (const auto &[op, operand] : operations_) {
    result.push_back(static_cast<uint8_t>(op));

    // Add operand if needed
    switch (op) {
    case Op::kFBReg:
    case Op::kConst1u:
      result.push_back(static_cast<uint8_t>(operand));
      break;
    case Op::kConst2u: {
      uint16_t val = static_cast<uint16_t>(operand);
      result.push_back(val & 0xFF);
      result.push_back((val >> 8) & 0xFF);
      break;
    }
    case Op::kConst4u: {
      uint32_t val = static_cast<uint32_t>(operand);
      for (int i = 0; i < 4; ++i) {
        result.push_back((val >> (i * 8)) & 0xFF);
      }
      break;
    }
    default:
      // No operand
      break;
    }
  }

  return result;
}

// ============================================================================
// DIE Implementation
// ============================================================================

void DIE::SetAttribute(dwarf::Attribute attr, dwarf::Form form, uint64_t value) {
  AttributeValue av;
  av.attr = attr;
  av.form = form;
  av.numeric_value = value;
  attributes_.push_back(av);
}

void DIE::SetAttribute(dwarf::Attribute attr, const std::string &value) {
  AttributeValue av;
  av.attr = attr;
  av.form = dwarf::Form::kStrp;
  av.string_value = value;
  attributes_.push_back(av);
}

void DIE::SetAttribute(dwarf::Attribute attr, const LocationExpr &expr) {
  AttributeValue av;
  av.attr = attr;
  av.form = dwarf::Form::kExprloc;
  av.expr_value = expr.Encode();
  attributes_.push_back(av);
}

void DIE::AddChild(std::unique_ptr<DIE> child) {
  children_.push_back(std::move(child));
}

std::vector<uint8_t> DIE::Encode() const {
  std::vector<uint8_t> result;

  // Encode tag (as ULEB128)
  uint16_t tag = static_cast<uint16_t>(tag_);
  result.push_back(tag & 0x7F);
  if (tag > 0x7F) {
    result.push_back((tag >> 7) & 0x7F);
  }

  // Encode attributes
  for (const auto &av : attributes_) {
    // Attribute name
    uint16_t attr = static_cast<uint16_t>(av.attr);
    result.push_back(attr & 0x7F);

    // Form
    result.push_back(static_cast<uint8_t>(av.form));

    // Value based on form
    switch (av.form) {
    case dwarf::Form::kData1:
      result.push_back(static_cast<uint8_t>(av.numeric_value));
      break;
    case dwarf::Form::kData2: {
      uint16_t val = static_cast<uint16_t>(av.numeric_value);
      result.push_back(val & 0xFF);
      result.push_back((val >> 8) & 0xFF);
      break;
    }
    case dwarf::Form::kData4:
    case dwarf::Form::kRef4:
    case dwarf::Form::kSecOffset:
    case dwarf::Form::kStrp: {
      uint32_t val = static_cast<uint32_t>(av.numeric_value);
      for (int i = 0; i < 4; ++i) {
        result.push_back((val >> (i * 8)) & 0xFF);
      }
      break;
    }
    case dwarf::Form::kData8:
    case dwarf::Form::kAddr: {
      uint64_t val = av.numeric_value;
      for (int i = 0; i < 8; ++i) {
        result.push_back((val >> (i * 8)) & 0xFF);
      }
      break;
    }
    case dwarf::Form::kExprloc:
      // ULEB128 length + data
      result.push_back(static_cast<uint8_t>(av.expr_value.size()));
      result.insert(result.end(), av.expr_value.begin(), av.expr_value.end());
      break;
    default:
      break;
    }
  }

  return result;
}

// ============================================================================
// LineNumberProgram Implementation
// ============================================================================

LineNumberProgram::LineNumberProgram() = default;

void LineNumberProgram::AddFile(const std::string &filename, const std::string &directory) {
  // Check if directory exists
  uint32_t dir_index = 0;
  for (size_t i = 0; i < directories_.size(); ++i) {
    if (directories_[i] == directory) {
      dir_index = static_cast<uint32_t>(i + 1);
      break;
    }
  }

  if (dir_index == 0) {
    directories_.push_back(directory);
    dir_index = static_cast<uint32_t>(directories_.size());
  }

  FileEntry entry;
  entry.filename = filename;
  entry.directory_index = dir_index;
  files_.push_back(entry);
}

void LineNumberProgram::AddLine(uint64_t address, const SourceLocation &loc) {
  LineEntry entry;
  entry.address = address;
  entry.file_index = LookupFileIndex(loc.file);
  entry.line = loc.line;
  entry.column = loc.column;
  entry.is_stmt = true;
  entry.end_sequence = false;
  lines_.push_back(entry);
}

uint32_t LineNumberProgram::LookupFileIndex(const std::string &filepath) const {
  // Extract filename from path for matching
  std::string filename = filepath;
  size_t pos = filepath.find_last_of("/\\");
  if (pos != std::string::npos) {
    filename = filepath.substr(pos + 1);
  }

  // Search for matching file in file table
  for (size_t i = 0; i < files_.size(); ++i) {
    if (files_[i].filename == filename || files_[i].filename == filepath) {
      return static_cast<uint32_t>(i + 1); // DWARF file indices are 1-based
    }
  }

  // If not found, return 1 (first file) as default, or 0 if no files registered
  return files_.empty() ? 0 : 1;
}

void LineNumberProgram::SetEndSequence(uint64_t address) {
  if (!lines_.empty()) {
    lines_.back().end_sequence = true;
  }
}

std::vector<uint8_t> LineNumberProgram::Encode() const {
  std::vector<uint8_t> result;

  // DWARF 5 line number program encoding

  // Encode directories (DWARF 5 format)
  EncodeULEB128(result, directories_.size());
  for (const auto &dir : directories_) {
    // Content type descriptor
    EncodeULEB128(result, 1); // DW_LNCT_path
    EncodeULEB128(result, static_cast<uint64_t>(dwarf::Form::kString));

    // Directory string
    result.insert(result.end(), dir.begin(), dir.end());
    result.push_back(0); // Null terminator
  }

  // Encode file names (DWARF 5 format)
  EncodeULEB128(result, files_.size());
  for (const auto &file : files_) {
    // Content type descriptor count
    EncodeULEB128(result, 2); // path and directory_index

    // Path
    EncodeULEB128(result, 1); // DW_LNCT_path
    EncodeULEB128(result, static_cast<uint64_t>(dwarf::Form::kString));
    result.insert(result.end(), file.filename.begin(), file.filename.end());
    result.push_back(0);

    // Directory index
    EncodeULEB128(result, 2); // DW_LNCT_directory_index
    EncodeULEB128(result, static_cast<uint64_t>(dwarf::Form::kData1));
    result.push_back(static_cast<uint8_t>(file.directory_index));
  }

  // Encode line number statements
  uint64_t prev_address = 0;
  uint32_t prev_line = 1;

  for (const auto &entry : lines_) {
    // Standard opcodes for line number program
    if (entry.address != prev_address) {
      // DW_LNS_advance_pc
      result.push_back(2); // Opcode
      EncodeULEB128(result, entry.address - prev_address);
      prev_address = entry.address;
    }

    if (entry.line != prev_line) {
      int32_t line_delta = static_cast<int32_t>(entry.line) - static_cast<int32_t>(prev_line);

      // DW_LNS_advance_line
      result.push_back(3); // Opcode
      EncodeSLEB128(result, line_delta);
      prev_line = entry.line;
    }

    // DW_LNS_copy
    result.push_back(1);

    if (entry.end_sequence) {
      // DW_LNE_end_sequence
      result.push_back(0); // Extended opcode
      result.push_back(1); // Length
      result.push_back(1); // DW_LNE_end_sequence
    }
  }

  return result;
}

// Helper functions for LEB128 encoding
void LineNumberProgram::EncodeULEB128(std::vector<uint8_t> &out, uint64_t value) const {
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value != 0) {
      byte |= 0x80; // More bytes to come
    }
    out.push_back(byte);
  } while (value != 0);
}

void LineNumberProgram::EncodeSLEB128(std::vector<uint8_t> &out, int64_t value) const {
  bool more = true;

  while (more) {
    uint8_t byte = value & 0x7F;
    value >>= 7;

    if ((value == 0 && !(byte & 0x40)) || (value == -1 && (byte & 0x40))) {
      more = false;
    } else {
      byte |= 0x80;
    }

    out.push_back(byte);
  }
}

// ============================================================================
// DwarfBuilder Implementation
// ============================================================================

DwarfBuilder::DwarfBuilder() : current_namespace_(nullptr) {
  compile_unit_ = std::make_unique<DIE>(dwarf::Tag::kCompileUnit);
}

void DwarfBuilder::SetCompileUnit(const std::string &filename, const std::string &directory,
                                  const std::string &producer, dwarf::Language language) {
  compile_unit_->SetAttribute(dwarf::Attribute::kName, filename);
  compile_unit_->SetAttribute(dwarf::Attribute::kCompDir, directory);
  compile_unit_->SetAttribute(dwarf::Attribute::kProducer, producer);
  compile_unit_->SetAttribute(dwarf::Attribute::kLanguage, dwarf::Form::kData2,
                              static_cast<uint64_t>(language));

  line_program_.AddFile(filename, directory);
}

DIE *DwarfBuilder::AddBaseType(const std::string &name, uint32_t byte_size,
                               dwarf::BaseTypeEncoding encoding) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kBaseType);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kByteSize, dwarf::Form::kData1, byte_size);
  die->SetAttribute(dwarf::Attribute::kEncoding, dwarf::Form::kData1,
                    static_cast<uint64_t>(encoding));

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddPointerType(DIE *pointee_type) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kPointerType);
  die->SetAttribute(dwarf::Attribute::kByteSize, dwarf::Form::kData1, 8);

  if (pointee_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, pointee_type->GetOffset());
  }

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddStructType(const std::string &name, uint32_t byte_size) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kStructType);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kByteSize, dwarf::Form::kData4, byte_size);

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddClassType(const std::string &name, uint32_t byte_size) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kClassType);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kByteSize, dwarf::Form::kData4, byte_size);

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddArrayType(DIE *element_type, uint32_t size) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kArrayType);
  if (element_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, element_type->GetOffset());
  }

  // Add subrange DIE for array bounds
  auto subrange = std::make_unique<DIE>(dwarf::Tag::kSubrangeType);
  subrange->SetAttribute(dwarf::Attribute::kUpperBound, dwarf::Form::kData4, size - 1);
  die->AddChild(std::move(subrange));

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddEnumType(const std::string &name, DIE *base_type) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kEnumerationType);
  die->SetAttribute(dwarf::Attribute::kName, name);

  if (base_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, base_type->GetOffset());
  }

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddTypedef(const std::string &name, DIE *base_type) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kTypedef);
  die->SetAttribute(dwarf::Attribute::kName, name);

  if (base_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, base_type->GetOffset());
  }

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddConstType(DIE *base_type) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kConstType);

  if (base_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, base_type->GetOffset());
  }

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddVolatileType(DIE *base_type) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kVolatileType);

  if (base_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, base_type->GetOffset());
  }

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddReferenceType(DIE *pointee_type, bool is_rvalue) {
  auto die = std::make_unique<DIE>(is_rvalue ? dwarf::Tag::kRvalueReferenceType
                                             : dwarf::Tag::kReferenceType);
  die->SetAttribute(dwarf::Attribute::kByteSize, dwarf::Form::kData1, 8);

  if (pointee_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, pointee_type->GetOffset());
  }

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddUnionType(const std::string &name, uint32_t byte_size) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kUnionType);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kByteSize, dwarf::Form::kData4, byte_size);

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

void DwarfBuilder::AddEnumerator(DIE *enum_type, const std::string &name, int64_t value) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kEnumerator);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kConstValue, dwarf::Form::kData8,
                    static_cast<uint64_t>(value));

  enum_type->AddChild(std::move(die));
}

DIE *DwarfBuilder::AddTemplateTypeParameter(DIE *parent, const std::string &name, DIE *type) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kTemplateTypeParameter);
  die->SetAttribute(dwarf::Attribute::kName, name);

  if (type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, type->GetOffset());
  }

  DIE *ptr = die.get();
  parent->AddChild(std::move(die));
  return ptr;
}

DIE *DwarfBuilder::AddTemplateValueParameter(DIE *parent, const std::string &name, DIE *type,
                                             const std::string &value) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kTemplateValueParameter);
  die->SetAttribute(dwarf::Attribute::kName, name);

  if (type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, type->GetOffset());
  }

  // Try to parse value as integer
  try {
    int64_t int_val = std::stoll(value);
    die->SetAttribute(dwarf::Attribute::kConstValue, dwarf::Form::kData8,
                      static_cast<uint64_t>(int_val));
  } catch (...) {
    // Store as string if not a number
    die->SetAttribute(dwarf::Attribute::kConstValue, value);
  }

  DIE *ptr = die.get();
  parent->AddChild(std::move(die));
  return ptr;
}

DIE *DwarfBuilder::AddNamespace(const std::string &name) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kNamespace);
  die->SetAttribute(dwarf::Attribute::kName, name);

  DIE *ptr = die.get();
  (current_namespace_ ? current_namespace_ : compile_unit_.get())->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

void DwarfBuilder::SetCurrentNamespace(DIE *ns) {
  current_namespace_ = ns;
}

DIE *DwarfBuilder::AddMember(DIE *struct_type, const std::string &name, DIE *type,
                             uint32_t offset) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kMember);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, type->GetOffset());
  die->SetAttribute(dwarf::Attribute::kDataMemberLocation, dwarf::Form::kData4, offset);

  DIE *ptr = die.get();
  struct_type->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

DIE *DwarfBuilder::AddSubprogram(const std::string &name, DIE *return_type, uint64_t low_pc,
                                 uint64_t high_pc, const SourceLocation &loc) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kSubprogram);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kLowPC, dwarf::Form::kAddr, low_pc);
  die->SetAttribute(dwarf::Attribute::kHighPC, dwarf::Form::kData4,
                    high_pc - low_pc); // DWARF 5 uses length

  if (return_type) {
    die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, return_type->GetOffset());
  }

  if (loc.line > 0) {
    die->SetAttribute(dwarf::Attribute::kLineNumber, dwarf::Form::kData4, loc.line);
  }

  DIE *ptr = die.get();
  compile_unit_->AddChild(std::move(die));
  all_dies_.push_back(ptr);
  return ptr;
}

void DwarfBuilder::AddParameter(DIE *subprogram, const std::string &name, DIE *type,
                                const LocationExpr &location) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kFormalParameter);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, type->GetOffset());
  die->SetAttribute(dwarf::Attribute::kLocation, location);

  subprogram->AddChild(std::move(die));
}

void DwarfBuilder::AddLocalVariable(DIE *subprogram, const std::string &name, DIE *type,
                                    const LocationExpr &location, const SourceLocation &loc) {
  auto die = std::make_unique<DIE>(dwarf::Tag::kVariable);
  die->SetAttribute(dwarf::Attribute::kName, name);
  die->SetAttribute(dwarf::Attribute::kType, dwarf::Form::kRef4, type->GetOffset());
  die->SetAttribute(dwarf::Attribute::kLocation, location);

  if (loc.line > 0) {
    die->SetAttribute(dwarf::Attribute::kLineNumber, dwarf::Form::kData4, loc.line);
  }

  subprogram->AddChild(std::move(die));
}

void DwarfBuilder::AddLineEntry(uint64_t address, const SourceLocation &loc) {
  line_program_.AddLine(address, loc);
}

std::unordered_map<std::string, std::vector<uint8_t>> DwarfBuilder::GenerateSections() {
  std::unordered_map<std::string, std::vector<uint8_t>> sections;

  sections[dwarf::kDebugInfo] = EncodeDebugInfo();
  sections[dwarf::kDebugAbbrev] = EncodeDebugAbbrev();
  sections[dwarf::kDebugStr] = EncodeDebugStr();
  sections[dwarf::kDebugLine] = EncodeDebugLine();

  return sections;
}

uint32_t DwarfBuilder::AddString(const std::string &str) {
  auto it = string_table_.find(str);
  if (it != string_table_.end()) {
    return it->second;
  }

  uint32_t offset = static_cast<uint32_t>(string_data_.size());
  string_table_[str] = offset;

  string_data_.insert(string_data_.end(), str.begin(), str.end());
  string_data_.push_back(0); // Null terminator

  return offset;
}

std::vector<uint8_t> DwarfBuilder::EncodeDebugInfo() {
  std::vector<uint8_t> result;

  // Compilation unit header (DWARF 5)
  // Unit length (32-bit, will be filled later)
  size_t length_pos = result.size();
  for (int i = 0; i < 4; ++i)
    result.push_back(0);

  // Version (5 for DWARF 5)
  result.push_back(5 & 0xFF);
  result.push_back((5 >> 8) & 0xFF);

  // Unit type (DW_UT_compile = 0x01)
  result.push_back(0x01);

  // Address size (8 bytes for 64-bit)
  result.push_back(8);

  // Debug abbrev offset (0 for first CU)
  for (int i = 0; i < 4; ++i)
    result.push_back(0);

  // Encode compilation unit DIE and children
  auto cu_data = compile_unit_->Encode();
  result.insert(result.end(), cu_data.begin(), cu_data.end());

  // Fill in length (excluding length field itself)
  uint32_t length = static_cast<uint32_t>(result.size() - 4);
  result[length_pos] = length & 0xFF;
  result[length_pos + 1] = (length >> 8) & 0xFF;
  result[length_pos + 2] = (length >> 16) & 0xFF;
  result[length_pos + 3] = (length >> 24) & 0xFF;

  return result;
}

std::vector<uint8_t> DwarfBuilder::EncodeDebugAbbrev() {
  std::vector<uint8_t> result;

  // Abbreviation table for common DIE types
  // Entry 1: Compilation unit
  result.push_back(1); // Abbrev code
  result.push_back(static_cast<uint8_t>(dwarf::Tag::kCompileUnit));
  result.push_back(1); // Has children

  // Attributes
  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kName));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kStrp));
  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kLanguage));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kData2));
  result.push_back(0);
  result.push_back(0); // End of attributes

  // Entry 2: Subprogram
  result.push_back(2);
  result.push_back(static_cast<uint8_t>(dwarf::Tag::kSubprogram));
  result.push_back(1); // Has children

  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kName));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kStrp));
  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kLowPC));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kAddr));
  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kHighPC));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kData4));
  result.push_back(0);
  result.push_back(0);

  // Entry 3: Base type
  result.push_back(3);
  result.push_back(static_cast<uint8_t>(dwarf::Tag::kBaseType));
  result.push_back(0); // No children

  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kName));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kStrp));
  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kByteSize));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kData1));
  result.push_back(static_cast<uint8_t>(dwarf::Attribute::kEncoding));
  result.push_back(static_cast<uint8_t>(dwarf::Form::kData1));
  result.push_back(0);
  result.push_back(0);

  // End of abbreviation table
  result.push_back(0);

  return result;
}

std::vector<uint8_t> DwarfBuilder::EncodeDebugStr() {
  return string_data_;
}

std::vector<uint8_t> DwarfBuilder::EncodeDebugLine() {
  return line_program_.Encode();
}

// ============================================================================
// DebugInfoGenerator Implementation
// ============================================================================

DebugInfoGenerator::DebugInfoGenerator() :
    language_(dwarf::Language::kCpp20), producer_(POLYGLOT_VERSION_BANNER),
    current_function_(nullptr) {}

void DebugInfoGenerator::RegisterType(const std::string &name, uint32_t size,
                                      dwarf::BaseTypeEncoding encoding) {
  DIE *type = builder_.AddBaseType(name, size, encoding);
  type_map_[name] = type;
}

void DebugInfoGenerator::BeginFunction(const std::string &name, const std::string &return_type,
                                       uint64_t start_address, const SourceLocation &loc) {
  DIE *ret_type = nullptr;
  auto it = type_map_.find(return_type);
  if (it != type_map_.end()) {
    ret_type = it->second;
  }

  current_function_ = builder_.AddSubprogram(name, ret_type, start_address, start_address, loc);
}

void DebugInfoGenerator::AddFunctionParameter(const std::string &name, const std::string &type,
                                              int frame_offset) {
  if (!current_function_)
    return;

  DIE *param_type = nullptr;
  auto it = type_map_.find(type);
  if (it != type_map_.end()) {
    param_type = it->second;
  }

  LocationExpr loc;
  loc.AddOp(LocationExpr::Op::kFBReg, frame_offset);

  builder_.AddParameter(current_function_, name, param_type, loc);
}

void DebugInfoGenerator::AddLocalVariable(const std::string &name, const std::string &type,
                                          int frame_offset, const SourceLocation &loc) {
  if (!current_function_)
    return;

  DIE *var_type = nullptr;
  auto it = type_map_.find(type);
  if (it != type_map_.end()) {
    var_type = it->second;
  }

  LocationExpr location;
  location.AddOp(LocationExpr::Op::kFBReg, frame_offset);

  builder_.AddLocalVariable(current_function_, name, var_type, location, loc);
}

void DebugInfoGenerator::EndFunction(uint64_t end_address) {
  current_function_ = nullptr;
}

void DebugInfoGenerator::AddSourceLine(uint64_t address, const SourceLocation &loc) {
  builder_.AddLineEntry(address, loc);
}

std::unordered_map<std::string, std::vector<uint8_t>> DebugInfoGenerator::Generate() {
  builder_.SetCompileUnit("main.cpp", comp_dir_, producer_, language_);

  // Register common types
  RegisterType("int", 4, dwarf::BaseTypeEncoding::kSigned);
  RegisterType("long", 8, dwarf::BaseTypeEncoding::kSigned);
  RegisterType("float", 4, dwarf::BaseTypeEncoding::kFloat);
  RegisterType("double", 8, dwarf::BaseTypeEncoding::kFloat);
  RegisterType("bool", 1, dwarf::BaseTypeEncoding::kBoolean);
  RegisterType("char", 1, dwarf::BaseTypeEncoding::kSignedChar);

  return builder_.GenerateSections();
}

} // namespace polyglot::debug
