---
name: new-feature
description: Implement one feature of ZubuvZpevnikUi end to end with the plan → implement → test → review loop. Use when starting a milestone item from docs/PLAN.md.
argument-hint: "<feature or milestone item>"
disable-model-invocation: true
---
Implement: $ARGUMENTS

1. Read CLAUDE.md and docs/PLAN.md. Find the matching milestone item.
2. Explore only what's needed. Use the `songbook-latex` or `chords-converter` agent for exact file formats or CLI behaviour instead of guessing.
3. Present a short plan: which files in core/, app/ and tests/ change, the core API (std-only), how it will be tested, and **which parts go to the `cpp-implementer` agent and which you do yourself**. Wait for my OK.
4. Implement core/ first with unit tests, then the Qt layer in app/. Split the work:
   - **Yourself:** public API headers, design decisions, and integration work that spans core/ and app/.
   - **`cpp-implementer` agent (Sonnet):** well-specified pieces. Give it a self-contained spec: exact files, the signatures (or headers you already wrote), behaviour including edge cases, and the tests it must write. Independent pieces can go to parallel agents only if they touch different files.
   - Read every diff it produces before moving on. Fix small problems yourself; send larger ones back to the same agent (SendMessage) with concrete feedback.
5. Build and run the tests (`cmake --build --preset debug && ctest --preset debug`). Fix failures before going on.
6. Run the `cpp-reviewer` agent on the diff, fix what is real, and tell me what you skipped and why.
7. Tick the item in docs/PLAN.md and update CLAUDE.md if commands or decisions changed. Summarise what to try manually in the app. Do not commit unless I ask.
8. Report the delegation: what each subagent did, its token count (from its completion notification), what you had to fix in its work, and whether delegating paid off for this feature.
