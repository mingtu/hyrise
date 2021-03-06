#pragma once

#include <string>

#include "abstract_lqp_node.hpp"

namespace opossum {

/**
 * This node type represents the SHOW TABLES management command.
 */
class ShowTablesNode : public AbstractLQPNode {
 public:
  ShowTablesNode();

  std::string description() const override;

 protected:
  std::shared_ptr<AbstractLQPNode> _deep_copy_impl() const override;
};

}  // namespace opossum
