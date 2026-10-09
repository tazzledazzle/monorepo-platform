"""analytics_package: a hand-written rule that bundles binaries into a reproducible tar.

rules_pkg is the production choice; this rule exists to exercise providers, depsets,
runfiles and exec-configured tools.

Known limitation: Python (and other runfiles-carrying) binaries are packaged together
with their runfiles tree, each file at `<subdir>/<file.short_path>`. Runfiles
symlinks/root_symlinks, tree artifacts (directories) and dangling symlink artifacts
(e.g. rules_python's `_x.venv/bin/python3`, skipped with a warning) are not packaged.
"""

AnalyticsPackageInfo = provider(
    doc = "Files to install, keyed by install path. Enables nesting of packages.",
    fields = {
        "files_by_path": "depset of structs(path = install path, file = File, label = string source label)",
    },
)

def _entry_json(entry):
    return json.encode(struct(path = entry.path, src = entry.file.path, label = entry.label))

def _component_entries(target, subdir):
    label = str(target.label)
    info = target[DefaultInfo]
    files = [info.files]
    if info.default_runfiles:
        files.append(info.default_runfiles.files)
    entries = []
    for f in depset(transitive = files).to_list():
        # short_path is "../repo/..." for external files; keep it under the subdir.
        rel = f.short_path
        if rel.startswith("../"):
            rel = "external/" + rel[3:]
        entries.append(struct(path = subdir + "/" + rel, file = f, label = label))
    return entries

def _analytics_package_impl(ctx):
    direct = []
    for target, subdir in ctx.attr.components.items():
        direct.extend(_component_entries(target, subdir.strip("/")))

    files_by_path = depset(
        direct = direct,
        transitive = [d[AnalyticsPackageInfo].files_by_path for d in ctx.attr.deps],
    )
    entries = files_by_path.to_list()

    tar = ctx.actions.declare_file(ctx.label.name + ".tar")
    manifest = ctx.actions.declare_file(ctx.label.name + ".MANIFEST.json")

    # Spec is a param file: one JSON object per line. Duplicate install paths are
    # detected by the tool, which fails naming both source labels.
    spec_args = ctx.actions.args()
    spec_args.set_param_file_format("multiline")
    spec_args.add_all(files_by_path, map_each = _entry_json)
    spec = ctx.actions.declare_file(ctx.label.name + ".spec")
    ctx.actions.write(spec, spec_args)

    ctx.actions.run(
        executable = ctx.executable._make_tarball,
        arguments = [
            "--spec=" + spec.path,
            "--out_tar=" + tar.path,
            "--out_manifest=" + manifest.path,
        ],
        inputs = depset([spec], transitive = [depset([e.file for e in entries])]),
        outputs = [tar, manifest],
        mnemonic = "AnalyticsPackage",
        progress_message = "Packaging %{label}",
    )

    return [
        DefaultInfo(files = depset([tar, manifest])),
        AnalyticsPackageInfo(files_by_path = files_by_path),
    ]

analytics_package = rule(
    implementation = _analytics_package_impl,
    doc = "Bundles components (and nested analytics_package deps) into <name>.tar + <name>.MANIFEST.json.",
    attrs = {
        "components": attr.label_keyed_string_dict(
            doc = "label -> install subdirectory.",
            allow_files = True,
        ),
        "deps": attr.label_list(
            doc = "Other analytics_package targets to nest.",
            providers = [AnalyticsPackageInfo],
        ),
        "_make_tarball": attr.label(
            default = Label("//tools/build_defs/packaging:make_tarball"),
            executable = True,
            cfg = "exec",
        ),
    },
)
