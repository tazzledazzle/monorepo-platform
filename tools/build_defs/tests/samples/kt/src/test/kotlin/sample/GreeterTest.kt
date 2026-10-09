package sample
import org.junit.Assert.assertEquals
import org.junit.Test
class GreeterTest { @Test fun greets() { assertEquals("Hello, x", Greeter.greet("x")) } }
