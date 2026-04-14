/**
 * @file     topology_codegen.cpp
 * @brief    Generates .ploy source code from a TopologyGraph
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/polytopo/include/topology_codegen.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_set>

namespace polyglot::tools::topo {

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

// Return a human-readable type name for a Port, defaulting to "Any".
std::string PortTypeName(const Port &port) {
    return port.type.name.empty() ? "Any" : port.type.name;
}

// Uppercase the first letter of a string for .ploy type names.
std::string UpperFirst(const std::string &s) {
    if (s.empty()) return s;
    std::string out = s;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// Convert a TopologyEdge::Status to a .ploy-friendly status comment.
std::string StatusComment(TopologyEdge::Status status) {
    switch (status) {
    case TopologyEdge::Status::kValid:           return "valid";
    case TopologyEdge::Status::kImplicitConvert: return "implicit_convert";
    case TopologyEdge::Status::kExplicitConvert: return "explicit_convert";
    case TopologyEdge::Status::kIncompatible:    return "incompatible";
    case TopologyEdge::Status::kUnknown:         return "unknown";
    }
    return "unknown";
}

// Convert TopologyNode::Kind to a short string for comments.
std::string KindString(TopologyNode::Kind kind) {
    switch (kind) {
    case TopologyNode::Kind::kFunction:     return "function";
    case TopologyNode::Kind::kConstructor:  return "constructor";
    case TopologyNode::Kind::kMethod:       return "method";
    case TopologyNode::Kind::kPipeline:     return "pipeline";
    case TopologyNode::Kind::kMapFunc:      return "map_func";
    case TopologyNode::Kind::kExternalCall: return "external_call";
    }
    return "unknown";
}

// Map a generic type name to a ploy-compatible type keyword.
// Ploy uses uppercase type names: INT, FLOAT, STRING, BOOL, VOID, ARRAY[T].
std::string ToPloySrcType(const std::string &type_name) {
    if (type_name.empty() || type_name == "Any") return "INT";
    // Already uppercase?
    if (!type_name.empty() && std::isupper(static_cast<unsigned char>(type_name[0]))) {
        return type_name;
    }
    // Lowercase common types
    if (type_name == "int" || type_name == "i32" || type_name == "i64" ||
        type_name == "I64") {
        return "INT";
    }
    if (type_name == "float" || type_name == "f32" || type_name == "f64" ||
        type_name == "double") {
        return "FLOAT";
    }
    if (type_name == "str" || type_name == "string" || type_name == "String") {
        return "STRING";
    }
    if (type_name == "bool" || type_name == "Bool") return "BOOL";
    if (type_name == "void" || type_name == "None") return "VOID";
    // Pass through unknown types capitalised
    return UpperFirst(type_name);
}

// Strip the leading "language::" prefix from a node name.
// "cpp::math_ops::add" → "math_ops::add"
// "python::string_utils::concat" → "string_utils::concat"
std::string StripLangPrefix(const std::string &name, const std::string &language) {
    std::string prefix = language + "::";
    if (name.size() > prefix.size() &&
        name.substr(0, prefix.size()) == prefix) {
        return name.substr(prefix.size());
    }
    return name;
}

// Emit a source location comment for diff/write-back.
// Format: "// @source <file>:<line>"
std::string SourceLocComment(const core::SourceLoc &loc) {
    if (loc.file.empty()) return {};
    return "// @source " + loc.file + ":" + std::to_string(loc.line);
}

} // anonymous namespace

// ============================================================================
// GeneratePloySrc
// ============================================================================

std::string GeneratePloySrc(const TopologyGraph &graph) {
    std::ostringstream out;

    /** @name Header comment */
    /** @{ */
    out << "// ============================================================================\n";
    out << "// Auto-generated .ploy source from topology graph\n";
    if (!graph.module_name.empty()) {
        out << "// Module: " << graph.module_name << "\n";
    }
    if (!graph.source_file.empty()) {
        out << "// Source: " << graph.source_file << "\n";
    }
    out << "// Nodes: " << graph.NodeCount()
        << "  Edges: " << graph.EdgeCount() << "\n";
    out << "// ============================================================================\n\n";

    /** @} */

    /** @name Collect distinct languages that appear in nodes */
    /** @{ */
    std::set<std::string> languages;
    for (const auto &node : graph.Nodes()) {
        if (!node.language.empty() && node.language != "ploy") {
            languages.insert(node.language);
        }
    }

    /** @} */

    /** @name IMPORT directives */
    /** @{ */
    // Group by language.  For each language, collect unique module prefixes
    // from node names (e.g. "cpp::math_ops::add" -> module "math_ops").
    std::set<std::string> emitted_imports;
    for (const auto &node : graph.Nodes()) {
        if (node.language.empty() || node.language == "ploy") continue;
        // Extract module from qualified name: "math_ops::add" -> "math_ops"
        std::string module;
        auto sep = node.name.find("::");
        if (sep != std::string::npos) {
            module = node.name.substr(0, sep);
        } else {
            module = node.name;
        }
        std::string import_key = node.language + "::" + module;
        if (emitted_imports.count(import_key) == 0) {
            out << "IMPORT " << node.language << "::" << module << ";\n";
            emitted_imports.insert(import_key);
        }
    }
    if (!emitted_imports.empty()) {
        out << "\n";
    }

    /** @} */

    /** @name Collect type mapping pairs from edges */
    /** @{ */
    // If source and target port types differ and belong to different languages,
    // emit MAP_TYPE directives.
    std::set<std::string> emitted_map_types;
    for (const auto &edge : graph.Edges()) {
        const auto *src_node = graph.GetNode(edge.source_node_id);
        const auto *tgt_node = graph.GetNode(edge.target_node_id);
        if (!src_node || !tgt_node) continue;
        if (src_node->language == tgt_node->language) continue;

        // Find the actual port types
        std::string src_type;
        for (const auto &p : src_node->outputs) {
            if (p.id == edge.source_port_id) {
                src_type = PortTypeName(p);
                break;
            }
        }
        std::string tgt_type;
        for (const auto &p : tgt_node->inputs) {
            if (p.id == edge.target_port_id) {
                tgt_type = PortTypeName(p);
                break;
            }
        }

        if (!src_type.empty() && !tgt_type.empty() &&
            src_type != "Any" && tgt_type != "Any") {
            std::string key = src_node->language + "::" + src_type +
                              " -> " + tgt_node->language + "::" + tgt_type;
            if (emitted_map_types.count(key) == 0) {
                out << "MAP_TYPE(" << src_node->language << "::" << src_type
                    << ", " << tgt_node->language << "::" << tgt_type << ");\n";
                emitted_map_types.insert(key);
            }
        }
    }
    if (!emitted_map_types.empty()) {
        out << "\n";
    }

    /** @} */

    /** @name LINK directives from edges */
    /** @{ */
    // For edges connecting nodes from different languages, emit LINK statements.
    // Use the correct ploy LINK syntax:
    //   LINK(src_lang, tgt_lang, src_func, tgt_func) RETURNS src_lang::type { MAP_TYPE(...); }
    std::set<std::string> emitted_links;
    for (const auto &edge : graph.Edges()) {
        const auto *src_node = graph.GetNode(edge.source_node_id);
        const auto *tgt_node = graph.GetNode(edge.target_node_id);
        if (!src_node || !tgt_node) continue;
        if (src_node->language == tgt_node->language) continue;
        if (src_node->language.empty() || tgt_node->language.empty()) continue;

        // Strip language prefix from node names (e.g. "cpp::math_ops::add" → "math_ops::add")
        std::string src_func = StripLangPrefix(src_node->name, src_node->language);
        std::string tgt_func = StripLangPrefix(tgt_node->name, tgt_node->language);

        std::string link_key = src_node->language + "::" + src_func +
                               " -> " + tgt_node->language + "::" + tgt_func;
        if (emitted_links.count(link_key) > 0) continue;
        emitted_links.insert(link_key);

        // Source location comment for diff/write-back
        std::string loc_cmt = SourceLocComment(src_node->loc);
        if (!loc_cmt.empty()) out << loc_cmt << "\n";

        // Determine RETURNS type from source node's output port
        std::string returns_type;
        if (!src_node->outputs.empty()) {
            std::string type_name = PortTypeName(src_node->outputs[0]);
            if (type_name != "Any") {
                returns_type = src_node->language + "::" + ToPloySrcType(type_name);
            }
        }

        out << "LINK(" << src_node->language << ", " << tgt_node->language
            << ", " << src_func << ", " << tgt_func << ")";
        if (!returns_type.empty()) {
            out << " RETURNS " << returns_type;
        }
        out << " {\n";

        // Collect per-edge type mappings
        for (const auto &e2 : graph.Edges()) {
            if (e2.source_node_id != edge.source_node_id ||
                e2.target_node_id != edge.target_node_id) {
                continue;
            }
            std::string st, tt;
            for (const auto &p : src_node->outputs) {
                if (p.id == e2.source_port_id) { st = PortTypeName(p); break; }
            }
            for (const auto &p : tgt_node->inputs) {
                if (p.id == e2.target_port_id) { tt = PortTypeName(p); break; }
            }
            if (!st.empty() && !tt.empty() && st != "Any" && tt != "Any") {
                out << "    MAP_TYPE(" << src_node->language << "::"
                    << ToPloySrcType(st) << ", "
                    << tgt_node->language << "::" << ToPloySrcType(tt) << ");\n";
            }
        }
        out << "}\n\n";
    }

    /** @} */

    /** @name Pipeline nodes */
    /** @{ */
    std::unordered_set<uint64_t> pipeline_ids;
    for (const auto &node : graph.Nodes()) {
        if (node.kind != TopologyNode::Kind::kPipeline) continue;
        pipeline_ids.insert(node.id);

        // Source location comment for diff/write-back
        std::string loc_cmt = SourceLocComment(node.loc);
        if (!loc_cmt.empty()) out << loc_cmt << "\n";

        out << "PIPELINE " << node.name << " {\n";

        // Collect child functions that feed into / out of this pipeline.
        // For simplicity, list all edges connected to this pipeline node as
        // CALL statements inside a wrapper FUNC.
        auto in_edges = graph.InEdges(node.id);
        auto out_edges = graph.OutEdges(node.id);

        // If the pipeline has typed ports, generate a wrapping FUNC
        if (!node.inputs.empty() || !node.outputs.empty()) {
            // Generate parameter list from inputs
            out << "    FUNC run(";
            for (size_t i = 0; i < node.inputs.size(); ++i) {
                if (i > 0) out << ", ";
                out << node.inputs[i].name << ": "
                    << ToPloySrcType(PortTypeName(node.inputs[i]));
            }
            out << ")";
            if (!node.outputs.empty()) {
                out << " -> " << ToPloySrcType(PortTypeName(node.outputs[0]));
            }
            out << " {\n";

            // Generate CALL statements for outgoing edges
            for (const auto *e : out_edges) {
                const auto *target = graph.GetNode(e->target_node_id);
                if (!target) continue;
                out << "        CALL(" << target->language << ", "
                    << target->name;
                // Pass pipeline inputs as arguments
                for (const auto &inp : node.inputs) {
                    out << ", " << inp.name;
                }
                out << ");\n";
            }

            if (!node.outputs.empty()) {
                out << "        RETURN " << node.inputs[0].name << ";\n";
            }
            out << "    }\n";
        }

        out << "}\n\n";
    }

    /** @} */

    /** @name Function nodes (non-pipeline, non-external) */
    /** @{ */
    for (const auto &node : graph.Nodes()) {
        if (node.kind == TopologyNode::Kind::kPipeline) continue;
        if (node.kind == TopologyNode::Kind::kExternalCall) continue;
        if (node.kind == TopologyNode::Kind::kMapFunc &&
            node.language != "ploy") {
            continue;  // External map functions are not regenerated
        }

        // Comment header with source location
        std::string loc_cmt = SourceLocComment(node.loc);
        if (!loc_cmt.empty()) {
            out << loc_cmt << "\n";
        }
        out << "// " << KindString(node.kind) << ": "
            << node.name << " [" << node.language << "]\n";

        if (node.kind == TopologyNode::Kind::kMapFunc) {
            // MAP_FUNC declaration
            out << "MAP_FUNC " << node.name << "(";
            for (size_t i = 0; i < node.inputs.size(); ++i) {
                if (i > 0) out << ", ";
                out << node.inputs[i].name << ": "
                    << ToPloySrcType(PortTypeName(node.inputs[i]));
            }
            out << ")";
            if (!node.outputs.empty()) {
                out << " -> " << ToPloySrcType(PortTypeName(node.outputs[0]));
            }
            out << " {\n";
            // Body: emit CALL statements for outgoing edges
            auto out_edges = graph.OutEdges(node.id);
            for (const auto *e : out_edges) {
                const auto *target = graph.GetNode(e->target_node_id);
                if (!target) continue;
                out << "    LET result = CALL(" << target->language << ", "
                    << target->name;
                for (const auto &inp : node.inputs) {
                    out << ", " << inp.name;
                }
                out << ");\n";
            }
            if (!node.outputs.empty()) {
                if (!out_edges.empty()) {
                    out << "    RETURN result;\n";
                } else {
                    // No outgoing edges, return first input
                    if (!node.inputs.empty()) {
                        out << "    RETURN " << node.inputs[0].name << ";\n";
                    }
                }
            }
            out << "}\n\n";
            continue;
        }

        // Regular FUNC (for kFunction, kConstructor, kMethod)
        out << "FUNC " << node.name << "(";
        for (size_t i = 0; i < node.inputs.size(); ++i) {
            if (i > 0) out << ", ";
            out << node.inputs[i].name << ": "
                << ToPloySrcType(PortTypeName(node.inputs[i]));
        }
        out << ")";
        if (!node.outputs.empty()) {
            out << " -> " << ToPloySrcType(PortTypeName(node.outputs[0]));
        }
        out << " {\n";

        // Generate body from outgoing edges
        auto out_edges = graph.OutEdges(node.id);
        std::string last_var;

        for (size_t ei = 0; ei < out_edges.size(); ++ei) {
            const auto *e = out_edges[ei];
            const auto *target = graph.GetNode(e->target_node_id);
            if (!target) continue;

            std::string var_name = "v" + std::to_string(ei);

            if (target->kind == TopologyNode::Kind::kConstructor) {
                // NEW expression
                out << "    LET " << var_name << " = NEW("
                    << target->language << ", " << target->name;
                for (const auto &inp : node.inputs) {
                    out << ", " << inp.name;
                }
                out << ");\n";
            } else if (target->kind == TopologyNode::Kind::kMethod) {
                // METHOD expression
                out << "    LET " << var_name << " = METHOD("
                    << target->language << ", " << node.inputs[0].name
                    << ", " << target->name;
                for (size_t pi = 1; pi < node.inputs.size(); ++pi) {
                    out << ", " << node.inputs[pi].name;
                }
                out << ");\n";
            } else {
                // CALL expression (default)
                out << "    LET " << var_name << " = CALL("
                    << target->language << ", " << target->name;
                for (const auto &inp : node.inputs) {
                    out << ", " << inp.name;
                }
                out << ");  // " << StatusComment(e->status) << "\n";
            }
            last_var = var_name;
        }

        // RETURN statement
        if (!node.outputs.empty()) {
            if (!last_var.empty()) {
                out << "    RETURN " << last_var << ";\n";
            } else if (!node.inputs.empty()) {
                out << "    RETURN " << node.inputs[0].name << ";\n";
            }
        }

        out << "}\n\n";
    }

    /** @} */

    /** @name EXPORT directives */
    /** @{ */
    // Export all non-external, non-map top-level functions
    for (const auto &node : graph.Nodes()) {
        if (node.kind == TopologyNode::Kind::kExternalCall) continue;
        if (node.kind == TopologyNode::Kind::kMapFunc) continue;
        if (node.language != "ploy" && !node.language.empty()) continue;

        out << "EXPORT " << node.name << ";\n";
    }

    return out.str();
}

// ============================================================================
// ParseJsonToGraph — reconstruct a graph from JSON
// ============================================================================

// Minimal hand-written JSON parser that handles the exact format produced by
// TopologyPrinter::PrintJson.  We avoid pulling in a full JSON library
// dependency in the topo_lib so that it stays lightweight.  However, if
// nlohmann/json is available at link time the polytopo CLI can parse
// arbitrarily complex JSON.  This function provides a self-contained
// fallback that covers the standard topology JSON schema.

namespace {

// Skip whitespace in a JSON string.
void SkipWs(const std::string &s, size_t &pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' ||
                               s[pos] == '\r' || s[pos] == '\t')) {
        ++pos;
    }
}

// Read a JSON string value (expects opening ").
std::string ReadJsonString(const std::string &s, size_t &pos) {
    if (pos >= s.size() || s[pos] != '"') return {};
    ++pos; // skip opening "
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            ++pos;
            switch (s[pos]) {
            case '"':  result += '"';  break;
            case '\\': result += '\\'; break;
            case 'n':  result += '\n'; break;
            case 't':  result += '\t'; break;
            default:   result += s[pos]; break;
            }
        } else {
            result += s[pos];
        }
        ++pos;
    }
    if (pos < s.size()) ++pos; // skip closing "
    return result;
}

// Read a JSON number (integer).
uint64_t ReadJsonUint(const std::string &s, size_t &pos) {
    uint64_t val = 0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
        val = val * 10 + static_cast<uint64_t>(s[pos] - '0');
        ++pos;
    }
    return val;
}

// Read a JSON boolean.
bool ReadJsonBool(const std::string &s, size_t &pos) {
    if (s.substr(pos, 4) == "true") {
        pos += 4;
        return true;
    }
    if (s.substr(pos, 5) == "false") {
        pos += 5;
        return false;
    }
    return false;
}

// Skip a JSON value (string, number, bool, null, object, array).
void SkipJsonValue(const std::string &s, size_t &pos) {
    SkipWs(s, pos);
    if (pos >= s.size()) return;
    if (s[pos] == '"') {
        ReadJsonString(s, pos);
    } else if (s[pos] == '{') {
        int depth = 1;
        ++pos;
        while (pos < s.size() && depth > 0) {
            if (s[pos] == '"') {
                ReadJsonString(s, pos);
                continue;
            }
            if (s[pos] == '{') depth++;
            else if (s[pos] == '}') depth--;
            ++pos;
        }
    } else if (s[pos] == '[') {
        int depth = 1;
        ++pos;
        while (pos < s.size() && depth > 0) {
            if (s[pos] == '"') {
                ReadJsonString(s, pos);
                continue;
            }
            if (s[pos] == '[') depth++;
            else if (s[pos] == ']') depth--;
            ++pos;
        }
    } else if (s.substr(pos, 4) == "true") {
        pos += 4;
    } else if (s.substr(pos, 5) == "false") {
        pos += 5;
    } else if (s.substr(pos, 4) == "null") {
        pos += 4;
    } else {
        // Number
        while (pos < s.size() && (std::isdigit(s[pos]) || s[pos] == '.' ||
                                   s[pos] == '-' || s[pos] == '+' ||
                                   s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
        }
    }
}

// Parse a Port from a JSON object: {"id": N, "name": "...", "type": "...", "index": N}
Port ParsePortJson(const std::string &s, size_t &pos) {
    Port port;
    SkipWs(s, pos);
    if (s[pos] != '{') return port;
    ++pos; // skip {
    while (pos < s.size() && s[pos] != '}') {
        SkipWs(s, pos);
        if (s[pos] == ',') { ++pos; SkipWs(s, pos); }
        if (s[pos] == '}') break;
        std::string key = ReadJsonString(s, pos);
        SkipWs(s, pos);
        if (s[pos] == ':') ++pos;
        SkipWs(s, pos);
        if (key == "id") {
            port.id = ReadJsonUint(s, pos);
        } else if (key == "name") {
            port.name = ReadJsonString(s, pos);
        } else if (key == "type") {
            std::string t = ReadJsonString(s, pos);
            port.type = core::Type{core::TypeKind::kClass, t};
        } else if (key == "index") {
            port.index = static_cast<int>(ReadJsonUint(s, pos));
        } else {
            SkipJsonValue(s, pos);
        }
    }
    if (pos < s.size()) ++pos; // skip }
    return port;
}

TopologyEdge::Status ParseStatus(const std::string &s) {
    if (s == "valid")            return TopologyEdge::Status::kValid;
    if (s == "implicit_convert") return TopologyEdge::Status::kImplicitConvert;
    if (s == "explicit_convert") return TopologyEdge::Status::kExplicitConvert;
    if (s == "incompatible")     return TopologyEdge::Status::kIncompatible;
    return TopologyEdge::Status::kUnknown;
}

TopologyNode::Kind ParseKind(const std::string &s) {
    if (s == "function")      return TopologyNode::Kind::kFunction;
    if (s == "constructor")   return TopologyNode::Kind::kConstructor;
    if (s == "method")        return TopologyNode::Kind::kMethod;
    if (s == "pipeline")      return TopologyNode::Kind::kPipeline;
    if (s == "map_func")      return TopologyNode::Kind::kMapFunc;
    if (s == "external_call") return TopologyNode::Kind::kExternalCall;
    return TopologyNode::Kind::kFunction;
}

} // anonymous namespace

bool ParseJsonToGraph(const std::string &json_str, TopologyGraph &out_graph) {
    size_t pos = 0;
    SkipWs(json_str, pos);
    if (pos >= json_str.size() || json_str[pos] != '{') return false;
    ++pos;

    while (pos < json_str.size() && json_str[pos] != '}') {
        SkipWs(json_str, pos);
        if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
        if (json_str[pos] == '}') break;

        std::string key = ReadJsonString(json_str, pos);
        SkipWs(json_str, pos);
        if (json_str[pos] == ':') ++pos;
        SkipWs(json_str, pos);

        if (key == "module") {
            out_graph.module_name = ReadJsonString(json_str, pos);
        } else if (key == "source_file") {
            out_graph.source_file = ReadJsonString(json_str, pos);
        } else if (key == "nodes") {
            // Parse array of node objects
            if (json_str[pos] != '[') { SkipJsonValue(json_str, pos); continue; }
            ++pos; // skip [
            while (pos < json_str.size() && json_str[pos] != ']') {
                SkipWs(json_str, pos);
                if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
                if (json_str[pos] == ']') break;
                if (json_str[pos] != '{') break;
                ++pos; // skip {

                TopologyNode node;
                while (pos < json_str.size() && json_str[pos] != '}') {
                    SkipWs(json_str, pos);
                    if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
                    if (json_str[pos] == '}') break;

                    std::string nkey = ReadJsonString(json_str, pos);
                    SkipWs(json_str, pos);
                    if (json_str[pos] == ':') ++pos;
                    SkipWs(json_str, pos);

                    if (nkey == "id") {
                        node.id = ReadJsonUint(json_str, pos);
                    } else if (nkey == "name") {
                        node.name = ReadJsonString(json_str, pos);
                    } else if (nkey == "language") {
                        node.language = ReadJsonString(json_str, pos);
                    } else if (nkey == "kind") {
                        node.kind = ParseKind(ReadJsonString(json_str, pos));
                    } else if (nkey == "is_linked") {
                        node.is_linked = ReadJsonBool(json_str, pos);
                    } else if (nkey == "inputs") {
                        // Parse port array
                        if (json_str[pos] != '[') { SkipJsonValue(json_str, pos); continue; }
                        ++pos;
                        while (pos < json_str.size() && json_str[pos] != ']') {
                            SkipWs(json_str, pos);
                            if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
                            if (json_str[pos] == ']') break;
                            Port p = ParsePortJson(json_str, pos);
                            p.direction = Port::Direction::kInput;
                            node.inputs.push_back(p);
                        }
                        if (pos < json_str.size()) ++pos; // skip ]
                    } else if (nkey == "outputs") {
                        if (json_str[pos] != '[') { SkipJsonValue(json_str, pos); continue; }
                        ++pos;
                        while (pos < json_str.size() && json_str[pos] != ']') {
                            SkipWs(json_str, pos);
                            if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
                            if (json_str[pos] == ']') break;
                            Port p = ParsePortJson(json_str, pos);
                            p.direction = Port::Direction::kOutput;
                            node.outputs.push_back(p);
                        }
                        if (pos < json_str.size()) ++pos; // skip ]
                    } else {
                        SkipJsonValue(json_str, pos);
                    }
                }
                if (pos < json_str.size()) ++pos; // skip }
                out_graph.AddNode(std::move(node));
            }
            if (pos < json_str.size()) ++pos; // skip ]
        } else if (key == "edges") {
            // Parse array of edge objects
            if (json_str[pos] != '[') { SkipJsonValue(json_str, pos); continue; }
            ++pos; // skip [
            while (pos < json_str.size() && json_str[pos] != ']') {
                SkipWs(json_str, pos);
                if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
                if (json_str[pos] == ']') break;
                if (json_str[pos] != '{') break;
                ++pos; // skip {

                TopologyEdge edge;
                while (pos < json_str.size() && json_str[pos] != '}') {
                    SkipWs(json_str, pos);
                    if (json_str[pos] == ',') { ++pos; SkipWs(json_str, pos); }
                    if (json_str[pos] == '}') break;

                    std::string ekey = ReadJsonString(json_str, pos);
                    SkipWs(json_str, pos);
                    if (json_str[pos] == ':') ++pos;
                    SkipWs(json_str, pos);

                    if (ekey == "id") {
                        edge.id = ReadJsonUint(json_str, pos);
                    } else if (ekey == "source_node") {
                        edge.source_node_id = ReadJsonUint(json_str, pos);
                    } else if (ekey == "source_port") {
                        edge.source_port_id = ReadJsonUint(json_str, pos);
                    } else if (ekey == "target_node") {
                        edge.target_node_id = ReadJsonUint(json_str, pos);
                    } else if (ekey == "target_port") {
                        edge.target_port_id = ReadJsonUint(json_str, pos);
                    } else if (ekey == "status") {
                        edge.status = ParseStatus(ReadJsonString(json_str, pos));
                    } else {
                        SkipJsonValue(json_str, pos);
                    }
                }
                if (pos < json_str.size()) ++pos; // skip }
                out_graph.AddEdge(std::move(edge));
            }
            if (pos < json_str.size()) ++pos; // skip ]
        } else {
            SkipJsonValue(json_str, pos);
        }
    }

    return true;
}

} // namespace polyglot::tools::topo

/** @} */