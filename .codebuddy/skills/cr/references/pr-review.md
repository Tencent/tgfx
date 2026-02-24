# PR Review

PR review uses **Worktree mode** — fetch the PR branch locally so review can
read related code across modules, at the exact version of the PR branch.

## Input from SKILL.md

- `REVIEW_PRIORITY`: A | A+B | A+B+C

Auto-fix is not available in PR mode.

## References

| File | Purpose |
|------|---------|
| `code-checklist.md` | Code review checklist |
| `doc-checklist.md` | Document review checklist |
| `judgment-matrix.md` | Worth-fixing criteria and special rules |

---

## Step 1: Create worktree

If `$ARGUMENTS` is a URL, extract the PR number from it.

Check whether the current branch is already the PR branch:
```bash
CURRENT_BRANCH=$(git branch --show-current)
PR_BRANCH=$(gh pr view {number} --json headRefName --jq .headRefName)
[ "$PR_BRANCH" = "$CURRENT_BRANCH" ]
```

**If current branch equals `PR_BRANCH`**, skip worktree creation — the code is
already local.

**Otherwise**, create a worktree:
```bash
# Clean up leftover worktrees from previous sessions
for dir in /tmp/pr-review-*; do
    [ -d "$dir" ] || continue
    n=$(basename "$dir" | sed 's/pr-review-//')
    git worktree remove "$dir" 2>/dev/null
    git branch -D "pr-${n}" 2>/dev/null
done

# Create worktree and switch into it
git fetch origin pull/{number}/head:pr-{number}
git worktree add --no-track /tmp/pr-review-{number} pr-{number}
cd /tmp/pr-review-{number}
```

---

## Step 2: Collect diff and context

Fetch PR metadata:
```bash
gh pr view {number} --json baseRefName,headRefOid,state,body
```
Extract: `BASE_BRANCH`, `HEAD_SHA`, `STATE`, `PR_BODY`.
If `STATE` is not `OPEN`, inform the user, clean up worktree, and exit.

Get diff:
```bash
git fetch origin {BASE_BRANCH}
git diff $(git merge-base origin/{BASE_BRANCH} HEAD)
```
If the diff exceeds 200 lines, first run `git diff --stat` to get an overview,
then read the diff per file using `git diff -- {file}` to avoid output
truncation.

Fetch existing PR review comments for de-duplication:
```bash
gh api repos/{owner}/{repo}/pulls/{number}/comments
```
Store as `EXISTING_PR_COMMENTS`.

If diff is empty → clean up worktree (if created) and exit.

---

## Step 3: Review

For each changed file, read its full content using the Read tool (not just
the diff — full file context is needed for accurate review). Then read
definitions of referenced symbols (types, base classes, called functions) that
are relevant to understanding the change's correctness.

Read `PR_BODY` (from Step 2) to understand the stated motivation and approach.
Verify the implementation actually achieves what the author describes.

Apply `code-checklist.md` to code files, `doc-checklist.md` to documentation
files. Only include priority levels the user selected.

For each issue found:
- Provide a code citation (file:line + snippet).
- Self-verify before confirming — re-read the surrounding code and check:
  - Is there a guard, null check, or early return elsewhere that already
    handles this case?
  - Does the call chain guarantee preconditions that make this issue
    impossible?
  - Am I misunderstanding the variable's lifetime or ownership?
  If any of these apply, withdraw the issue.
- De-duplicate against `EXISTING_PR_COMMENTS` — skip issues already covered.

---

## Step 4: Clean up and report

If a worktree was created, clean it up first:
```bash
cd -
git worktree remove /tmp/pr-review-{number}
git branch -D pr-{number}
```

**If no issues found** → inform the user that the review is clean, summarize
the key areas checked, and ask whether to submit an approval review:
```bash
gh api repos/{owner}/{repo}/pulls/{number}/reviews --input - <<'EOF'
{
  "commit_id": "{HEAD_SHA}",
  "event": "APPROVE"
}
EOF
```

**If issues found** → present confirmed issues:

```
{N}. [{priority}] {file}:{line} — {description of the problem and suggested fix}
```

Where `{priority}` is the checklist item ID (e.g., A2, B1, C7). User selects
which to submit as PR comments, declines are marked `skipped`.

Submit as a **single** GitHub PR review with line-level comments. **Must** use
`gh api` + heredoc:

```bash
gh api repos/{owner}/{repo}/pulls/{number}/reviews --input - <<'EOF'
{
  "commit_id": "{HEAD_SHA}",
  "event": "COMMENT",
  "comments": [
    {
      "path": "relative/file/path",
      "line": 42,
      "side": "RIGHT",
      "body": "Description of the issue and suggested fix"
    }
  ]
}
EOF
```

- `path`: relative to repository root
- `line`: line number in the **new** file (right side of diff)
- `side`: always `"RIGHT"`
- `body`: concise, in the user's conversation language

Summary of issues found / submitted / skipped.
