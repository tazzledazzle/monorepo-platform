"""Cross-language policy shared by every analytics_* wrapper macro.

Policy that lives here (per-target, so it is the platform team's to change):
  * every target declares its owning `team`, which becomes a `team:<name>` tag
    (M3 uses these tags in `bazel query`);
  * tests default to `size = "small"`;
  * visibility is whatever the BUILD file says; the macro never widens it, so
    targets stay private unless a team opts in.

Repo-wide toolchain flags (language levels, sanitizers, cache) do NOT belong
here; they live in .bazelrc so they take part in the toolchain's cache keys.
"""

TEAM_ATTRS = {
    # Not `mandatory = True` on purpose: on Bazel 9.3.0 a missing mandatory attribute of a symbolic
    # macro is reported as an ERROR but `bazel build`/`bazel test` still exit 0 (only `bazel query`
    # fails). `team_tags` therefore enforces it with fail(), which stops every command.
    "team": attr.string(
        default = "",
        configurable = False,
        doc = "Required. Owning team, e.g. 'engine'. Becomes the tag `team:<name>`.",
    ),
}

TEST_ATTRS = {
    "size": attr.string(
        default = "small",
        configurable = False,
        values = ["small", "medium", "large", "enormous"],
        doc = "Test size. Defaults to 'small' so slow tests must opt in visibly.",
    ),
    "timeout": attr.string(configurable = False),
    "args": attr.string_list(),
    "env": attr.string_dict(),
    "flaky": attr.bool(configurable = False),
    "shard_count": attr.int(configurable = False, default = -1),
}

def team_tags(tags, team):
    """Returns `tags` plus the `team:<team>` tag. `tags` may be None.

    Fails at load time, naming the attribute, when `team` is empty. Every analytics_* macro calls
    this, so this is the single place the "team is required" rule is enforced.
    """
    if not team:
        fail("missing value for mandatory attribute 'team' (package //{}): every analytics_* target must name its owning team".format(native.package_name()))
    return list(tags or []) + ["team:" + team]

def present(attrs):
    """Drops attributes the BUILD file left unset (None), so the wrapped rule's own defaults apply."""
    return {k: v for k, v in attrs.items() if v != None}

def prepend(defaults, value):
    """Returns `defaults + value`; works whether `value` is a list or a select()."""
    return defaults + value

def test_attrs(size, timeout, args, env, flaky, shard_count):
    """Packs the shared test attributes into kwargs for the wrapped test rule."""
    return present({
        "size": size,
        "timeout": timeout if timeout else None,
        "args": args if args else None,
        "env": env if env else None,
        "flaky": flaky if flaky else None,
        "shard_count": shard_count if shard_count > 0 else None,
    })
