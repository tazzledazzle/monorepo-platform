package analytics.catalog;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.example.analytics.v1.ColumnType;
import com.example.analytics.v1.DatasetInfo;
import com.example.analytics.v1.Schema;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.Test;

public class CatalogTest {
  @Test
  public void salesIsSeeded() {
    Optional<Schema> schema = Catalog.seeded().lookup("sales");
    assertTrue(schema.isPresent());
    Schema s = schema.get();
    assertEquals(4, s.getColumnsCount());
    assertEquals("region", s.getColumns(0).getName());
    assertEquals(ColumnType.COLUMN_TYPE_STRING, s.getColumns(0).getType());
    assertEquals("product", s.getColumns(1).getName());
    assertEquals(ColumnType.COLUMN_TYPE_STRING, s.getColumns(1).getType());
    assertEquals("units", s.getColumns(2).getName());
    assertEquals(ColumnType.COLUMN_TYPE_INT64, s.getColumns(2).getType());
    assertEquals("revenue", s.getColumns(3).getName());
    assertEquals(ColumnType.COLUMN_TYPE_DOUBLE, s.getColumns(3).getType());
  }

  @Test
  public void unknownDatasetIsEmpty() {
    assertFalse(Catalog.seeded().lookup("nope").isPresent());
    assertFalse(Catalog.seeded().lookup("").isPresent());
    assertFalse(Catalog.seeded().lookup("Sales").isPresent());
  }

  @Test
  public void listIsSortedByName() {
    Catalog catalog = Catalog.seeded();
    catalog.register("aaa", Schema.getDefaultInstance());
    List<DatasetInfo> all = catalog.list();
    assertEquals(2, all.size());
    assertEquals("aaa", all.get(0).getName());
    assertEquals("sales", all.get(1).getName());
  }

  @Test(expected = IllegalArgumentException.class)
  public void emptyNameRejected() {
    new Catalog().register("", Schema.getDefaultInstance());
  }

  @Test
  public void concurrentRegisterAndLookup() throws Exception {
    Catalog catalog = Catalog.seeded();
    int threads = 8;
    ExecutorService pool = Executors.newFixedThreadPool(threads);
    CountDownLatch done = new CountDownLatch(threads);
    for (int t = 0; t < threads; t++) {
      final int id = t;
      pool.execute(
          () -> {
            for (int i = 0; i < 200; i++) {
              String name = "ds-" + id + "-" + i;
              catalog.register(name, Schema.getDefaultInstance());
              if (!catalog.lookup(name).isPresent() || !catalog.lookup("sales").isPresent()) {
                throw new AssertionError("lost entry " + name);
              }
            }
            done.countDown();
          });
    }
    assertTrue(done.await(30, TimeUnit.SECONDS));
    pool.shutdown();
    assertEquals(threads * 200 + 1, catalog.list().size());
  }
}
