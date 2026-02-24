# PR Review

PR review uses **Worktree mode** — fetch the PR branch locally so review can
read related code across modules, at the exact version of the PR branch. This
is critical for review accuracy.

## Input from SKILL.md

- `REVIEW_PRIORITY`: A | A+B | A+B+C

## References

| File | Purpose |
|------|---------|
| `code-checklist.md` | Code review checklist |
| `doc-checklist.md` | Document review checklist |
| `judgment-matrix.md` | Worth-fixing criteria and special rules |

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

Fetch PR metadata and repo info:
```bash
gh repo view --json nameWithOwner --jq .nameWithOwner
gh pr view {number} --json baseRefName,headRefOid,state,body,author
```
Record `OWNER_REPO` from the first command. Extract: `BASE_BRANCH`,
`HEAD_SHA`, `STATE`, `PR_BODY`, `PR_AUTHOR`.
If `STATE` is not `OPEN`, inform the user, clean up worktree, and exit.

Get diff:
```bash
git fetch origin {BASE_BRANCH}
git diff $(git merge-base origin/{BASE_BRANCH} HEAD)
```
If the diff exceeds 200 lines, first run `git diff --stat` to get an overview,
then read the diff per file using `git diff -- {file}` to avoid output
truncation.

Fetch existing PR comments for de-duplication and to verify fixes:
```bash
gh pr view {number} --comments
gh api repos/{OWNER_REPO}/pulls/{number}/comments
```

If diff is empty → clean up worktree (if created) and exit.

---

## Step 3: Review

Based on the diff, read relevant code context as needed to understand the
change's correctness (e.g., surrounding logic, base classes, callers).

Read `PR_BODY` (from Step 2) to understand the stated motivation and approach.
Verify the implementation actually achieves what the author describes.

Apply `code-checklist.md` to code files, `doc-checklist.md` to documentation
files. Only include priority levels the user selected.

Verify existing PR comments: check whether issues raised in previous comments
have actually been fixed in the current code.

For each issue found:
- Provide a code citation (file:line + snippet).
- Self-verify before confirming — re-read the surrounding code and check:
  - Is there a guard, null check, or early return elsewhere that already
    handles this case?
  - Does the call chain guarantee preconditions that make this issue
    impossible?
  - Am I misunderstanding the variable's lifetime or ownership?
  If any of these apply, withdraw the issue.
- De-duplicate against existing PR comments — skip issues already covered.

---

## Step 4: Report and handle issues

Optimize PR title if needed:
```bash
gh pr edit {number} --title "New title"
```
Title format: English, under 120 characters, ending with a period, no other
punctuation, focusing on user-visible changes.

**If no issues found** → clean up worktree (if created), inform the user that
the review is clean, summarize the key areas checked, and ask whether to submit
an approval review:
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
{N}. [{priority}] {file}:{line} — {description of the problem and suggested fix}
```

Where `{priority}` is the checklist item ID (e.g., A2, B1, C7).

Determine code ownership: compare `PR_AUTHOR` with `gh api user -q '.login'`.

**Own code** → ask the user which issues to fix. Fix in worktree, commit and
push:
```bash
git add . && git commit -m "{message}"
git push origin HEAD:$PR_BRANCH
```
Then clean up worktree (if created).

**Others' code** → clean up worktree (if created) first, then ask the user
which issues to submit as PR comments. Never auto-fix and push to others'
branches.

User selects which to submit as PR comments, declines are marked `skipped`.

Submit as a **single** GitHub PR review with line-level comments. **Must** use
`gh api` + heredoc:

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

### Worktree cleanup

If a worktree was created and not yet cleaned up:
```bash
cd -
git worktree remove /tmp/pr-review-{number}
git branch -D pr-{number}
```

Summary of issues found / submitted / skipped.
