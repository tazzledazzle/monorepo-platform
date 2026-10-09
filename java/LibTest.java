import static org.junit.Assert.assertEquals;

import com.example.Lib;
import org.junit.Test;

public class LibTest {
  @Test
  public void greetsByName() {
    assertEquals("Hello, Bazel!", Lib.greet("Bazel"));
  }

  @Test
  public void fallsBackToWorld() {
    assertEquals("Hello, world!", Lib.greet(""));
    assertEquals("Hello, world!", Lib.greet(null));
  }
}
