/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stx/io/BufferedOutputStream.h>
#include <stx/io/fileutil.h>
#include <csql/tasks/groupby.h>
#include <csql/runtime/emptytable.h>

namespace csql {

GroupByExpression::GroupByExpression(
    Transaction* ctx,
    const Vector<String>& column_names,
    Vector<ValueExpression> select_expressions) :
    ctx_(ctx),
    column_names_(column_names),
    select_exprs_(std::move(select_expressions)) {}

void GroupByExpression::executeRemote(
    ExecutionContext* context,
    OutputStream* os) {
  HashMap<String, Vector<VM::Instance>> groups;
  ScratchMemory scratch;

  try {
    accumulate(&groups, &scratch, context);
  } catch (...) {
    freeResult(&groups);
    throw;
  }

  encode(&groups, os);
  freeResult(&groups);
}

//void GroupByExpression::execute(
//    ExecutionContext* context,
//    Function<bool (int argc, const SValue* argv)> fn) {
//  HashMap<String, Vector<VM::Instance >> groups;
//  ScratchMemory scratch;
//
//  try {
//    accumulate(&groups, &scratch, context);
//    getResult(&groups, fn);
//  } catch (...) {
//    freeResult(&groups);
//    throw;
//  }
//
//  freeResult(&groups);
//  context->incrNumSubtasksCompleted(1);
//}

GroupBy::GroupBy(
    Transaction* ctx,
    ScopedPtr<Task> source,
    const Vector<String>& column_names,
    Vector<ValueExpression> select_expressions,
    Vector<ValueExpression> group_expressions,
    SHA1Hash qtree_fingerprint) :
    GroupByExpression(ctx, column_names, std::move(select_expressions)),
    source_(std::move(source)),
    group_exprs_(std::move(group_expressions)),
    qtree_fingerprint_(qtree_fingerprint) {}

void GroupBy::accumulate(
    HashMap<String, Vector<VM::Instance >>* groups,
    ScratchMemory* scratch,
    ExecutionContext* context) {
  auto cache_key = cacheKey();
  String cache_filename;
  bool from_cache = false;
  auto cachedir = context->cacheDir();
  if (!cache_key.isEmpty() && !cachedir.isEmpty()) {
    cache_filename = FileUtil::joinPaths(
        cachedir.get(),
        cache_key.get().toString() + ".qcache");

    if (FileUtil::exists(cache_filename)) {
      auto fis = FileInputStream::openFile(cache_filename);
      from_cache = decode(groups, scratch, fis.get());
    }
  }

  if (!from_cache) {
    //source_->execute(
    //    context,
    //    std::bind(
    //        &GroupBy::nextRow,
    //        this,
    //        groups,
    //        scratch,
    //        std::placeholders::_1,
    //        std::placeholders::_2));
  }

  if (!from_cache && !cache_key.isEmpty() && !cachedir.isEmpty()) {
    BufferedOutputStream fos(
        FileOutputStream::fromFile(
            File::openFile(
                cache_filename + "~",
                File::O_CREATEOROPEN | File::O_WRITE | File::O_TRUNCATE)));

    encode(groups, &fos);
    FileUtil::mv(cache_filename + "~", cache_filename);
  }
}

void GroupByExpression::getResult(
    const HashMap<String, Vector<VM::Instance >>* groups,
    Function<bool (int argc, const SValue* argv)> fn) {
  Vector<SValue> out_row(select_exprs_.size(), SValue{});
  for (auto& group : *groups) {
    for (size_t i = 0; i < select_exprs_.size(); ++i) {
      VM::result(ctx_, select_exprs_[i].program(), &group.second[i], &out_row[i]);
    }

    if (!fn(out_row.size(), out_row.data())) {
      return;
    }
  }
}

void GroupByExpression::freeResult(
    HashMap<String, Vector<VM::Instance >>* groups) {
  for (auto& group : (*groups)) {
    for (size_t i = 0; i < select_exprs_.size(); ++i) {
      VM::freeInstance(ctx_, select_exprs_[i].program(), &group.second[i]);
    }
  }
}

void GroupByExpression::mergeResult(
    const HashMap<String, Vector<VM::Instance >>* src,
    HashMap<String, Vector<VM::Instance >>* dst,
    ScratchMemory* scratch) {
  for (const auto& src_group : *src) {
    auto& dst_group = (*dst)[src_group.first];
    if (dst_group.size() == 0) {
      for (const auto& e : select_exprs_) {
        dst_group.emplace_back(VM::allocInstance(ctx_, e.program(), scratch));
      }
    }

    for (size_t i = 0; i < select_exprs_.size(); ++i) {
      VM::merge(
          ctx_,
          select_exprs_[i].program(),
          &dst_group[i],
          &src_group.second[i]);
    }
  }
}

bool GroupBy::nextRow(
    HashMap<String, Vector<VM::Instance >>* groups,
    ScratchMemory* scratch,
    int row_len, const SValue* row) {
  Vector<SValue> gkey(group_exprs_.size(), SValue{});
  for (size_t i = 0; i < group_exprs_.size(); ++i) {
    VM::evaluate(ctx_, group_exprs_[i].program(), row_len, row, &gkey[i]);
  }

  auto group_key = SValue::makeUniqueKey(gkey.data(), gkey.size());
  auto& group = (*groups)[group_key];
  if (group.size() == 0) {
    for (const auto& e : select_exprs_) {
      group.emplace_back(VM::allocInstance(ctx_, e.program(), scratch));
    }
  }

  for (size_t i = 0; i < select_exprs_.size(); ++i) {
    VM::accumulate(ctx_, select_exprs_[i].program(), &group[i], row_len, row);
  }

  return true;
}

Vector<String> GroupByExpression::columnNames() const {
  return column_names_;
}

size_t GroupByExpression::numColumns() const {
  return column_names_.size();
}

void GroupByExpression::encode(
    const HashMap<String, Vector<VM::Instance >>* groups,
    OutputStream* os) const {
  os->appendVarUInt(groups->size());
  os->appendVarUInt(select_exprs_.size());

  for (auto& group : *groups) {
    os->appendLenencString(group.first);

    for (size_t i = 0; i < select_exprs_.size(); ++i) {
      VM::saveState(ctx_, select_exprs_[i].program(), &group.second[i], os);
    }
  }
}

bool GroupByExpression::decode(
    HashMap<String, Vector<VM::Instance >>* groups,
    ScratchMemory* scratch,
    InputStream* is) const {
  auto ngroups = is->readVarUInt();
  auto nexprs = is->readVarUInt();

  if (select_exprs_.size() != nexprs) {
    return false;
  }

  for (size_t j = 0; j < ngroups; ++j) {
    auto group_key = is->readLenencString();

    auto& group = (*groups)[group_key];
    for (size_t i = 0; i < select_exprs_.size(); ++i) {
      const auto& e = select_exprs_[i];
      group.emplace_back(VM::allocInstance(ctx_, e.program(), scratch));
      VM::loadState(ctx_, e.program(), &group[i], is);
    }
  }

  return true;
}

Option<SHA1Hash> GroupBy::cacheKey() const {
  auto source_key = source_->cacheKey();
  if (source_key.isEmpty()) {
    return None<SHA1Hash>();
  }

  return Some(
      SHA1::compute(
          StringUtil::format(
              "$0~$1",
              source_key.get().toString(),
              qtree_fingerprint_.toString())));
}

RemoteGroupBy::RemoteGroupBy(
    Transaction* ctx,
    const Vector<String>& column_names,
    Vector<ValueExpression> select_expressions,
    const RemoteAggregateParams& params,
    RemoteExecuteFn execute_fn) :
    GroupByExpression(ctx, column_names, std::move(select_expressions)),
    params_(params),
    execute_fn_(execute_fn) {}

void RemoteGroupBy::accumulate(
    HashMap<String, Vector<VM::Instance >>* groups,
    ScratchMemory* scratch,
    ExecutionContext* context) {
  auto result = execute_fn_(params_);
  context->incrNumSubtasksCompleted(1);
  if (!decode(groups, scratch, result.get())) {
    RAISE(kRuntimeError, "RemoteGroupBy failed");
  }
}

GroupByMerge::GroupByMerge(
    Vector<ScopedPtr<GroupByExpression>> sources) :
    sources_(std::move(sources)) {
  if (sources_.size() == 0) {
    RAISE(kRuntimeError, "GROUP MERGE must have at least one source table");
  }

  size_t ncols = size_t(-1);

  for (const auto& cur : sources_) {
    if (dynamic_cast<EmptyTable*>(cur.get())) {
      continue;
    }

    auto tcols = cur->numColumns();
    if (ncols != size_t(-1) && ncols != tcols) {
      RAISE(
          kRuntimeError,
          "GROUP MERGE tables return different number of columns");
    }

    ncols = tcols;
  }
}

//void GroupByMerge::execute(
//    ExecutionContext* context,
//    Function<bool (int argc, const SValue* argv)> fn) {
//  HashMap<String, Vector<VM::Instance >> groups;
//  ScratchMemory scratch;
//  std::mutex mutex;
//  std::condition_variable cv;
//  auto sem = sources_.size();
//  Vector<String> errors;
//
//  for (auto& s : sources_) {
//    auto source = s.get();
//
//    context->runAsync([
//        context,
//        &scratch,
//        &groups,
//        source,
//        &mutex,
//        &cv,
//        &sem,
//        &errors] {
//      HashMap<String, Vector<VM::Instance >> tgroups;
//      ScratchMemory tscratch;
//
//      String terror;
//      try {
//        source->accumulate(&tgroups, &tscratch, context);
//      } catch (const StandardException& e) {
//        terror = e.what();
//      }
//
//      std::unique_lock<std::mutex> lk(mutex);
//      if (terror.empty()) {
//        try {
//          source->mergeResult(&tgroups, &groups, &scratch);
//        } catch (const StandardException& e) {
//          errors.emplace_back(e.what());
//        }
//      } else {
//        errors.emplace_back(terror);
//      }
//
//      --sem;
//      lk.unlock();
//      cv.notify_all();
//    });
//  }
//
//  std::unique_lock<std::mutex> lk(mutex);
//  while (sem > 0) {
//    cv.wait(lk);
//  }
//
//  if (!errors.empty()) {
//    sources_[0]->freeResult(&groups);
//    RAISE(
//        kRuntimeError,
//        StringUtil::join(errors, "; "));
//  }
//
//  try {
//    sources_[0]->getResult(&groups, fn);
//  } catch (...) {
//    sources_[0]->freeResult(&groups);
//    throw;
//  }
//
//  sources_[0]->freeResult(&groups);
//}

Vector<String> GroupByMerge::columnNames() const {
  return sources_[0]->columnNames();
}

size_t GroupByMerge::numColumns() const {
  return sources_[0]->numColumns();
}

}
