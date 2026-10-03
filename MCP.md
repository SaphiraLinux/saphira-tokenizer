# MCP Persistence Policy

SHAMPOO MCP is a normal part of development work, not an optional reporting
facility.

Every agent working on this project MUST use the available MCP knowledge
surfaces throughout its work.

The objective is simple:

> Git preserves the source.
> Markdown preserves the detailed human record.
> MCP preserves the machine-usable knowledge needed to understand, operate,
> recover and continue the system.

All three are required.

## MCP endpoint services to use

memory
entity
bridge
intel
session
tasks

Memory is the most important, followed by tasks, entity and sessions.
Bridge is only used when more than one agent may need to share information;
otherwise leave memory notes for each project (include the git upstream or
endpoint and commit as reference).

## During every task

Agents MUST actively record durable information as it is discovered.
Do not wait until context is nearly exhausted to preserve important
discoveries.

### Memory

Use Memory for knowledge a future agent needs in order to work correctly:

- architectural invariants
- design decisions and their reasons
- operator rules
- important implementation behaviour
- recovery knowledge
- non-obvious constraints
- significant gotchas
- failed approaches worth avoiding
- facts necessary to resume work safely

### Intel

Use Intel for verified technical facts established through source
inspection, tests, measurements or observed behaviour:

- protocol behaviour
- database/table behaviour
- API/tool semantics
- dependencies
- measured performance
- confirmed defects
- component relationships
- implementation facts
- compatibility findings

Intel must distinguish verified evidence from assumption.

### Entities and relationships

Maintain useful entities for products, modules, services, stores and
other significant components. Record meaningful relationships where the
MCP surface supports them. Do not create entities merely to increase
counts.

### Sessions

Sessions MUST preserve continuity:

- what is being worked on
- repository/branch/commit state
- important findings
- decisions taken
- unresolved questions
- blockers
- next action

A new agent should be able to recover the active line of work without
needing the previous agent.

### Tasks and notes

Use Tasks for actionable work. Establish task identity at start
(task/title, product/project, repo, branch, starting commit, objective)
and read relevant existing tasks, notes, memories, intel, entities,
sessions and canonical docs before rediscovering known facts.

Use Notes for durable observations, indexes, handoffs and records that
are not themselves work items. Do not create meaningless tasks merely
to satisfy persistence requirements.

## Documentation

MCP DOES NOT replace Markdown documentation.

Detailed product, development, operation, security, recovery and protocol
knowledge belongs in the appropriate `.md` documentation. MCP records
should point back to the authoritative source where appropriate:

- repository
- commit
- file/path
- task
- test/evidence

Do not copy entire documents into Memory or Intel simply for redundancy.
Preserve enough structured knowledge that an agent can locate, understand
and correctly use the canonical documentation.

## Mandatory task close

Every task, without exception, has a persistence close-out. A task is
NOT DONE until the close protocol below has completed.

Before saying DONE, ALWAYS perform ALL of these:

1. GIT — record final commit SHA and branch, verify tree state, preserve
   failed evidence where applicable.
2. TASK / NOTE — update/create the record with objective, result,
   files/components changed, tests/proof, commit SHA, outstanding work,
   failures/deviations.
3. MEMORY — ALWAYS write at least one task-close memory (what was done,
   decisions, operational knowledge, result, commit/source reference,
   what the next agent must know). NO TASK CLOSES WITHOUT MEMORY.
4. INTEL — ALWAYS process task findings into Intel. If the task produced
   no new verified technical fact, explicitly record that in the
   close-out rather than inventing Intel.
5. SESSION — ALWAYS save/update the session (project, completed task,
   repository state, final SHA, next action, blockers/open questions).
6. DOCUMENTATION — ALWAYS perform a documentation close-out. If
   behaviour/API/schema/operation/security/setup changed, update the
   relevant canonical docs; otherwise update the appropriate
   internal task/design/recovery record. "No docs needed" is not an
   excuse to skip documentation review.
7. ENTITIES / RELATIONSHIPS — update affected products, modules,
   services, databases, capabilities, ownership/relationships.
8. RECOVERY CHECK — ask mechanically: could another agent resume this
   work from Git + .md + MCP state without asking the previous agent
   what happened? If NO, the task is not complete.

## Recovery criterion

Before declaring DONE, ask:

> Could a new agent with the Git repositories, Markdown documentation
> and restored MCP knowledge understand what happened, restore the
> relevant state, and continue correctly?

If not, the task is not finished.

## Failure of persistence

If the implementation/task itself succeeds but an MCP persistence
operation fails, report:

    TASK COMPLETE / PERSISTENCE INCOMPLETE

Preserve the work and repair the persistence state before proceeding
where possible. Never hide or silently discard an MCP persistence
failure. If persistence fails: repair before moving to the next task.

## Source-of-truth hierarchy

For exact source bytes:
    Git / repository backups.

For detailed product and engineering knowledge:
    canonical Markdown documentation.

For machine-oriented continuity, knowledge, provenance and
reconstruction:
    MCP Memory, Intel, Entities, Sessions, Tasks and Notes.

These layers complement one another. None should be treated as
permission to neglect the others.

Git/source + canonical .md documentation = authoritative durable
product record. SHAMPOO MCP = machine-readable knowledge and recovery
plane. Use BOTH.

## No silent completion

An agent MUST NOT finish with "done", "tests pass" or "committed"
without first completing the persistence close protocol.

## Default rule — do not ask, do it

DO NOT ASK:
    "Should I add this to memory?"
    "Should I update Intel?"
    "Should I document this?"

Do it. Persistence is part of doing the work. Persistence is part of
the task.
