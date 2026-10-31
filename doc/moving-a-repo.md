# Moving a repository into the tree

`components/library/pth` was moved this way and is the example to follow.

1. Pick the category from the table in the README, and the directory name:
   `<name>`, or `<name>-<version>` once more than one version is kept.
2. Bring the repository in with its history. Its working copy must be clean
   and pushed. In amix-userland:

       git subtree add --prefix=components/<category>/<name> <repo URL> main

3. Add what the component needs:
   - a `Makefile` from `templates/Makefile`,
   - `<name>.license`, the upstream licence,
   - `patches/` for the AMIX changes, `files/` for what is ours.
4. Check that `make build` works in the component's directory.
5. Push. In the old repository, point the README to the new place, then
   archive or delete the old repository on GitHub.

`git log -- components/<category>/<name>` shows the component's commits, and
`git log --follow` shows a file's commits from before the move.
