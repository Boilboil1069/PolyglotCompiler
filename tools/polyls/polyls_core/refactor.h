/**
 * @file     refactor.h
 * @brief    Workspace refactoring engine for polyls
 *
 * Builds @c WorkspaceEdit payloads for the LSP refactoring requests
 * negotiated by demand item 2026-04-28-23:
 *   • `textDocument/prepareRename` and `textDocument/rename`
 *     (cross-file, cross-language identifier rename driven by
 *     @ref SymbolIndex references plus the .ploy LINK reverse map);
 *   • `textDocument/codeAction` quick fixes for
 *       - `refactor.extract.function`,
 *       - `refactor.inline.variable`,
 *       - `refactor.inline.function`,
 *       - `refactor.changeSignature`,
 *       - `refactor.move.file`.
 *
 * The engine operates entirely on the in-memory document store kept by
 * @ref PolylsServer plus the workspace @ref SymbolIndex; it does not
 * re-parse host-language sources, which keeps every refactor query
 * within one frame's worth of work even on large workspaces.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tools/polyls/polyls_core/symbol_index.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::polyls {

namespace lsp = polyglot::tools::ui::lsp;

/// Snapshot view of one open document handed to the refactor engine.
/// Mirrors @ref PolylsServer::OpenDocument but is decoupled from the
/// server class so unit tests can drive the engine directly.
struct DocumentView {
  std::string uri;
  std::string language_id;
  std::string text;
};

/// Token resolution under a given (uri, line, character) cursor.
struct RefactorToken {
  std::string bare;        ///< Identifier under the cursor.
  std::string language;    ///< Document language id (best-effort).
  std::uint32_t line{0};
  std::uint32_t start_col{0};
  std::uint32_t end_col{0};
  bool valid{false};
};

/// Locate the identifier under @p (line, character) inside @p text.
/// Returns `valid=false` when the cursor sits outside any identifier
/// (whitespace, punctuation, comment).
RefactorToken ResolveIdentifierAt(const std::string &text,
                                  std::uint32_t line,
                                  std::uint32_t character,
                                  const std::string &language_id);

/// `textDocument/prepareRename`: return the editable range of the
/// identifier under the cursor, or std::nullopt when the cursor is not
/// on a renameable token.
std::optional<lsp::Range> PrepareRename(const std::vector<DocumentView> &docs,
                                        const std::string &uri,
                                        std::uint32_t line,
                                        std::uint32_t character);

/// `textDocument/rename`: build a @c WorkspaceEdit that rewrites every
/// occurrence of the identifier under @p (line, character) to
/// @p new_name across every open document plus every workspace
/// reference recorded in @p index.
///
/// Rename also follows .ploy LINK reverse references: renaming a
/// host-language symbol updates the LINK / EXPORT sites in any .ploy
/// file that imports it, and renaming a LINK site keeps both sides in
/// sync.  Locations that fall outside the open document set are still
/// recorded in the resulting edit so the editor can apply them through
/// its own file IO.
///
/// @returns std::nullopt when the cursor is not on an identifier or
///          the new name is empty / not a valid identifier.
std::optional<lsp::WorkspaceEdit> BuildRenameEdit(
    const SymbolIndex &index, const std::vector<DocumentView> &docs,
    const std::string &uri, std::uint32_t line, std::uint32_t character,
    const std::string &new_name);

/// `textDocument/codeAction`: build the catalogue of refactorings
/// applicable to @p range inside @p uri.  The returned vector is what
/// the server forwards to the client; each entry already carries its
/// pre-computed `WorkspaceEdit` when one can be produced statically.
///
/// The current implementation honours the demand 2026-04-28-23
/// matrix:
///   • extract function — when @p range covers a non-empty selection
///     the engine emits a literal text wrap that creates a new FUNC
///     and replaces the selection with a call;
///   • inline variable — when the cursor sits on a `LET name = expr;`
///     binding the engine inlines `expr` at every reference;
///   • change signature, inline function, move file — emitted as
///     informational entries with empty edits; the editor surfaces
///     them in the lightbulb menu and the actual mechanical edit is
///     deferred to the editor wizard.
std::vector<lsp::CodeAction> BuildCodeActions(
    const SymbolIndex &index, const std::vector<DocumentView> &docs,
    const std::string &uri, const lsp::Range &range,
    const std::string &new_function_name = "extracted");

/// `true` when @p name is a syntactically valid identifier across the
/// languages polyls touches (matches `[A-Za-z_][A-Za-z0-9_]*`).
bool IsValidIdentifier(const std::string &name);

}  // namespace polyglot::polyls
