#ifndef ENGINE_SERVICE_DATASET_STORE_H_
#define ENGINE_SERVICE_DATASET_STORE_H_

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "engine/core/status.h"
#include "engine/core/table.h"

namespace analytics::engine {

// Thread-safe map of dataset name to table.
//
// Tables are published as immutable snapshots (shared_ptr<const Table>):
// readers take a snapshot under the lock and then run lock-free, and a writer
// builds the replacement outside readers' view and swaps it in. A query
// therefore never sees a half-applied load.
class DatasetStore {
 public:
  using Snapshot = std::shared_ptr<const Table>;

  // Null when the dataset does not exist.
  Snapshot Get(const std::string& name) const;

  // Schema of an existing dataset, if any (used to fail a load early).
  std::optional<std::vector<ColumnSpec>> SchemaOf(const std::string& name) const;

  // Appends `staged` (already validated against its own schema) to `name`,
  // creating the dataset if absent. If it exists the schemas must be equal
  // (kSchemaMismatch). Atomic: on error nothing changes. Returns the dataset's
  // new total row count.
  Result<size_t> Append(const std::string& name, const Table& staged);

 private:
  mutable std::mutex mu_;
  std::map<std::string, Snapshot> datasets_;
};

}  // namespace analytics::engine

#endif  // ENGINE_SERVICE_DATASET_STORE_H_
