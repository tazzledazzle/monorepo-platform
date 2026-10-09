package analytics.catalog;

import com.example.analytics.v1.Column;
import com.example.analytics.v1.ColumnType;
import com.example.analytics.v1.DatasetInfo;
import com.example.analytics.v1.Schema;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;

/**
 * In-memory dataset registry: dataset name to schema. Thread-safe; {@link #seeded()} is
 * pre-loaded with the {@code sales} dataset (must match the sales CSV used by the ingest agent).
 */
public final class Catalog {
  public static final String SALES = "sales";

  private final Map<String, Schema> datasets = new ConcurrentHashMap<>();

  /** Returns an empty catalog. */
  public Catalog() {}

  /** Returns a catalog seeded with the {@code sales} dataset. */
  public static Catalog seeded() {
    Catalog catalog = new Catalog();
    catalog.register(
        SALES,
        Schema.newBuilder()
            .addColumns(column("region", ColumnType.COLUMN_TYPE_STRING))
            .addColumns(column("product", ColumnType.COLUMN_TYPE_STRING))
            .addColumns(column("units", ColumnType.COLUMN_TYPE_INT64))
            .addColumns(column("revenue", ColumnType.COLUMN_TYPE_DOUBLE))
            .build());
    return catalog;
  }

  /** Registers or replaces a dataset. */
  public void register(String name, Schema schema) {
    if (name.isEmpty()) {
      throw new IllegalArgumentException("dataset name must be non-empty");
    }
    datasets.put(name, schema);
  }

  /** Returns the schema of the dataset, or empty if it is not registered. */
  public Optional<Schema> lookup(String name) {
    return Optional.ofNullable(datasets.get(name));
  }

  /** Returns all datasets sorted by name. */
  public List<DatasetInfo> list() {
    List<DatasetInfo> result = new ArrayList<>();
    for (Map.Entry<String, Schema> e : datasets.entrySet()) {
      result.add(DatasetInfo.newBuilder().setName(e.getKey()).setSchema(e.getValue()).build());
    }
    result.sort(Comparator.comparing(DatasetInfo::getName));
    return result;
  }

  private static Column column(String name, ColumnType type) {
    return Column.newBuilder().setName(name).setType(type).build();
  }
}
