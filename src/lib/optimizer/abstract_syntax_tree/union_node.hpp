#pragma once

#include "optimizer/abstract_syntax_tree/abstract_ast_node.hpp"
#include "types.hpp"

namespace opossum {

class UnionNode : public AbstractASTNode {
 public:
  UnionNode(UnionMode union_mode);

  UnionMode union_mode() const;

  std::string description() const override;

 private:
  UnionMode _union_mode;
};
}