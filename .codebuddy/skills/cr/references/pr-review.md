# PR Review

PR review uses **Worktree mode** — fetch the PR branch locally so review can
read related code across modules, at the exact version of the PR branch. This
is critical for review accuracy.

---

## Step 1: Create worktree

If `$ARGUMENTS` is a URL, extract the PR number from it.

Clean up leftover worktrees from previous sessions:
```bash
for dir in /tmp/pr-review-*; do
    [ -d "$dir" ] || continue
    n=$(basename "$dir" | sed 's/pr-review-//')
    git worktree remove "$dir" 2>/dev/null
    git branch -D "pr-${n}" 2>/dev/null
done
```

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
git fetch origin pull/{number}/head:pr-{number}
git worktree add --no-track /tmp/pr-review-{number} pr-{number}
cd /tmp/pr-review-{number}
```

---

## Step 2: Collect diff and context

```bash
gh repo view --json nameWithOwner --jq .nameWithOwner
gh pr view {number} --json baseRefName,headRefOid,state,body
```
Record `OWNER_REPO`. Extract: `BASE_BRANCH`, `HEAD_SHA`, `STATE`, `PR_BODY`.
If `STATE` is not `OPEN`, inform the user, clean up worktree, and exit.

Get diff:
```bash
git fetch origin {BASE_BRANCH}
git diff $(git merge-base origin/{BASE_BRANCH} HEAD)
```
If the diff exceeds 200 lines, first run `git diff --stat` to get an overview,
then read the diff per file using `git diff -- {file}` to avoid output
truncation.

Fetch existing PR comments:
```bash
gh pr view {number} --comments
gh api repos/{OWNER_REPO}/pulls/{number}/comments
```

---

## Step 3: Review

**Internal analysis** (do not output anything during this phase):

1. Based on the diff, read relevant code context as needed to understand the
   change's correctness (e.g., surrounding logic, base classes, callers).
2. Read `PR_BODY` to understand the stated motivation. Verify the
   implementation actually achieves what the author describes.
3. Apply `code-checklist.md` to code files, `doc-checklist.md` to
   documentation files. Only include priority levels the user selected. Use
   `judgment-matrix.md` to decide whether each issue is worth reporting.
4. Check whether issues raised in previous PR comments have been fixed.
5. For each potential issue, perform a second-pass verification: re-read the
   surrounding code to rule out false positives, already-handled cases, or
   misunderstandings.
6. **Discard all ruled-out issues. Keep only issues confirmed to exist.**

De-duplicate confirmed issues against existing PR comments.

**Output rule**: only present the final confirmed issues to the user. Do not
output analysis process, exclusion reasoning, or issues that were considered
but ruled out.

---

## Step 4: Clean up and report

If a worktree was created, clean it up:
```bash
cd -
git worktree remove /tmp/pr-review-{number}
git branch -D pr-{number}
```

Present results to user:
- Summary: one paragraph describing the purpose and scope of the change.
- Overall assessment: code quality evaluation and key improvement directions.
- Issue list (or "no issues" if none).

**If no issues found** → ask whether to submit an approval:
```bash
gh api repos/{OWNER_REPO}/pulls/{number}/reviews --input - <<'EOF'
{
  "commit_id": "{HEAD_SHA}",
  "event": "APPROVE"
}
EOF
```

**If issues found** → present confirmed issues:

```
{N}. [{priority}] {file}:{line} — {description and suggested fix}
```

Ask user which issues to submit using a multi-select interactive prompt, with
each issue as a selectable option. Submit selected issues as line-level PR
comments.

**Must** use `gh api` + heredoc. Do not use `gh pr comment`, `gh pr review`,
or any command that creates non-line-level comments:

```bash
gh api repos/{OWNER_REPO}/pulls/{number}/reviews --input - <<'EOF'
{
  "commit_id": "{HEAD_SHA}",
  "event": "COMMENT",
  "comments": [
    {
      "path": "relative/file/path",
      "line": 42,
      "side": "RIGHT",
      "body": "Issue description and suggested fix"
    }
  ]
}
EOF
```

Summary of issues found / submitted / skipped.
