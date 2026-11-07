---
name: new-feature
description: Implement one feature of ZubuvZpevnikUi end to end with the plan → implement → test → review loop. Use when starting a milestone item from docs/PLAN.md.
argument-hint: "<feature or milestone item>"
disable-model-invocation: true
---
Implement: $ARGUMENTS

1. Read CLAUDE.md and docs/PLAN.md. Find the matching milestone item.
2. Explore only what's needed. Use the `songbook-latex` or `chords-converter` agent for exact file formats or CLI behaviour instead of guessing.
3. Present a short plan: which files in core/, app/ and tests/ change, the core API (std-only), and how it will be tested. Wait for my OK.
4. Implement core/ first with unit tests, then the Qt layer in app/.
5. Build and run the tests (`cmake --build --preset debug && ctest --preset debug`). Fix failures before going on.
6. Run the `cpp-reviewer` agent on the diff, fix what is real, and tell me what you skipped and why.
7. Tick the item in docs/PLAN.md and update CLAUDE.md if commands or decisions changed. Summarise what to try manually in the app. Do not commit unless I ask.
