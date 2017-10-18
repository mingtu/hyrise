#pragma once

#include <bitset>
#include <optional>
#include <memory>
#include <vector>

#include "join_graph.hpp"
#include "join_ordering_types.hpp"
#include "types.hpp"

namespace opossum {

class AbstractASTNode;
class TableStatistics;

using TableNodeSet = std::bitset<32>;

struct JoinTableNode {
  std::shared_ptr<AbstractASTNode> ast_node;
  size_t index = 0;
};

struct JoinPlanNode {
  std::shared_ptr<TableStatistics> statistics;

  std::optional<size_t> table_idx;

  JoinPredicate join_predicate;
  std::shared_ptr<JoinPlanNode> left_child;
  std::shared_ptr<JoinPlanNode> right_child;
  std::vector<JoinPredicate> pending_predicates;
};

class DPsize final {
 public:
  explicit DPsize(const std::shared_ptr<JoinGraph>& join_graph);

  std::shared_ptr<AbstractASTNode> run();

 private:
  std::vector<TableNodeSet> _generate_subsets_of_size(uint32_t set_size, uint32_t subset_size) const;
  std::vector<JoinEdge> _find_edges_between_sets(const TableNodeSet& left, const TableNodeSet& right) const;
  std::shared_ptr<JoinPlanNode> _join_plans(const std::shared_ptr<JoinPlanNode>& best_plan_left,
                                            const std::shared_ptr<JoinPlanNode>& best_plan_right,
                                            const std::vector<JoinEdge>& join_edges) const;
  std::shared_ptr<AbstractASTNode> _build_join_tree(const std::shared_ptr<JoinPlanNode>& plan_node) const;

 private:
  std::shared_ptr<JoinGraph> _join_graph;
};
}