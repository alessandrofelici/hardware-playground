# rootclock: planning documents

Planning set for **rootclock**, a FreeWili OG app: a chess-clock-style turn timer for 3–4
players of the board game Root. No firmware has been written yet. These documents define
what to build, how it is structured, and how an agent should build it.

Read them in order:

| # | Document | What it settles |
|---|---|---|
| 01 | [Spec review](01-spec-review.md) | The requirements with IDs, hardware constraints, every gap or conflict and how it was resolved, the button map, and the timer rules |
| 02 | [MVP](02-mvp.md) | What is in and out of scope, the screens with pixel layouts, and the acceptance tests that prove it works on the board |
| 03 | [Object model](03-object-model.md) | The classes table, page → class mapping, mermaid class and state diagrams, file layout, and invariants |
| 04 | [Future: ESP32 → mobile integration](04-future-esp32-integration.md) | The data path through both CPUs and the Bottlenose, and **payload contract v1** |
| 05 | [Agent implementation plan](05-agent-implementation-plan.md) | Securing `main`, the feature branch, worktrees and subagents, gates, and stop-and-ask points |
| 06 | [Mobile app starter brief](06-mobile-app-starter-spec.md) | A self-contained hand-off for an agent in another repo, with the contract and fixtures |

[`fixtures/fixture-c-device-4p.json`](fixtures/fixture-c-device-4p.json) is a real 4-player
result captured from the board on 2026-11-20. It is compact contract-v1 output, as emitted,
for the app's parser tests.

Decisions the user made while this was being planned:

- the pause-menu button scheme;
- a per-turn time limit that keeps counting past the limit;
- pause stops the global timer;
- the app is named `rootclock`.

The user also accepted every item marked **Assumed** in
[01 §3](01-spec-review.md#3-gaps-and-conflicts-and-how-they-were-resolved).

The build runs **autonomously, in one go**. The answers given in advance are listed in
[05](05-agent-implementation-plan.md#run-mode-autonomous-in-one-go).
