/**
 * @file     scm_provider.cpp
 * @brief    Out-of-line definitions for the SCM abstraction.
 *
 * Most of `ScmProvider` is header-only (pure virtual + small
 * fixture-backed test double).  This translation unit exists so the
 * static library is non-empty and so future shared logic (e.g. URL
 * parsing) has a natural home.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/scm/scm_provider.h"

namespace polyglot::tools::ui::scm {

// Anchor point — keeps the static library non-empty even when no
// concrete provider compiles into it.
const char *ProviderAbstractionVersion() { return "1.0"; }

}  // namespace polyglot::tools::ui::scm
