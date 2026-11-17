# Continuum

Continuum is a local-first C++20 tool for transferring reliable engineering context between AI sessions.

It extracts conversations from Claude Desktop, preserves original code artifacts, stores evidence and provenance in SQLite, builds reviewable project memory, and compiles task-specific transfer packages locally with Ollama.

## Current capabilities

- Claude Desktop UI Automation capture
- Conversation parsing with role, order, and code-artifact preservation
- Exact artifact text plus normalized readable code
- Artifact provenance, implementation status, revisions, and supersession
- Local SQLite evidence repository with migrations and FTS5 retrieval
- Deterministic context proposals for decisions, constraints, tasks, and unresolved issues
- Review actions including accept, reject, and supersede
- Project memory views and deterministic checkpoints
- Markdown and JSON context compilation
- Optional local Ollama compilation without a cloud API key
- Regression tests for parsing, artifact preservation, review, checkpoints, and compilation

## Requirements

- Windows
- MSYS2 UCRT64
- CMake
- Ninja
- C++20 compiler
- Optional: Ollama with a compatible local model for local AI-assisted compilation

## Build

From an MSYS2 UCRT64 terminal:

```bash
cd /d/continuum-uia-poc
cmake -S . -B build -G Ninja
cmake --build build
```

Run the regression tests:

```bash
ctest --test-dir build --output-on-failure
```

## Main workflow

Capture and compile a Claude conversation:

```bash
./build/continuum-uia-poc.exe --compile text
```

Compile locally with Ollama:

```bash
./build/continuum-uia-poc.exe --compile-local qwen2.5-coder:3b
```

Compile reviewed project memory:

```bash
./build/continuum-uia-poc.exe --compile-project-local test-project qwen2.5-coder:3b "Continue the reviewed project"
```

Inspect project memory:

```bash
./build/continuum-uia-poc.exe --inspect-memory test-project
```

Generated runtime data is written under `output/` and is intentionally excluded from version control because it may contain private conversations and local databases.

## Architecture

Continuum treats raw extraction as immutable evidence. Parsed messages and extracted artifacts are derived data. Project memory is created through proposals and explicit review actions, so corrections supersede earlier records without destroying history.

The current implementation uses deterministic local retrieval first. Embeddings, graph traversal, automatic synchronization, and provider-independent automation are intentionally deferred until the core transfer workflow is benchmarked and stable.

See:

- [`docs/continuum-architecture-roadmap.md`](docs/continuum-architecture-roadmap.md)
- [`docs/episodic-memory-architecture.md`](docs/episodic-memory-architecture.md)

## Known limitations

- Claude Desktop UI changes may affect extraction reliability.
- The current workflow is primarily single-provider and CLI-based.
- Human review remains part of the authoritative memory-update path.
- Fresh-session transfer evaluation is still being expanded.

