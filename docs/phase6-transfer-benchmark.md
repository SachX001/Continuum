# Phase 6 Transfer Benchmark

This benchmark measures whether a fresh AI session can continue from a generated Continuum transfer package.

## Automated run

From PowerShell at the repository root:

```powershell
.\tests\phase6_transfer_benchmark.ps1 -SkipOllama
```

For the full local compilation stage, with Ollama running:

```powershell
.\tests\phase6_transfer_benchmark.ps1 -Model qwen2.5-coder:3b
```

The fixture contains an initial C++ implementation, a debugging discussion, and an explicitly superseding correction. The deterministic stage verifies that both implementations, exact source text, and artifact IDs survive prompt generation. The Ollama stage additionally validates the generated package and memory-update prompt.

## Fresh-session evaluation

Open a new Claude conversation and provide only `output/context/phase6-benchmark/memory_update_prompt.md`. Ask:

> Continue the parser project. Identify the current implementation, explain why it supersedes the earlier version, and show the exact current C++ source without merging it with the historical version.

Record the result using this rubric:

- **Current implementation:** identifies `parser-v2` / `trim_preserving_code`.
- **Supersession reasoning:** cites the duplicate-marker/code-block loss bug.
- **Source fidelity:** preserves the corrected source as a separate implementation.
- **History separation:** does not present parser-v1 and parser-v2 as one compilable blob.
- **Continuation:** proposes work based on parser-v2.

The fresh-session test is successful only when all five criteria pass. Model quality and token usage should be recorded separately from deterministic package validation.
