"""JVM helpers shared by the Java and Kotlin wrappers."""

# Source roots, longest first. A BUILD file at `server/catalog` has its sources under
# `src/test/java/...`, so the class's package is everything after one of these markers.
_SOURCE_ROOTS = [
    "src/test/kotlin/",
    "src/test/java/",
    "src/main/kotlin/",
    "src/main/java/",
    "javatests/",
    "java/",
]

def derive_test_class(*, test_class, name, srcs):
    """Returns the fully qualified test class a JVM test should run.

    An explicit `test_class` always wins. Otherwise the class is the one whose file
    is named after the target (target `CatalogTest` -> `CatalogTest.java`/`.kt`), with
    its package taken from the path below the source root:

        server/catalog/src/test/java/analytics/catalog/CatalogTest.java
          -> analytics.catalog.CatalogTest

    Fails at load time, naming the target, when no source matches, rather than letting
    the runner discover a wrong class name at test time.
    """
    if test_class:
        return test_class

    for src in srcs:
        path = (src.package + "/" if src.package else "") + src.name
        for ext in (".java", ".kt"):
            if not path.endswith("/" + name + ext):
                continue
            for root in _SOURCE_ROOTS:
                idx = path.find("/" + root)
                if idx >= 0:
                    below_root = path[idx + 1 + len(root):-len(ext)]
                    return below_root.replace("/", ".")
                if path.startswith(root):
                    return path[len(root):-len(ext)].replace("/", ".")

    fail("Cannot derive test_class for '{}': no source named '{}.java' or '{}.kt' under a " +
         "recognised source root ({}). Set test_class explicitly.".format(
             name,
             name,
             name,
             ", ".join(_SOURCE_ROOTS),
         ))
