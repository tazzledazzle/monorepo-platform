"""One generic analysistest used for every attribute-level check of the analytics_* macros.

The subject is a target created by a macro under test. The test inspects the *wrapped*
rule's resolved attributes (`ctx.rule.attr`), which is exactly what the macro controls.
Only attribute-level facts are checked; load-time failures (missing `team`) cannot be
analysistests and are covered by the negative tests.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("//tools/build_defs:package.bzl", "AnalyticsPackageInfo")
load("@rules_kotlin//kotlin/internal:opts.bzl", "KotlincOptions")

_INSPECTED = ["tags", "copts", "javacopts", "deps", "size", "test_class", "main", "main_class"]

AttrInfo = provider(doc = "Snapshot of the wrapped rule's resolved attributes.", fields = ["values", "kotlinc_warn"])

def _plain(value):
    """Targets -> label strings, so attribute values compare as plain data."""
    if type(value) == "Target":
        return str(value.label)
    if type(value) == "list":
        return [str(v.label) if type(v) == "Target" else v for v in value]
    return value

def _inspect_impl(target, ctx):
    values = {n: _plain(getattr(ctx.rule.attr, n)) for n in _INSPECTED if hasattr(ctx.rule.attr, n)}
    warn = None
    opts = getattr(ctx.rule.attr, "kotlinc_opts", None)
    if opts and KotlincOptions in opts:
        warn = opts[KotlincOptions].warn
    return [AttrInfo(values = values, kotlinc_warn = warn)]

# analysistest rules cannot read `ctx.rule`; an aspect on the subject can.
_inspect = aspect(implementation = _inspect_impl)

def _impl(ctx):
    env = analysistest.begin(ctx)
    subject = analysistest.target_under_test(env)
    info = subject[AttrInfo]
    values = info.values

    tags = values.get("tags", [])
    for tag in ctx.attr.expect_tags:
        asserts.true(env, tag in tags, "tag '%s' missing from %s" % (tag, tags))

    for attr_name, wanted in ctx.attr.expect_list_attrs.items():
        have = values.get(attr_name, [])
        for item in wanted.split(","):
            asserts.true(env, item in have, "%s: '%s' missing from %s" % (attr_name, item, have))

    deps = values.get("deps", [])
    for dep in ctx.attr.expect_deps:
        asserts.true(env, str(dep.label) in deps, "dep %s missing from %s" % (dep.label, deps))

    for attr_name, wanted in ctx.attr.expect_values.items():
        asserts.equals(env, wanted, str(values.get(attr_name)))

    if ctx.attr.expect_main:
        asserts.equals(env, str(ctx.attr.expect_main.label), values.get("main"))

    if ctx.attr.expect_kotlinc_warn:
        asserts.equals(env, ctx.attr.expect_kotlinc_warn, info.kotlinc_warn)
    return analysistest.end(env)

attr_test = analysistest.make(
    _impl,
    extra_target_under_test_aspects = [_inspect],
    attrs = {
        "expect_tags": attr.string_list(),
        "expect_no_tags": attr.string_list(),
        # attr name -> comma-separated items that must all be in that list attribute
        "expect_list_attrs": attr.string_dict(),
        "expect_deps": attr.label_list(),
        # attr name -> str() of the expected value (e.g. size "small")
        "expect_values": attr.string_dict(),
        "expect_main": attr.label(allow_single_file = True),
        "expect_kotlinc_warn": attr.string(),
    },
)

def _package_impl(ctx):
    env = analysistest.begin(ctx)
    subject = analysistest.target_under_test(env)
    info = subject[AnalyticsPackageInfo]
    entries = info.files_by_path.to_list()
    asserts.equals(env, sorted(ctx.attr.expect_paths), sorted([e.path for e in entries]))
    labels = {e.path: e.label for e in entries}
    for path, label in ctx.attr.expect_labels.items():
        asserts.equals(env, label, labels.get(path))
    outs = sorted([f.basename for f in subject[DefaultInfo].files.to_list()])
    asserts.equals(env, ctx.attr.expect_outputs, outs)
    actions = [a.mnemonic for a in analysistest.target_actions(env)]
    asserts.true(env, "AnalyticsPackage" in actions, "no AnalyticsPackage action in %s" % actions)
    return analysistest.end(env)

package_test = analysistest.make(
    _package_impl,
    attrs = {
        "expect_paths": attr.string_list(),
        "expect_labels": attr.string_dict(),
        "expect_outputs": attr.string_list(),
    },
)

def assert_rules_exist(name, expect):
    """Load-time check of rules the analysistest cannot reference.

    `expect` maps a target name to {"kind": ..., attr: wanted, ...}. A list attribute
    must contain an entry ending with each wanted string; a scalar must be equal.
    Used for the gRPC service's `_test`, which is private to the macro, so an
    analysistest dependency edge is not allowed to point at it. Failing at load time
    fails every test in this package, which is the desired loudness.
    """
    for target, wanted in expect.items():
        rule = native.existing_rule(target)
        if rule == None:
            fail("%s: expected target '%s' to exist" % (name, target))
        for attr_name, want in wanted.items():
            have = rule.get(attr_name)
            if type(want) == "list":
                for w in want:
                    if not [h for h in have if str(h).endswith(w)]:
                        fail("%s: %s.%s is %s, missing '%s'" % (name, target, attr_name, have, w))
            elif have != want:
                fail("%s: %s.%s is %r, expected %r" % (name, target, attr_name, have, want))
