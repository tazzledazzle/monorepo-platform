package sample;
import static org.junit.Assert.assertEquals;
import org.junit.Test;
public class GreeterTest { @Test public void greets() { assertEquals("Hello, x", Greeter.greet("x")); } }
