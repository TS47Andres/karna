1. Strict Comment Pragmatism (No Historical Commemorations)
Do not write comments detailing the history, modifications, or reason for changes. Comments must exclusively express what the code is currently doing right now and state technical realities as they stand.

2. The One-Line Function Requirement
Directly above every single function declaration, you must write a single-line comment detailing its precise entry prerequisites, expected behavior, or return conditions.

3. Inline Variable Documentation
Immediately following every single non-trivial variable declaration, you must add an inline comment explaining what the reference represents or handles, unless the assignment is an obvious utility tracker (like a loop index i).

4. Zero Emojis Anywhere
No emojis in code, comments, copy, audit labels, error messages, in-app strings, commit messages, PR titles, PR descriptions, issue titles, or test names.

5. Commit Conventions
Conventional Commits format. Allowed types: feat, fix, chore, docs, refactor, test, perf, build, ci.
Commit message subject line maximum 72 characters.
Bodies must use complete sentences and end with periods.
No emojis. No Gitmoji shortcodes either (e.g., :rocket:).

6. Branch and Merge
Default branch: main.
Feature branches: phase-N/short-slug.
Squash merges only. The squash commit message must be a valid Conventional Commit.
Tags: vX.Y.Z.
