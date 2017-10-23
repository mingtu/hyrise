#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "optimizer/abstract_syntax_tree/abstract_ast_node.hpp"
#include "types.hpp"

namespace opossum {

/**
 * Node that represents a table that has no data backing it, but may provide (mocked) statistics
 */
class MockTableNode : public AbstractASTNode {
 public:
  MockTableNode(const std::string & name, size_t column_count);
  MockTableNode(const std::shared_ptr<TableStatistics>& statistics, const std::string & name);

  const std::vector<ColumnID>& output_column_id_to_input_column_id() const override;
  const std::vector<std::string>& output_column_names() const override;

  bool knows_table(const std::string& table_name) const override;

  std::vector<ColumnID> get_output_column_ids_for_table(const std::string& table_name) const override;

  std::optional<ColumnID> find_column_id_by_named_column_reference(
      const NamedColumnReference& named_column_reference) const override;

  std::string description() const override;
  std::string get_verbose_column_name(ColumnID column_id) const override;

 protected:
  void _on_child_changed() override;

 private:
  std::string _name;
  std::vector<ColumnID> _output_column_id_to_input_column_id;
  std::vector<std::string> _output_column_names;
};
}  // namespace opossum
