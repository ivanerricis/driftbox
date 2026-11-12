# Branch protection

Recommended settings for `main`: pull request required, required status checks, linear history, squash merge only, no force-push or deletion.

## Repository merge settings

```
gh repo edit ivanerricis/driftbox \
  --enable-squash-merge --enable-merge-commit=false --enable-rebase-merge=false \
  --delete-branch-on-merge --enable-auto-merge
```

## Ruleset via `gh api`

The required contexts are the check names shown by `gh pr checks <number>` on a recent PR. For reusable workflows the name is `<caller job> / <called job>`, so the build checks are `build (windows) / build (windows)` and `build (linux) / build (linux)` (the `plan` jobs and `build-macos` are not required).

```
gh api -X POST repos/ivanerricis/driftbox/rulesets --input - <<'JSON'
{
  "name": "protect-main",
  "target": "branch",
  "enforcement": "active",
  "conditions": { "ref_name": { "include": ["~DEFAULT_BRANCH"], "exclude": [] } },
  "rules": [
    { "type": "deletion" },
    { "type": "non_fast_forward" },
    { "type": "required_linear_history" },
    { "type": "pull_request", "parameters": {
        "required_approving_review_count": 0,
        "dismiss_stale_reviews_on_push": true,
        "require_code_owner_review": false,
        "require_last_push_approval": false,
        "required_review_thread_resolution": false,
        "allowed_merge_methods": ["squash"] } },
    { "type": "required_status_checks", "parameters": {
        "strict_required_status_checks_policy": true,
        "required_status_checks": [
          { "context": "clang-format 18" },
          { "context": "PR title (conventional commit)" },
          { "context": "build (windows) / build (windows)" },
          { "context": "build (linux) / build (linux)" },
          { "context": "asan-ubsan" },
          { "context": "clang-tidy" }
        ] } }
  ]
}
JSON
```

List or remove rulesets: `gh api repos/ivanerricis/driftbox/rulesets`, `gh api -X DELETE repos/ivanerricis/driftbox/rulesets/<id>`.

`asan-ubsan` (AddressSanitizer + UndefinedBehaviorSanitizer on the Debug build) and `clang-tidy` (any project warning fails the job, `--warnings-as-errors='*'`) are required too: both are clean and no longer use `continue-on-error`. If an existing ruleset still lists other names for them, update it with the JSON above (list rulesets with `gh api repos/ivanerricis/driftbox/rulesets`, then `gh api -X PUT repos/ivanerricis/driftbox/rulesets/<id> --input -`).

macOS is only built on `main` or with the `ci:macos` label, so do not make it a required check.

## Private repositories

The repository is public, so rulesets are enforced on the Free plan. If it were ever made private again, rulesets and branch protection rules would need GitHub Pro, Team or Enterprise to be enforced.
