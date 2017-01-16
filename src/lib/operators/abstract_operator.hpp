#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../common.hpp"
#include "../storage/table.hpp"

namespace opossum {

// AbstractOperator is the abstract super class for all operators.
// All operators have up to two input tables and one output table.
// Their lifecycle has three phases:
// 1. The operator is constructed. Previous operators are not guaranteed to have already executed, so operators must not
// call get_output in their execute method
// 2. The execute method is called from the outside (usually by the scheduler). This is where the heavy lifting is done.
// By now, the input operators have already executed.
// 3. The consumer (usually another operator) calls get_output. This should be very cheap. It is only guaranteed to
// succeed if execute was called before. Otherwise, a nullptr or an empty table could be returned.
//
// Operators shall not be executed twice.
class AbstractOperator {
 public:
  AbstractOperator(const std::shared_ptr<const AbstractOperator> left = nullptr,
                   const std::shared_ptr<const AbstractOperator> right = nullptr);

  // copying a operator is not allowed
  AbstractOperator(AbstractOperator const &) = delete;
  AbstractOperator &operator=(const AbstractOperator &) = delete;

  // we need to explicitly set the move constructor to default when
  // we overwrite the copy constructor
  AbstractOperator(AbstractOperator &&) = default;
  AbstractOperator &operator=(AbstractOperator &&) = default;

  virtual ~AbstractOperator() = default;

  // abstract method to actually execute the operator
  // execute and get_output are split into two methods to allow for easier
  // asynchronous execution
  virtual void execute() = 0;

  // returns the result of the operator
  virtual std::shared_ptr<const Table> get_output() const = 0;

  virtual const std::string name() const = 0;

  // returns the number of input tables, range of values is [0, 2]
  virtual uint8_t num_in_tables() const = 0;

  // returns the number of output tables, range of values is [0, 1]
  virtual uint8_t num_out_tables() const = 0;

 protected:
  // Shared pointers to input tables, can be nullptr. Using shared pointers makes sure that tables do not disappear
  // during processing
  const std::shared_ptr<const Table> _input_left, _input_right;

  // Some operators need an internal implementation class, mostly in cases where
  // their execute method depends on a template parameter. An example for this is
  // found in table_scan.hpp.
  class AbstractOperatorImpl {
   public:
    virtual void execute() = 0;
    virtual std::shared_ptr<Table> get_output() const = 0;
    virtual ~AbstractOperatorImpl() = default;
  };
};

}  // namespace opossum
