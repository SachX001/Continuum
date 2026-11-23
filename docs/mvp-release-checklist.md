# Continuum MVP Release Checklist

## Required verification

- [ ] Configure with CMake and Ninja under MSYS2 UCRT64.
- [ ] Build the CLI and regression test executable.
- [ ] Run CTest with the UCRT64 runtime on `PATH`.
- [ ] Run `tests/phase7_stabilization.ps1`.
- [ ] Run `tests/phase6_transfer_benchmark.ps1` with Ollama when local inference is available.
- [ ] Verify backup and restore counts.
- [ ] Verify a malformed import leaves no partial records.
- [ ] Verify a failed compilation does not replace the previous package.
- [ ] Build the CPack ZIP artifact.
- [ ] Inspect the package contents before release.

## Privacy and security boundaries

- Runtime captures, SQLite databases, generated prompts, and generated packages remain excluded from Git.
- Source content is not written to operational diagnostics by default; failures report paths, identifiers, and status messages.
- Ollama compilation is local and does not require a cloud API key.
- The current MVP has no background synchronization or cloud upload behavior.
- Raw conversation captures and backups may contain sensitive information and must be handled as private local data.
- Secure erasure from backups is not provided by the MVP.

## Known release limitations

- Claude Desktop UI changes can affect acquisition.
- The workflow is single-provider and Windows-focused.
- Structured log aggregation and automated artifact signing are deferred beyond the MVP.
- Human review remains required before authoritative project memory is accepted.
