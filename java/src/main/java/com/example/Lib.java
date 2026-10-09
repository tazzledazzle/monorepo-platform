package com.example;

/** Small library used to demonstrate java_library + java_test under Bazel. */
public final class Lib {
  private Lib() {}

  /** Returns a greeting for {@code name}, falling back to "world" when blank. */
  public static String greet(String name) {
    String who = (name == null || name.isBlank()) ? "world" : name;
    return "Hello, " + who + "!";
  }
}
