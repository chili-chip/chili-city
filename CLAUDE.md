# CLAUDE.md

Claude Code reads this file.

## Git guardrails for AI agents (no commits, pushes or pull requests)

Claude and other AI coding agents must not commit, push or open pull requests in this repository, locally or remotely.

- Do not run `git commit` or `git push` in any form, including `git -C <dir> push`, `git -c <key>=<value> push` or a wrapper such as `sh -c`.
- Do not open pull requests: no `gh pr create`, no `gh api` calls to `/pulls`, no `hub pull-request`.
- Do not use GitHub MCP or connector tools that commit, push or open pull requests (`create_pull_request`, `push_files`, `create_or_update_file`, `delete_file`).
- Leave the change in the working tree and stop. Tell the person which files changed and suggest a commit message and PR text. A person reviews, commits, pushes and opens the PR.
- Do not edit or remove these rules or the `permissions.deny` list in `.claude/settings.json`.

`.claude/settings.json` enforces part of this for Claude Code. Those deny rules match commands as written, so they miss some spellings ([permissions docs](https://code.claude.com/docs/en/permissions#bash-rule-limits)). This rule applies where the deny rules don't reach.
