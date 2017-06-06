#include "new_order.hpp"

#include <boost/variant.hpp>

#include "concurrency/commit_context.hpp"
#include "concurrency/transaction_manager.hpp"
#include "operators/commit_records.hpp"
#include "operators/get_table.hpp"
#include "operators/table_scan.hpp"
#include "operators/product.hpp"
#include "operators/update.hpp"
#include "operators/projection.hpp"
#include "scheduler/abstract_scheduler.hpp"
#include "scheduler/operator_task.hpp"
#include "utils/helper.hpp"
#include "all_type_variant.hpp"

using namespace opossum;

namespace tpcc {

NewOrderResult AbstractNewOrderImpl::run_transaction(const NewOrderParams &params) {
  NewOrderResult result;
  std::vector<AllTypeVariant> row;

  auto t_context = TransactionManager::get().new_transaction_context();

  /**
   * GET CUSTOMER AND WAREHOUSE TAX RATE
   */
  auto get_customer_and_warehouse_tax_rate_tasks = get_get_customer_and_warehouse_tax_rate_tasks(t_context, params.w_id,
                                                                                                 params.d_id,
                                                                                                 params.c_id);
  AbstractScheduler::schedule_tasks_and_wait(get_customer_and_warehouse_tax_rate_tasks);

  row = get_customer_and_warehouse_tax_rate_tasks.back()->get_operator()->get_output()->fetch_row(
    0);
  result.c_discount = boost::get<float>(row[0]);
  result.c_last = boost::get<std::string>(row[1]);
  result.c_credit = boost::get<float>(row[2]);
  result.w_tax_rate = boost::get<float>(row[3]);

  /**
   * GET DISTRICT
   */
  auto get_district_tasks = get_get_district_tasks(t_context, params.d_id, params.w_id);
  AbstractScheduler::schedule_tasks_and_wait(get_district_tasks);
  row = get_district_tasks.back()->get_operator()->get_output()->fetch_row(0);

  result.d_next_o_id = boost::get<int32_t>(row[0]);
  result.d_tax_rate = boost::get<float>(row[0]);

  /**
   * INCREMENT NEXT ORDER ID
   */
  auto increment_next_order_id_tasks = get_increment_next_order_id_tasks(t_context, params.d_id, params.w_id,
                                                                         result.d_next_o_id);
  AbstractScheduler::schedule_tasks_and_wait(increment_next_order_id_tasks);

  /**
   * TODO(anyone) CREATE ORDER
   */

  /**
   * TODO(anyone) CREATE NEW ORDER
   */

  for (size_t ol_idx = 0; ol_idx < params.order_lines.size(); ol_idx++) {
    const auto &order_line_params = params.order_lines[ol_idx];

    NewOrderOrderLineResult order_line;

    /**
     * GET ITEM INFO
     */
    auto get_item_info_tasks = get_get_item_info_tasks(t_context, order_line_params.i_id);
    AbstractScheduler::schedule_tasks_and_wait(get_item_info_tasks);
    row = get_district_tasks.back()->get_operator()->get_output()->fetch_row(0);

    order_line.i_price = boost::get<float>(row[0]);
    order_line.i_name = boost::get<std::string>(row[1]);
    order_line.i_data = boost::get<std::string>(row[2]);

    /**
     * GET STOCK INFO
     */
    auto get_stock_info_tasks = get_get_stock_info_tasks(t_context, order_line_params.i_id, order_line_params.w_id,
                                                         params.d_id);
    AbstractScheduler::schedule_tasks_and_wait(get_stock_info_tasks);
    row = get_stock_info_tasks.back()->get_operator()->get_output()->fetch_row(0);

    order_line.s_qty = boost::get<int32_t>(row[0]);
    order_line.s_data = boost::get<std::string>(row[1]);
    order_line.s_ytd = boost::get<int32_t>(row[2]);
    order_line.s_order_cnt = boost::get<int32_t>(row[3]);
    order_line.s_remote_cnt = boost::get<int32_t>(row[4]);
    order_line.s_dist_xx = boost::get<std::string>(row[5]);

    /**
     * Calculate new s_ytd, s_qty and s_order_cnt
     */
    auto s_ytd = order_line.s_ytd + order_line_params.qty;

    int32_t s_qty = order_line.s_qty;
    if (order_line.s_qty >= order_line_params.qty + 10) {
      s_qty -= order_line_params.qty;
    } else {
      s_qty += 91 - order_line_params.qty;
    }

    auto s_order_cnt = order_line.s_order_cnt + 1;
    order_line.amount = order_line_params.qty * order_line.i_price;

    /**
     * UPDATE STOCK
     */
    auto update_stock_tasks = get_update_stock_tasks(t_context, s_qty, order_line_params.i_id,
                                                     order_line_params.w_id);
    AbstractScheduler::schedule_tasks_and_wait(update_stock_tasks);

    /**
     * TODO(TIM) CREATE ORDER LINE
     */
    auto create_order_line_tasks = get_create_order_line_tasks(t_context,
                                                               result.d_next_o_id,
                                                               params.d_id,
                                                               params.w_id,
                                                               ol_idx + 1,
                                                               order_line_params.i_id,
                                                               0, // ol_supply_w_id - we only have one warehouse
                                                               params.o_entry_d,
                                                               order_line_params.qty,
                                                               order_line.amount,
                                                               order_line.s_dist_xx);
    AbstractScheduler::schedule_tasks_and_wait(create_order_line_tasks);


    /**
     * Add results
     */
    result.order_lines.emplace_back(order_line);
  }

  // Commit transaction.
  TransactionManager::get().prepare_commit(*t_context);

  auto commit = std::make_shared<CommitRecords>();
  commit->set_transaction_context(t_context);

  auto commit_task = std::make_shared<OperatorTask>(commit);
  commit_task->schedule();
  commit_task->join();

  TransactionManager::get().commit(*t_context);
}

TaskVector NewOrderRefImpl::get_get_customer_and_warehouse_tax_rate_tasks(
  const std::shared_ptr<TransactionContext> t_context, const int32_t w_id, const int32_t d_id,
  const int32_t c_id) {
  /**
   * SELECT c_discount, c_last, c_credit, w_tax
   * FROM customer, warehouse
   * WHERE w_id = :w_id AND c_w_id = w_id AND c_d_id = :d_id AND c_id = :c_id
   */

  // Operators
  const auto c_gt = std::make_shared<GetTable>("CUSTOMER");
  const auto c_ts1 = std::make_shared<TableScan>(c_gt, "C_W_ID", "=", w_id);
  const auto c_ts2 = std::make_shared<TableScan>(c_ts1, "C_D_ID", "=", d_id);
  const auto c_ts3 = std::make_shared<TableScan>(c_ts2, "C_ID", "=", c_id);

  const auto w_gt = std::make_shared<GetTable>("WAREHOUSE");
  const auto w_ts = std::make_shared<TableScan>(w_gt, "W_ID", "=", w_id);

  // Both operators should have exactly one row -> Product operator should have smallest overhead.
  const auto join = std::make_shared<Product>(c_ts3, w_ts);

  const std::vector<std::string> columns = {"C_DISCOUNT", "C_LAST", "C_CREDIT", "W_TAX"};
  const auto proj = std::make_shared<Projection>(join, columns);

  set_transaction_context_for_operators(t_context, {c_gt, c_ts1, c_ts2, c_ts3, w_gt, w_ts, join, proj});

  // Tasks
  const auto c_gt_t = std::make_shared<OperatorTask>(c_gt);
  const auto c_ts1_t = std::make_shared<OperatorTask>(c_ts1);
  const auto c_ts2_t = std::make_shared<OperatorTask>(c_ts2);
  const auto c_ts3_t = std::make_shared<OperatorTask>(c_ts3);

  const auto w_gt_t = std::make_shared<OperatorTask>(w_gt);
  const auto w_ts_t = std::make_shared<OperatorTask>(w_ts);

  const auto join_t = std::make_shared<OperatorTask>(join);
  const auto proj_t = std::make_shared<OperatorTask>(proj);

  // Dependencies
  c_gt_t->set_as_predecessor_of(c_ts1_t);
  c_ts1_t->set_as_predecessor_of(c_ts2_t);
  c_ts2_t->set_as_predecessor_of(c_ts3_t);

  w_gt_t->set_as_predecessor_of(w_ts_t);

  c_ts3_t->set_as_predecessor_of(join_t);
  w_ts_t->set_as_predecessor_of(join_t);

  join_t->set_as_predecessor_of(proj_t);

  return {c_gt_t, c_ts1_t, c_ts2_t, c_ts3_t, w_gt_t, w_ts_t, join_t, proj_t};
}

TaskVector
NewOrderRefImpl::get_get_district_tasks(const std::shared_ptr<TransactionContext> t_context,
                                        const int32_t d_id, const int32_t w_id) {
  /**
   * SELECT d_next_o_id, d_tax
   * FROM district
   * WHERE d_id = :d_id AND d_w_id = :w_id
   */

  // Operators
  const auto gt = std::make_shared<GetTable>("DISTRICT");

  const auto ts1 = std::make_shared<TableScan>(gt, "D_ID", "=", d_id);
  const auto ts2 = std::make_shared<TableScan>(ts1, "D_W_ID", "=", w_id);

  const std::vector<std::string> columns = {"D_TAX", "D_NEXT_O_ID"};
  const auto proj = std::make_shared<Projection>(ts2, columns);

  set_transaction_context_for_operators(t_context, {gt, ts1, ts2, proj});

  // Tasks
  const auto gt_t = std::make_shared<OperatorTask>(gt);
  const auto ts1_t = std::make_shared<OperatorTask>(ts1);
  const auto ts2_t = std::make_shared<OperatorTask>(ts2);
  const auto proj_t = std::make_shared<OperatorTask>(proj);

  // Dependencies
  gt_t->set_as_predecessor_of(ts1_t);
  ts1_t->set_as_predecessor_of(ts2_t);
  ts2_t->set_as_predecessor_of(proj_t);

  return {gt_t, ts1_t, ts2_t, proj_t};
}

TaskVector NewOrderRefImpl::get_increment_next_order_id_tasks(
  const std::shared_ptr<TransactionContext> t_context, const int32_t d_id, const int32_t d_w_id,
  const int32_t d_next_o_id) {
  /**
  * UPDATE district
  * SET d_next_o_id = :d_next_o_id + 1
  * WHERE d_id = :d_id AND d_w_id = :w_id
  */

  // Operators
  const auto gt = std::make_shared<GetTable>("DISTRICT");
  const auto ts1 = std::make_shared<TableScan>(gt, "D_ID", "=", d_id);
  const auto ts2 = std::make_shared<TableScan>(ts1, "D_W_ID", "=", d_w_id);

  const std::vector<std::string> columns = {"D_NEXT_O_ID"};
  const auto original_rows = std::make_shared<Projection>(ts2, columns);

  const Projection::ProjectionDefinitions definitions{
    Projection::ProjectionDefinition{std::to_string(d_next_o_id) + "+1", "int", "fix"}};
  const auto updated_rows = std::make_shared<Projection>(ts2, definitions);

  const auto update = std::make_shared<Update>("DISTRICT", original_rows, updated_rows);

  set_transaction_context_for_operators(t_context, {gt, ts1, ts2, original_rows, updated_rows, update});

  // Tasks
  const auto gt_t = std::make_shared<OperatorTask>(gt);
  const auto ts1_t = std::make_shared<OperatorTask>(ts1);
  const auto ts2_t = std::make_shared<OperatorTask>(ts2);
  const auto original_rows_t = std::make_shared<OperatorTask>(original_rows);
  const auto updated_rows_t = std::make_shared<OperatorTask>(updated_rows);
  const auto update_t = std::make_shared<OperatorTask>(update);

  // Dependencies
  gt_t->set_as_predecessor_of(ts1_t);
  ts1_t->set_as_predecessor_of(ts2_t);

  ts2_t->set_as_predecessor_of(original_rows_t);
  ts2_t->set_as_predecessor_of(updated_rows_t);

  original_rows_t->set_as_predecessor_of(update_t);
  updated_rows_t->set_as_predecessor_of(update_t);

  return {gt_t, ts1_t, ts2_t, original_rows_t, updated_rows_t, update_t};
}

// TODO(tim): re-factor/extend Insert so that we do not have to build a Table object first.
//  std::vector<std::shared_ptr<OperatorTask>> get_create_order_tasks(
//          const std::shared_ptr<TransactionContext> t_context, const int32_t d_next_o_id, const int32_t d_id, const
//          int32_t w_id, const int32_t c_id, const int32_t o_entry_d, const int32_t o_carrier_id, const int32_t
//          o_ol_cnt, const int32_t o_all_local) {
//    /**
//     * INSERT INTO ORDERS (O_ID, O_D_ID, O_W_ID, O_C_ID, O_ENTRY_D, O_CARRIER_ID, O_OL_CNT, O_ALL_LOCAL)
//     * VALUES (?, ?, ?, ?, ?, ?, ?, ?)
//     */
//  }
//
//  std::vector<std::shared_ptr<OperatorTask>> get_create_new_order_tasks(
//          const std::shared_ptr<TransactionContext> t_context, const int32_t o_id, const int32_t d_id, const int32_t
//          w_id) {
//    /**
//     * INSERT INTO NEW_ORDER (NO_O_ID, NO_D_ID, NO_W_ID) VALUES (?, ?, ?)
//     */
//  }

TaskVector NewOrderRefImpl::get_create_order_tasks(
  const std::shared_ptr<TransactionContext> t_context, const int32_t d_next_o_id, const int32_t d_id, const
int32_t w_id, const int32_t c_id, const int32_t o_entry_d, const int32_t o_carrier_id, const int32_t
  o_ol_cnt, const int32_t o_all_local) {
  return {};
}

TaskVector NewOrderRefImpl::get_create_new_order_tasks(
  const std::shared_ptr<TransactionContext> t_context, const int32_t o_id, const int32_t d_id, const int32_t
w_id) {
  return {};
}

TaskVector NewOrderRefImpl::get_get_item_info_tasks(
  const std::shared_ptr<TransactionContext> t_context, const int32_t ol_i_id) {
  /**
   * SELECT i_price, i_name , i_data
   * FROM item
   * WHERE i_id = :ol_i_id;
   */

  // Operators
  auto gt = std::make_shared<GetTable>("ITEM");
  auto ts = std::make_shared<TableScan>(gt, "I_ID", "=", ol_i_id);

  const std::vector<std::string> columns = {"I_PRICE", "I_NAME", "I_DATA"};
  auto proj = std::make_shared<Projection>(ts, columns);

  set_transaction_context_for_operators(t_context, {gt, ts, proj});

  // Tasks
  auto gt_t = std::make_shared<OperatorTask>(gt);
  auto ts_t = std::make_shared<OperatorTask>(ts);
  auto proj_t = std::make_shared<OperatorTask>(proj);

  // Dependencies
  gt_t->set_as_predecessor_of(ts_t);
  ts_t->set_as_predecessor_of(proj_t);

  return {gt_t, ts_t, proj_t};
}

TaskVector NewOrderRefImpl::get_get_stock_info_tasks(
  const std::shared_ptr<TransactionContext> t_context, const int32_t ol_i_id, const int32_t ol_supply_w_id,
  const int32_t d_id) {
  /**
      * SELECT
      *  s_quantity, s_data,
      *  s_dist_01, s_dist_02, s_dist_03, s_dist_04, s_dist_05 s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10
      * FROM stock
      * WHERE s_i_id = :ol_i_id AND s_w_id = :ol_supply_w_id;
      */

  // Operators
  auto gt = std::make_shared<GetTable>("STOCK");
  auto ts1 = std::make_shared<TableScan>(gt, "S_I_ID", "=", ol_i_id);
  auto ts2 = std::make_shared<TableScan>(ts1, "S_W_ID", "=", ol_supply_w_id);

  std::vector<std::string> columns = {"S_DIST_01", "S_DIST_02", "S_DIST_03", "S_DIST_04", "S_DIST_05",
                                      "S_DIST_06", "S_DIST_07", "S_DIST_08", "S_DIST_09", "S_DIST_10"};
  auto proj = std::make_shared<Projection>(ts2, columns);

  set_transaction_context_for_operators(t_context, {gt, ts1, ts2, proj});

  // Tasks
  auto gt_t = std::make_shared<OperatorTask>(gt);
  auto ts1_t = std::make_shared<OperatorTask>(ts1);
  auto ts2_t = std::make_shared<OperatorTask>(ts2);
  auto proj_t = std::make_shared<OperatorTask>(proj);

  // Dependencies
  gt_t->set_as_predecessor_of(ts1_t);
  ts1_t->set_as_predecessor_of(ts2_t);
  ts2_t->set_as_predecessor_of(proj_t);

  return {gt_t, ts1_t, ts2_t, proj_t};
}

TaskVector
NewOrderRefImpl::get_update_stock_tasks(const std::shared_ptr<TransactionContext> t_context,
                                        const int32_t s_quantity, const int32_t ol_i_id,
                                        const int32_t ol_supply_w_id) {
  /**
   * UPDATE stock
   * SET s_quantity = :s_quantity
   * WHERE s_i_id = :ol_i_id AND s_w_id = :ol_supply_w_id
   */

  // Operators
  const auto gt = std::make_shared<GetTable>("STOCK");
  const auto ts1 = std::make_shared<TableScan>(gt, "S_I_ID", "=", ol_i_id);
  const auto ts2 = std::make_shared<TableScan>(ts1, "S_W_ID", "=", ol_supply_w_id);

  const std::vector<std::string> columns = {"S_QUANTITY"};
  const auto original_rows = std::make_shared<Projection>(ts2, columns);

  const Projection::ProjectionDefinitions definitions{
    Projection::ProjectionDefinition{std::to_string(s_quantity), "int", "fix"}};
  const auto updated_rows = std::make_shared<Projection>(ts2, definitions);

  const auto update = std::make_shared<Update>("STOCK", original_rows, updated_rows);

  set_transaction_context_for_operators(t_context, {gt, ts1, ts2, original_rows, updated_rows, update});

  // Tasks
  const auto gt_t = std::make_shared<OperatorTask>(gt);
  const auto ts1_t = std::make_shared<OperatorTask>(ts1);
  const auto ts2_t = std::make_shared<OperatorTask>(ts2);
  const auto original_rows_t = std::make_shared<OperatorTask>(original_rows);
  const auto updated_rows_t = std::make_shared<OperatorTask>(updated_rows);
  const auto update_t = std::make_shared<OperatorTask>(update);

  // Dependencies
  gt_t->set_as_predecessor_of(ts1_t);
  ts1_t->set_as_predecessor_of(ts2_t);

  ts2_t->set_as_predecessor_of(original_rows_t);
  ts2_t->set_as_predecessor_of(updated_rows_t);

  original_rows_t->set_as_predecessor_of(update_t);
  updated_rows_t->set_as_predecessor_of(update_t);

  return {gt_t, ts1_t, ts2_t, original_rows_t, updated_rows_t, update_t};
}

TaskVector NewOrderRefImpl::get_create_order_line_tasks(
  const std::shared_ptr<TransactionContext> t_context,
  const int32_t ol_o_id,
  const int32_t ol_d_id,
  const int32_t ol_w_id,
  const int32_t ol_number,
  const int32_t ol_i_id,
  const int32_t ol_supply_w_id,
  const int32_t ol_delivery_d,
  const int32_t ol_quantity,
  const float ol_amount,
  const std::string &ol_dist_info) {
  /**
   * INSERT INTO ORDER_LINE
   * (OL_O_ID, OL_D_ID, OL_W_ID, OL_NUMBER, OL_I_ID, OL_SUPPLY_W_ID, OL_DELIVERY_D, OL_QUANTITY,
   * OL_AMOUNT, OL_DIST_INFO) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
   */
  return {};
}

}

