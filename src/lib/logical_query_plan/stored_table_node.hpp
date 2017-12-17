#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "abstract_lqp_node.hpp"

namespace opossum {

struct ColumnID;
class TableStatistics;

/**
 * This node type represents a table stored by the table manager.
 * They are the leafs of every meaningful LQP tree.
 */
class StoredTableNode : public AbstractLQPNode {
 public:
  explicit StoredTableNode(const std::string& table_name);

  const std::string& table_name() const;

  std::string description() const override;
  std::shared_ptr<const AbstractLQPNode> find_table_name_origin(const std::string& table_name) const override;
  const std::vector<std::optional<ColumnID>>& output_column_ids_to_input_column_ids() const override;
  const std::vector<std::string>& output_column_names() const override;

  std::shared_ptr<TableStatistics> derive_statistics_from(
      const std::shared_ptr<AbstractLQPNode>& left_child = nullptr,
      const std::shared_ptr<AbstractLQPNode>& right_child = nullptr) const override;

  std::string get_verbose_column_name(ColumnID column_id) const override;

 protected:
  void _on_child_changed() override;
  std::optional<NamedColumnReference> _resolve_local_column_prefix(const NamedColumnReference& named_column_reference) const;

 private:
  const std::string _table_name;

  std::vector<std::string> _output_column_names;
};

}  // namespace opossum
