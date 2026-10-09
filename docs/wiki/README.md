# Wiki source

These files are the source of the project's GitHub wiki. GitHub stores a wiki in a separate git repository, `<repo>.wiki.git`, so the files live here for review and are copied over by hand.

Inside the wiki, pages link to each other with `[text](Page-Name)`. Those links work on the wiki but not when you browse this directory on GitHub. Links to repository files appear as code paths (for example `docs/adding-a-target.md`) because a wiki cannot link to them relatively.

## Publish

Enable the wiki in the repository settings and create the first page once in the web UI (GitHub creates the wiki repo on the first save). Then:

```sh
git clone https://github.com/<owner>/<repo>.wiki.git /tmp/wiki
cp docs/wiki/*.md /tmp/wiki/
rm /tmp/wiki/README.md        # this file is repo-only instructions
cd /tmp/wiki
git add -A
git commit -m "Update wiki from docs/wiki"
git push
```

Replace `<owner>/<repo>`. The wiki's default branch is usually `master`; `git push` uses whatever the clone checked out.

## Update

Edit the files in `docs/wiki/`, commit them to the main repo, then repeat the copy and push. Do not edit the wiki in the web UI, or the next copy overwrites those edits.

## Pages

`Home`, `_Sidebar`, `Getting-Started`, `Architecture`, `Platform-Macros`, `Packaging-Rule`, `Adding-a-Target`, `Roadmap`, `Findings-and-Gotchas`, `Glossary`.
