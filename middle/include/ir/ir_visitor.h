/**
 * @file     ir_visitor.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

// Minimal visitor kept for compatibility; can be extended as needed.
/** @brief IRVisitor class. */
class IRVisitor {
 public:
  virtual ~IRVisitor() = default;

  virtual void Visit(const LiteralExpression &expr) = 0;
  virtual void Visit(const ReturnStatement &stmt) = 0;
};

}  // namespace polyglot::ir
