/**
 * @file     token_pool.cpp
 * @brief    Implementation of the shared frontend token pool.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "frontends/common/include/token_pool.h"

#include <stdexcept>
#include <utility>

namespace polyglot::frontends {

// ---------------------------------------------------------------------------
// TokenPool
// ---------------------------------------------------------------------------

TokenPool::TokenPool(std::size_t arena_chunk_bytes) :
    arena_(std::make_unique<StringArena>(arena_chunk_bytes)),
    identifiers_(std::make_unique<IdentifierTable>(*arena_)) {}

TokenHandle TokenPool::Add(Token token) {
  // Mirror the lexeme into the arena so that lookups via InternLexeme observe
  // a stable view backing the same bytes.  The token's owned std::string is
  // preserved for compatibility with consumers that hold a Token by value.
  if (!token.lexeme.empty()) {
    (void)arena_->Intern(token.lexeme);
  }
  const TokenHandle handle = static_cast<TokenHandle>(tokens_.size());
  tokens_.push_back(std::move(token));
  return handle;
}

const Token &TokenPool::Get(TokenHandle handle) const {
  if (handle >= tokens_.size()) {
    throw std::out_of_range("TokenPool::Get: handle out of range");
  }
  return tokens_[handle];
}

std::string_view TokenPool::InternLexeme(std::string_view text) {
  return arena_->Intern(text);
}

SymbolId TokenPool::InternIdentifier(std::string_view name) {
  return identifiers_->Intern(name);
}

SymbolId TokenPool::FindIdentifier(std::string_view name) const {
  return identifiers_->Find(name);
}

std::string_view TokenPool::IdentifierName(SymbolId id) const {
  return identifiers_->Lookup(id);
}

Token TokenPool::MakeToken(TokenKind kind, std::string_view lexeme,
                           const core::SourceLoc &loc) {
  // Intern bytes so the resulting Token's lexeme is rooted in stable storage
  // even if the source buffer is later destroyed by the caller.
  const std::string_view stable = arena_->Intern(lexeme);
  Token                  t;
  t.kind   = kind;
  t.lexeme = std::string(stable);
  t.loc    = loc;
  return t;
}

TokenPool::Snapshot TokenPool::Save() const noexcept {
  Snapshot snap;
  snap.token_count         = tokens_.size();
  snap.arena_mark          = arena_->CurrentMark();
  snap.identifier_snapshot = identifiers_->CurrentSnapshot();
  return snap;
}

void TokenPool::Restore(const Snapshot &snap) {
  if (snap.token_count > tokens_.size()) {
    return;
  }
  while (tokens_.size() > snap.token_count) {
    tokens_.pop_back();
  }
  identifiers_->RestoreSnapshot(snap.identifier_snapshot);
  arena_->RewindTo(snap.arena_mark);
}

void TokenPool::Reset() {
  tokens_.clear();
  identifiers_->Reset();
  arena_->Reset();
}

TokenPoolStats TokenPool::Stats() const noexcept {
  TokenPoolStats s;
  s.tokens             = tokens_.size();
  s.arena_bytes        = arena_->BytesUsed();
  s.arena_capacity     = arena_->Capacity();
  s.unique_identifiers = identifiers_->Size();
  s.intern_hits        = identifiers_->HitCount();
  s.intern_misses      = identifiers_->MissCount();
  return s;
}

// ---------------------------------------------------------------------------
// SharedTokenPool
// ---------------------------------------------------------------------------

SharedTokenPool::SharedTokenPool(std::size_t arena_chunk_bytes) : inner_(arena_chunk_bytes) {}

TokenHandle SharedTokenPool::Add(Token token) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return inner_.Add(std::move(token));
}

Token SharedTokenPool::Get(TokenHandle handle) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return inner_.Get(handle); // returned by value, lock released on return
}

std::size_t SharedTokenPool::Size() const noexcept {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return inner_.Size();
}

std::string_view SharedTokenPool::InternLexeme(std::string_view text) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return inner_.InternLexeme(text);
}

SymbolId SharedTokenPool::InternIdentifier(std::string_view name) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return inner_.InternIdentifier(name);
}

SymbolId SharedTokenPool::FindIdentifier(std::string_view name) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return inner_.FindIdentifier(name);
}

std::string_view SharedTokenPool::IdentifierName(SymbolId id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return inner_.IdentifierName(id);
}

Token SharedTokenPool::MakeToken(TokenKind kind, std::string_view lexeme,
                                 const core::SourceLoc &loc) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return inner_.MakeToken(kind, lexeme, loc);
}

TokenPoolStats SharedTokenPool::Stats() const noexcept {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return inner_.Stats();
}

void SharedTokenPool::Reset() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  inner_.Reset();
}

} // namespace polyglot::frontends
