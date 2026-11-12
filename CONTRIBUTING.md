# Contributing to Driftbox

## Workflow

1. Branch from `main` using a prefix: `feat/`, `fix/`, `docs/`, `ci/` (for example `feat/drift-profile`).
2. Open a pull request against `main`. PRs are **squash merged** and `main` keeps a linear history, so the PR title becomes the commit message.
3. Required CI checks must be green and the PR template checklist completed. The PR title is checked against Conventional Commits.
4. macOS is built in CI only on `main` and on pull requests labelled `ci:macos`; add the label when a change touches platform-specific code (see `docs/DEVELOPMENT.md`).

## Commit messages

[Conventional Commits](https://www.conventionalcommits.org/): `type(scope): summary`, with types `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `build`, `ci`, `chore`. Use `!` or a `BREAKING CHANGE:` footer for breaking changes. CI checks the PR title and the release notes are generated from these messages.

## Code style

C++20, with source comments written in Italian, formatted with `clang-format` (config in `.clang-format`; CI uses clang-format 18). Run it before pushing:

```
clang-format -i $(git ls-files '*.cpp' '*.h')
```

`clang-tidy` runs in CI on `src/` and any warning fails the job; see `docs/DEVELOPMENT.md` to run it locally. The audio thread must stay realtime safe: no allocation, locks or blocking calls in `processBlock`; allocation only in `prepare()` / `prepareToPlay`.

## Tests

```
cmake --preset win-vs
cmake --build --preset win-vs-release
ctest --preset win-vs-release
```

Add or update Catch2 tests with every behavioural change. See `docs/DEVELOPMENT.md` for pluginval and other tooling.

## Parameter IDs are immutable

Never rename, remove or re-type a parameter ID once defined in `src/params/ParamIDs.h`. Reasons:

- The CLAP format identifies parameters by a **hash of the ID**, so a rename breaks host automation and saved mappings.
- Hosts and saved projects/presets store state by ID, so a change breaks **state compatibility**.

If a change to the state layout is unavoidable (new parameter semantics, different ranges, migrated values), add a migration step and **bump `stateVersion`** so older projects load correctly. Adding new parameters with new IDs is fine. Document the change in `CHANGELOG.md`.

## Pull request checklist

See `.github/PULL_REQUEST_TEMPLATE.md`: conventional title, builds and tests pass, formatted, parameter IDs untouched (or `stateVersion` bumped), tests and docs updated, changelog entry.

## License

By contributing you agree that your contributions are licensed under AGPL-3.0-or-later.
