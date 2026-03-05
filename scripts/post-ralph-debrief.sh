#!/bin/bash
# Post-Ralph Debrief — Auto-updates project status after Ralph Loop completes
# Runs in the project directory, chained after ralph.sh

set -e

echo ""
echo "==============================================================="
echo "  Post-Ralph Debrief — Updating project status..."
echo "==============================================================="
echo ""

# Verify we're in a project directory
if [ ! -f "prd.json" ] || [ ! -d "docs" ]; then
  echo "Error: Not in a valid project directory (no prd.json or docs/)"
  exit 1
fi

# Run Claude to read project state and update status docs
claude --dangerously-skip-permissions --print <<'DEBRIEF_PROMPT'
You are a VP performing a post-Ralph Loop debrief. Update the project status docs to reflect the latest state.

## Steps

1. Read these files to understand current state:
   - `prd.json` — check which stories pass and which don't
   - `progress.txt` — read the latest iteration entries
   - `docs/status.md` — current status (you will rewrite this)
   - `docs/worklog.md` — work log (you will append to this)
   - `docs/next-tasks.md` — upcoming tasks (if it exists)

2. **Rewrite `docs/status.md`** with fresh status:

```markdown
# Project Status

**Last updated:** <today's date> (auto-updated after Ralph Loop)

**Current state:** <1-2 sentence summary of where the project stands>

## PRD Progress

- Stories complete: X/Y
- Remaining: <list titles of stories where passes is false, or "All complete!">

## Recently Completed (Latest Ralph Run)

<from progress.txt latest entries — what stories were done, key files changed>

## Up Next

<from remaining PRD stories ordered by priority, and next-tasks.md if it exists>

## Blockers & Notes

<any issues noted in progress.txt, or "None">
```

3. **Append to `docs/worklog.md`**:

```markdown

## <today's date> - Ralph Loop completed (auto-debrief)

- Stories completed this run: <count>
- Summary: <brief 1-2 line description of what was accomplished>
- PRD progress: X/Y stories complete
```

IMPORTANT: Keep updates factual. Only report what the files actually say.
DEBRIEF_PROMPT

echo ""
echo "Debrief complete — docs/status.md updated"
echo ""
