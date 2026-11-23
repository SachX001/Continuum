# Continuum

Continuum is a local-first engineering context-transfer system for AI conversations.

It addresses a common problem: an AI coding session contains important decisions, debugging history, implementation revisions, and source code, but a later session does not have that context. Copying the entire conversation wastes tokens and can cause multiple code revisions to be merged into one incorrect blob.

Continuum extracts the conversation, preserves original technical artifacts, stores evidence and provenance locally, creates reviewable project memory, and generates a compact transfer package for a fresh AI session.

## Why Continuum exists

A normal conversation summary is not enough for engineering work. A summary may remember that a parser was corrected but omit the exact source. It may preserve the newest code while losing why the older version was rejected. It may also describe proposed code as tested code.

Continuum separates these concerns:

1. Raw conversation capture is treated as evidence.
2. Parsed messages and code artifacts are derived from that evidence.
3. Context proposals are extracted deterministically and remain reviewable.
4. Accepted memory represents the current reviewed project state.
5. Exact source artifacts remain separate from model-generated summaries.
6. A task-specific transfer package is generated for the next AI session.

This lets the destination session understand both the current implementation and the history that led to it, without receiving the entire original conversation.

## Core capabilities

- Capture conversations from Claude Desktop through Windows UI Automation.
- Detect incomplete extraction and refuse to process stale output.
- Parse ordered user and assistant messages.
- Extract multiple code blocks without merging separate implementations.
- Preserve exact source text and readable normalized display text.
- Record message-level provenance for every technical artifact.
- Track proposed, tested, superseded, and revised artifact relationships.
- Persist conversations, artifacts, proposals, and memory in local SQLite.
- Provide FTS5 message retrieval and metadata-based project retrieval.
- Extract decisions, constraints, tasks, and unresolved issues as proposals.
- Require explicit review before proposals enter authoritative project memory.
- Preserve superseded memory instead of silently deleting history.
- Create deterministic memory checkpoints.
- Generate Markdown and JSON transfer artifacts.
- Compile context locally with Ollama without a cloud API key.
- Validate generated packages before replacing the current package.
- Provide backup/restore, migration, rollback, and stabilization regression tests.

## Architecture

```text
Claude Desktop
     │
     ▼
UI Automation capture
     │
     ▼
Raw evidence + diagnostics
     │
     ▼
Conversation parser
     ├── ordered messages
     └── exact technical artifacts
     │
     ▼
SQLite evidence repository
     ├── FTS5 retrieval
     ├── provenance
     ├── revisions
     └── reviewable proposals
     │
     ▼
Reviewed project memory and checkpoints
     │
     ▼
Task-specific compilation prompt
     ├── deterministic artifact manifest
     └── optional local Ollama semantic compilation
     │
     ▼
Validated transfer package and memory-update prompt
```

The model is not the source of truth for code. Ollama receives compact semantic context, while Continuum restores exact artifacts from the local evidence manifest after generation. This avoids asking a model to reproduce large source blocks through JSON.

## Requirements

- Windows 10 or later
- MSYS2 UCRT64
- CMake 3.20 or newer
- Ninja
- A C++20 compiler
- SQLite3 development libraries
- Optional: Ollama and a local model such as `qwen2.5-coder:3b`

The current acquisition adapter targets Claude Desktop. Storage and compilation are provider-neutral, but other conversation providers are not yet implemented.

## Build from source

Open an MSYS2 UCRT64 terminal:

```bash
cd /d/continuum-uia-poc
cmake -S . -B build -G Ninja
cmake --build build --parallel 2
```

Run the automated regression suite:

```bash
ctest --test-dir build --output-on-failure
```

When running from PowerShell, make sure the MSYS2 UCRT64 runtime is available:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:Path"
ctest --test-dir build --output-on-failure
```

## Typical workflow

### 1. Capture and parse a Claude conversation

With the intended Claude Desktop conversation visible:

```bash
./build/continuum-uia-poc.exe
```

Continuum writes raw and parsed evidence under `output/`. If UI extraction fails, the command stops and refuses to parse an older capture.

### 2. Generate a deterministic compilation prompt

```bash
./build/continuum-uia-poc.exe --compile-context
```

The default output is `output/context/compilation_prompt.md`.

A fixture or another parsed conversation can be compiled explicitly:

```bash
./build/continuum-uia-poc.exe \
  --compile-context tests/fixtures/phase6_multi_revision.json \
  output/context/phase6-benchmark/compilation_prompt.md
```

### 3. Compile locally with Ollama

Start Ollama and confirm that the model is installed:

```bash
ollama list
```

Then run:

```bash
./build/continuum-uia-poc.exe \
  --compile-local qwen2.5-coder:3b
```

For a custom conversation and prompt path:

```bash
./build/continuum-uia-poc.exe \
  --compile-local qwen2.5-coder:3b \
  output/context/phase6-benchmark/compilation_prompt.md \
  tests/fixtures/phase6_multi_revision.json
```

The package is written only after generation and validation succeed.

### 4. Validate and build the transfer prompt

```bash
./build/continuum-uia-poc.exe \
  --validate-context output/context/context_package.json

./build/continuum-uia-poc.exe \
  --build-memory-prompt \
  output/context/context_package.json \
  output/context/memory_update_prompt.md
```

Provide `memory_update_prompt.md` to a new AI conversation. The destination session should receive the current implementation, historical revisions, provenance, status, and reason for supersession without treating the revisions as one file.

## Project memory workflow

Import parsed evidence into the local repository:

```bash
./build/continuum-uia-poc.exe \
  --import output/parsed_conversation.json my-project
```

Search stored messages:

```bash
./build/continuum-uia-poc.exe \
  --search "duplicate marker" my-project
```

Generate deterministic proposals:

```bash
./build/continuum-uia-poc.exe \
  --propose output/parsed_conversation.json my-project
```

Review proposals before accepting them:

```bash
./build/continuum-uia-poc.exe --list-proposals my-project proposed
./build/continuum-uia-poc.exe --review-proposal <proposal-id> accepted "Confirmed"
```

Create and inspect a project checkpoint:

```bash
./build/continuum-uia-poc.exe --checkpoint my-project "reviewed state"
./build/continuum-uia-poc.exe --inspect-memory my-project
```

Compile reviewed project memory locally:

```bash
./build/continuum-uia-poc.exe \
  --compile-project-local my-project \
  qwen2.5-coder:3b \
  "Continue the reviewed project"
```

## Code revision handling

Suppose a conversation contains:

1. An initial implementation.
2. A debugging discussion identifying a defect.
3. A corrected implementation that explicitly supersedes the first.

Continuum stores the implementations separately. The later artifact receives links such as `supersedes_id` and `revision_of`, while the earlier artifact remains available as historical evidence. The generated transfer prompt can therefore tell the destination model:

- which implementation is current;
- which implementation is historical;
- why the correction superseded the earlier version;
- where each artifact came from;
- whether the artifact was tested or merely proposed.

This is intentionally different from concatenating all code blocks into one summary or merged source file.

## Tests and benchmarks

Run the Phase 6 transfer benchmark without Ollama:

```powershell
.\tests\phase6_transfer_benchmark.ps1 -SkipOllama
```

Run the full local benchmark:

```powershell
.\tests\phase6_transfer_benchmark.ps1 -Model qwen2.5-coder:3b
```

Run the Phase 7 stabilization regression:

```powershell
.\tests\phase7_stabilization.ps1
```

The tests cover no-code conversations, multiple revisions, exact artifact recovery, provenance, supersession, package validation, failed imports, migration, backup/restore, duplicate imports, and protection against stale package replacement.

## Packaging

Create a local installation tree:

```bash
cmake --install build --prefix build/package-root
```

Create the distributable ZIP:

```bash
cpack --config build/CPackConfig.cmake -B build/packages
```

The current release artifact is named similarly to `Continuum-0.1.0-win64.zip`.

GitHub Actions builds and tests the project using MSYS2 UCRT64 on Windows.

## Data and privacy

Continuum is local-first:

- SQLite storage is local.
- Ollama inference is local.
- No cloud AI API key is required for compilation.
- Conversation captures, databases, generated prompts, and packages are excluded from Git.
- Raw captures and backups may contain private conversation data and should be treated as sensitive.
- The MVP does not provide secure erasure from every backup.

## Current limitations

- Claude Desktop UI changes may require parser or UI Automation updates.
- The acquisition workflow is Windows-focused.
- Only the Claude Desktop provider adapter is currently implemented.
- Human review is required before proposals become authoritative memory.
- Fresh-session quality depends on the destination model.
- Embeddings, graph traversal, background synchronization, cloud storage, and multi-provider support are deferred beyond the MVP.
- Structured log aggregation and artifact signing are not included in the MVP.

## Media and project overview

- [Continuum architecture diagram](docs/media/continuum-architecture.svg)
- [LinkedIn demo storyboard](docs/media/linkedin-demo-storyboard.md)
- [GitHub repository](https://github.com/SachX001/Continuum)

## Project documentation

- [`docs/continuum-architecture-roadmap.md`](docs/continuum-architecture-roadmap.md) — phased architecture and implementation plan
- [`docs/episodic-memory-architecture.md`](docs/episodic-memory-architecture.md) — local episodic-memory design and embedding evaluation
- [`docs/phase6-transfer-benchmark.md`](docs/phase6-transfer-benchmark.md) — fresh-session transfer evaluation
- [`docs/mvp-release-checklist.md`](docs/mvp-release-checklist.md) — release and security checklist

## License

No license has been selected yet. Treat the repository as source-available for development until a license is added.
