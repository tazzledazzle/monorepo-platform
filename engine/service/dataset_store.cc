#include "engine/service/dataset_store.h"

#include <utility>

namespace analytics::engine {

DatasetStore::Snapshot DatasetStore::Get(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = datasets_.find(name);
  return it == datasets_.end() ? nullptr : it->second;
}

std::optional<std::vector<ColumnSpec>> DatasetStore::SchemaOf(const std::string& name) const {
  Snapshot s = Get(name);
  if (!s) return std::nullopt;
  return s->schema();
}

Result<size_t> DatasetStore::Append(const std::string& name, const Table& staged) {
  // Writers are serialized by the lock; the copy keeps readers on the old
  // snapshot until the swap.
  std::lock_guard<std::mutex> lock(mu_);
  auto it = datasets_.find(name);
  if (it == datasets_.end()) {
    auto fresh = std::make_shared<const Table>(staged);
    const size_t rows = fresh->row_count();
    datasets_.emplace(name, std::move(fresh));
    return rows;
  }
  if (it->second->schema() != staged.schema()) {
    return Error{ErrorCode::kSchemaMismatch,
                 "schema of dataset '" + name + "' differs from the stored schema"};
  }
  Table next = *it->second;
  std::vector<ColumnValues> batch;
  batch.reserve(staged.column_count());
  for (const Column& c : staged.columns()) batch.push_back(c.values());
  if (auto err = next.AppendColumns(std::move(batch))) return *err;
  const size_t rows = next.row_count();
  it->second = std::make_shared<const Table>(std::move(next));
  return rows;
}

}  // namespace analytics::engine
