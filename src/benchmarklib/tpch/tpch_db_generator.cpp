#include "tpch_db_generator.hpp"

extern "C" {
#include <dss.h>
#include <dsstypes.h>
#include <rnd.h>
}

#include <utility>

#include "storage/chunk.hpp"
#include "storage/storage_manager.hpp"

/**
 * Declare tpch_dbgen function we use that are not exposed by tpch-dbgen via headers
 */
extern "C" {
void row_start(int t);
void row_stop(int t);
}

extern char** asc_date;
extern seed_t Seed[];

namespace {

// clang-format off
const auto customer_column_types = boost::hana::tuple      <int32_t,    std::string, std::string, int32_t,       std::string, float,       std::string,    std::string>();  // NOLINT
const auto customer_column_names = boost::hana::make_tuple("c_custkey", "c_name",    "c_address", "c_nationkey", "c_phone",   "c_acctbal", "c_mktsegment", "c_comment"); // NOLINT

const auto order_column_types = boost::hana::tuple      <int32_t,     int32_t,     std::string,     float,          std::string,   std::string,       std::string, int32_t,          std::string>();  // NOLINT
const auto order_column_names = boost::hana::make_tuple("o_orderkey", "o_custkey", "o_orderstatus", "o_totalprice", "o_orderdate", "o_orderpriority", "o_clerk",   "o_shippriority", "o_comment");  // NOLINT

const auto lineitem_column_types = boost::hana::tuple      <int32_t,     int32_t,     int32_t,     int32_t,        float,        float,             float,        float,   std::string,    std::string,    std::string,  std::string,    std::string,     std::string,      std::string,  std::string>();  // NOLINT
const auto lineitem_column_names = boost::hana::make_tuple("l_orderkey", "l_partkey", "l_suppkey", "l_linenumber", "l_quantity", "l_extendedprice", "l_discount", "l_tax", "l_returnflag", "l_linestatus", "l_shipdate", "l_commitdate", "l_receiptdate", "l_shipinstruct", "l_shipmode", "l_comment");  // NOLINT

const auto part_column_types = boost::hana::tuple      <int32_t,    std::string, std::string, std::string, std::string, int32_t,  std::string,   int32_t,        std::string>();  // NOLINT
const auto part_column_names = boost::hana::make_tuple("p_partkey", "p_name",    "p_mfgr",    "p_brand",   "p_type",    "p_size", "p_container", "p_retailsize", "p_comment");  // NOLINT

const auto partsupp_column_types = boost::hana::tuple<     int32_t,      int32_t,      int32_t,       float,           std::string>();  // NOLINT
const auto partsupp_column_names = boost::hana::make_tuple("ps_partkey", "ps_suppkey", "ps_availqty", "ps_supplycost", "ps_comment");  // NOLINT

const auto supplier_column_types = boost::hana::tuple<     int32_t,     std::string, std::string, int32_t,       std::string, float,       std::string>();  // NOLINT
const auto supplier_column_names = boost::hana::make_tuple("s_suppkey", "s_name",    "s_address", "s_nationkey", "s_phone",   "s_acctbal", "s_comment");  // NOLINT

const auto nation_column_types = boost::hana::tuple<     int32_t,       std::string, int32_t,       std::string>();  // NOLINT
const auto nation_column_names = boost::hana::make_tuple("n_nationkey", "n_name",    "n_regionkey", "n_comment");  // NOLINT

const auto region_column_types = boost::hana::tuple<     int32_t,       std::string, std::string>();  // NOLINT
const auto region_column_names = boost::hana::make_tuple("r_regionkey", "r_name",    "r_comment");  // NOLINT

// clang-format on

/**
 * Helper to build a table with a static (specified by template args `ColumnTypes`) column type layout. Keeps a vector
 * for each column and appends values to them in append_row(). Automatically creates chunks in accordance with the
 * specified chunk size.
 *
 * No real need to tie this to TPCH, but atm it is only used here so that's where it resides.
 */
template <typename... DataTypes>
class TableBuilder {
 public:
  template <typename... Strings>
  TableBuilder(size_t chunk_size, const boost::hana::tuple<DataTypes...>& column_types,
               const boost::hana::tuple<Strings...>& column_names, opossum::ChunkUseMvcc use_mvcc)
      : _use_mvcc(use_mvcc) {
    _table = std::make_shared<opossum::Table>(chunk_size);

    /**
     * Create a tuple ((column_name0, column_type0), (column_name1, column_type1), ...) so we can iterate over the
     * columns.
     * fold_left as below does this in order, I think boost::hana::zip_with() doesn't, which is why I'm doing two steps
     * here.
     */
    const auto column_names_and_data_types = boost::hana::zip_with(
        [&](auto column_type, auto column_name) {
          return boost::hana::make_tuple(column_name, opossum::data_type_from_type<decltype(column_type)>());
        },
        column_types, column_names);

    // Iterate over the column types/names and create the columns.
    boost::hana::fold_left(column_names_and_data_types, _table, [](auto table, auto column_name_and_type) {
      table->add_column_definition(column_name_and_type[boost::hana::llong_c<0>],
                                   column_name_and_type[boost::hana::llong_c<1>]);
      return table;
    });
  }

  std::shared_ptr<opossum::Table> finish_table() {
    if (_current_chunk_row_count() > 0) {
      _emit_chunk();
    }

    return _table;
  }

  void append_row(DataTypes&&... column_values) {
    // Create a tuple ([&column_vector0, value0], ...)
    auto vectors_and_values = boost::hana::zip_with(
        [](auto& vector, auto&& value) { return boost::hana::make_tuple(std::reference_wrapper(vector), value); },
        _column_vectors, boost::hana::make_tuple(std::forward<DataTypes>(column_values)...));

    // Add the values to their respective column vector
    boost::hana::for_each(vectors_and_values, [](auto vector_and_value) {
      vector_and_value[boost::hana::llong_c<0>].get().push_back(vector_and_value[boost::hana::llong_c<1>]);
    });

    if (_current_chunk_row_count() >= _table->max_chunk_size()) {
      _emit_chunk();
    }
  }

 private:
  std::shared_ptr<opossum::Table> _table;
  opossum::ChunkUseMvcc _use_mvcc;
  boost::hana::tuple<opossum::pmr_concurrent_vector<DataTypes>...> _column_vectors;

  size_t _current_chunk_row_count() const { return _column_vectors[boost::hana::llong_c<0>].size(); }

  void _emit_chunk() {
    opossum::Chunk chunk(_use_mvcc);

    // Create a column from each column vector and add it to the Chunk, then re-initialize the vector
    boost::hana::for_each(_column_vectors, [&](auto&& vector) {
      using T = typename std::decay_t<decltype(vector)>::value_type;
      chunk.add_column(std::make_shared<opossum::ValueColumn<T>>(std::move(vector)));
      vector = std::decay_t<decltype(vector)>();
    });
    _table->emplace_chunk(std::move(chunk));
  }
};

std::unordered_map<opossum::TpchTable, std::underlying_type_t<opossum::TpchTable>> tpch_table_to_dbgen_id = {
    {opossum::TpchTable::Part, PART},     {opossum::TpchTable::PartSupp, PSUPP}, {opossum::TpchTable::Supplier, SUPP},
    {opossum::TpchTable::Customer, CUST}, {opossum::TpchTable::Orders, ORDER},   {opossum::TpchTable::LineItem, LINE},
    {opossum::TpchTable::Nation, NATION}, {opossum::TpchTable::Region, REGION}};

template <typename DSSType, typename MKRetType, typename... Args>
DSSType _call_dbgen_mk(size_t idx, MKRetType (*mk_fn)(DSS_HUGE, DSSType* val, Args...), opossum::TpchTable table,
                       Args... args) {
  /**
   * Preserve calling scheme (row_start(); mk...(); row_stop(); as in dbgen's gen_tbl())
   */

  const auto dbgen_table_id = tpch_table_to_dbgen_id.at(table);

  row_start(dbgen_table_id);

  DSSType value;
  mk_fn(idx, &value, std::forward<Args>(args)...);

  row_stop(dbgen_table_id);

  return value;
}

float _convert_money(DSS_HUGE cents) {
  const auto dollars = cents / 100;
  cents %= 100;
  return dollars + (static_cast<float>(cents)) / 100.0f;
}

/**
 * Call this after using dbgen to avoid memory leaks
 */
void _dbgen_cleanup() {
  for (auto* distribution : {&nations,     &regions,        &o_priority_set, &l_instruct_set,
                             &l_smode_set, &l_category_set, &l_rflag_set,    &c_mseg_set,
                             &colors,      &p_types_set,    &p_cntr_set,     &articles,
                             &nouns,       &adjectives,     &adverbs,        &prepositions,
                             &verbs,       &terminators,    &auxillaries,    &np,
                             &vp,          &grammar}) {
    free(distribution->permute);
    distribution->permute = nullptr;
  }

  if (asc_date != nullptr) {
    for (size_t idx = 0; idx < TOTDATE; ++idx) {
      free(asc_date[idx]);
    }
    free(asc_date);
  }
  asc_date = nullptr;
}

}  // namespace

namespace opossum {

std::unordered_map<TpchTable, std::string> tpch_table_names = {
    {TpchTable::Part, "part"},         {TpchTable::PartSupp, "partsupp"}, {TpchTable::Supplier, "supplier"},
    {TpchTable::Customer, "customer"}, {TpchTable::Orders, "orders"},     {TpchTable::LineItem, "lineitem"},
    {TpchTable::Nation, "nation"},     {TpchTable::Region, "region"}};

TpchDbGenerator::TpchDbGenerator(float scale_factor, uint32_t chunk_size)
    : _scale_factor(scale_factor), _chunk_size(chunk_size) {}

std::unordered_map<TpchTable, std::shared_ptr<Table>> TpchDbGenerator::generate() {
  TableBuilder customer_builder{_chunk_size, customer_column_types, customer_column_names, ChunkUseMvcc::Yes};
  TableBuilder order_builder{_chunk_size, order_column_types, order_column_names, ChunkUseMvcc::Yes};
  TableBuilder lineitem_builder{_chunk_size, lineitem_column_types, lineitem_column_names, ChunkUseMvcc::Yes};
  TableBuilder part_builder{_chunk_size, part_column_types, part_column_names, ChunkUseMvcc::Yes};
  TableBuilder partsupp_builder{_chunk_size, partsupp_column_types, partsupp_column_names, ChunkUseMvcc::Yes};
  TableBuilder supplier_builder{_chunk_size, supplier_column_types, supplier_column_names, ChunkUseMvcc::Yes};
  TableBuilder nation_builder{_chunk_size, nation_column_types, nation_column_names, ChunkUseMvcc::Yes};
  TableBuilder region_builder{_chunk_size, region_column_types, region_column_names, ChunkUseMvcc::Yes};

  dbgen_reset_seeds();

  /**
   * CUSTOMER
   */
  const auto customer_count = static_cast<size_t>(tdefs[CUST].base * _scale_factor);

  for (size_t row_idx = 0; row_idx < customer_count; row_idx++) {
    auto customer = _call_dbgen_mk<customer_t>(row_idx + 1, mk_cust, TpchTable::Customer);
    customer_builder.append_row(customer.custkey, customer.name, customer.address, customer.nation_code, customer.phone,
                                _convert_money(customer.acctbal), customer.mktsegment, customer.comment);
  }

  /**
   * ORDER and LINEITEM
   */
  const auto order_count = static_cast<size_t>(tdefs[ORDER].base * _scale_factor);

  for (size_t order_idx = 0; order_idx < order_count; ++order_idx) {
    const auto order = _call_dbgen_mk<order_t>(order_idx + 1, mk_order, TpchTable::Orders, 0l, _scale_factor);

    order_builder.append_row(order.okey, order.custkey, std::string(1, order.orderstatus),
                             _convert_money(order.totalprice), order.odate, order.opriority, order.clerk,
                             order.spriority, order.comment);

    for (auto line_idx = 0; line_idx < order.lines; ++line_idx) {
      const auto& lineitem = order.l[line_idx];

      lineitem_builder.append_row(lineitem.okey, lineitem.partkey, lineitem.suppkey, lineitem.lcnt, lineitem.quantity,
                                  _convert_money(lineitem.eprice), _convert_money(lineitem.discount),
                                  _convert_money(lineitem.tax), std::string(1, lineitem.rflag[0]),
                                  std::string(1, lineitem.lstatus[0]), lineitem.sdate, lineitem.cdate, lineitem.rdate,
                                  lineitem.shipinstruct, lineitem.shipmode, lineitem.comment);
    }
  }

  /**
   * PART and PARTSUPP
   */
  const auto part_count = static_cast<size_t>(tdefs[PART].base * _scale_factor);

  for (size_t part_idx = 0; part_idx < part_count; ++part_idx) {
    const auto part = _call_dbgen_mk<part_t>(part_idx + 1, mk_part, TpchTable::Part, _scale_factor);

    part_builder.append_row(part.partkey, part.name, part.mfgr, part.brand, part.type, part.size, part.container,
                            _convert_money(part.retailprice), part.comment);

    for (const auto& partsupp : part.s) {
      partsupp_builder.append_row(partsupp.partkey, partsupp.suppkey, partsupp.qty, _convert_money(partsupp.scost),
                                  partsupp.comment);
    }
  }

  /**
   * SUPPLIER
   */
  const auto supplier_count = static_cast<size_t>(tdefs[SUPP].base * _scale_factor);

  for (size_t supplier_idx = 0; supplier_idx < supplier_count; ++supplier_idx) {
    const auto supplier = _call_dbgen_mk<supplier_t>(supplier_idx + 1, mk_supp, TpchTable::Supplier);

    supplier_builder.append_row(supplier.suppkey, supplier.name, supplier.address, supplier.nation_code, supplier.phone,
                                _convert_money(supplier.acctbal), supplier.comment);
  }

  /**
   * NATION
   */
  const auto nation_count = static_cast<size_t>(tdefs[NATION].base);

  for (size_t nation_idx = 0; nation_idx < nation_count; ++nation_idx) {
    const auto nation = _call_dbgen_mk<code_t>(nation_idx + 1, mk_nation, TpchTable::Nation);
    nation_builder.append_row(nation.code, nation.text, nation.join, nation.comment);
  }

  /**
   * REGION
   */
  const auto region_count = static_cast<size_t>(tdefs[REGION].base);

  for (size_t region_idx = 0; region_idx < region_count; ++region_idx) {
    const auto region = _call_dbgen_mk<code_t>(region_idx + 1, mk_region, TpchTable::Region);
    region_builder.append_row(region.code, region.text, region.comment);
  }

  /**
   * Clean up dbgen every time we finish table generation to avoid memory leaks in dbgen
   */
  _dbgen_cleanup();

  return {
      {TpchTable::Customer, customer_builder.finish_table()}, {TpchTable::Orders, order_builder.finish_table()},
      {TpchTable::LineItem, lineitem_builder.finish_table()}, {TpchTable::Part, part_builder.finish_table()},
      {TpchTable::PartSupp, partsupp_builder.finish_table()}, {TpchTable::Supplier, supplier_builder.finish_table()},
      {TpchTable::Nation, nation_builder.finish_table()},     {TpchTable::Region, region_builder.finish_table()}};
}

void TpchDbGenerator::generate_and_store() {
  const auto tables = generate();

  for (auto& table : tables) {
    StorageManager::get().add_table(tpch_table_names.at(table.first), table.second);
  }
}

}  // namespace opossum
