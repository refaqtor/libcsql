/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <stx/stdtypes.h>
#include <csql/qtree/ShowTablesNode.h>
#include <csql/tasks/Task.h>
#include <csql/runtime/tablerepository.h>

namespace csql {

class ShowTables : public Task {
public:

  ShowTables(Transaction* txn, RowSinkFn output);

  void onInputsReady() override;

protected:
  Transaction* txn_;
  RowSinkFn output_;
};

class ShowTablesFactory : public TaskFactory {
public:

  RefPtr<Task> build(
      Transaction* txn,
      RowSinkFn output) const override;

};

}
