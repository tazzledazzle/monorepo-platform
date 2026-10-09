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
    "team": attr.string(
        mandatory = True,
        configurable = False,
        doc = "Owning team, e.g. 'engine'. Becomes the tag `team:<name>`.",
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
    """Returns `tags` plus the `team:<team>` tag. `tags` may be None."""
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
