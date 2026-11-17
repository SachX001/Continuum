# Continuum Episodic Memory Architecture

Status: finalized design direction

## Decision

Continuum will use a hybrid memory model:

1. **Current project state** is the authoritative, structured view of what is
   currently implemented, unresolved, superseded, and next.
2. **Episodic memory** records meaningful historical events such as decisions,
   debugging discoveries, failed approaches, implementations, and test results.
3. **Raw conversations and exact technical artifacts** remain immutable evidence.
4. **Retrieval selects evidence and memory records for a task**; it does not
   decide which implementation is current by semantic similarity alone.
5. **The transfer package is generated deterministically** from the current
   state, selected episodes, and selected artifacts.

The first implementation will use SQLite and FTS5 with metadata filtering.
Vector embeddings are an optional second retrieval channel, introduced only
after a lexical-retrieval baseline and a fresh-session benchmark exist.

## Why this is the correct boundary

An embedding is useful for finding semantically related text. It is not a
reliable authority for revision order, implementation status, test status, or
conflict resolution. Those properties require explicit metadata and
relationships.

The repository remains authoritative for current source code. Conversation
artifacts supplement the repository with rationale, debugging history, and
uncommitted or proposed code; they must not silently replace repository state.

## Memory layers

### Evidence layer

Immutable records:

- raw capture bytes and checksum;
- normalized messages and source ranges;
- extracted technical artifacts;
- provider, account, conversation, and parser metadata.

Evidence is never rewritten when a later interpretation changes.

### Current-state layer

Accepted project records:

- current implementation and authoritative files;
- active decisions and constraints;
- unresolved issues;
- current tasks and next steps;
- supersession and conflict relationships.

Every record has provenance, lifecycle status, and a history of changes.

### Episodic layer

An episode represents one meaningful event, not an entire conversation. Initial
episode types are:

- `decision`
- `implementation`
- `debugging`
- `test_result`
- `failure`
- `discovery`
- `reversal`
- `handoff`

Each episode stores a concise summary plus links to its original messages,
artifacts, project, task, and related episodes. The summary is disposable; the
linked evidence is not.

### Transfer layer

A transfer package contains only the material needed for a selected task:

1. current project state;
2. active constraints and unresolved issues;
3. relevant episodes and their outcomes;
4. exact artifacts required for the task;
5. provenance, status, and supersession relationships;
6. a compact next-step objective.

The package includes a manifest and validation metadata. It must not be a raw
database dump or an unbounded conversation summary.

## Proposed records

The existing relational plan remains appropriate. Add these records to the
SQLite design:

```text
episodes(
  id, project_id, conversation_id, type, summary, original_content,
  outcome, status, confidence, created_at, source_message_id,
  extraction_version, review_state
)

episode_links(
  episode_id, related_episode_id, relationship_type
)

episode_artifacts(
  episode_id, artifact_id, relationship_type
)

embeddings(
  record_type, record_id, model_id, model_revision, dimensions,
  normalized, vector_blob, created_at
)

retrieval_runs(
  id, project_id, query_text, retrieval_policy, model_id,
  selected_record_ids, created_at
)
```

`context_records` or equivalent current-state records remain separate from
`episodes`. An episode saying “parser-v1 was implemented” must not make
parser-v1 current after a later episode says parser-v2 superseded it.

Embeddings are versioned by model and dimensions. Changing the embedding model
creates a new embedding set; it never silently mixes incompatible vectors.

## Retrieval algorithm

The retrieval service will use this order:

1. Identify the project and task.
2. Load current-state records, active constraints, unresolved conflicts, and
   authoritative artifact references.
3. Run SQLite FTS5 over episode summaries, original content, decisions, and
   artifact metadata.
4. Apply metadata filters for project, status, episode type, file/component,
   and current versus superseded state.
5. If enabled, run vector search over the same candidate population.
6. Fuse lexical and vector results, then apply recency and relationship rules.
7. Include supersession ancestors when a selected record depends on them, but
   label them as historical rather than current.
8. Enforce a character/token budget and emit an explanation of why each record
   was selected.
9. Compile and validate the transfer package.

The repository/current-state layer is always loaded before semantic retrieval.
Vector similarity cannot override an explicit supersession or conflict.

## Local storage evaluation

### Recommended: SQLite + FTS5 first

SQLite is the best MVP store because Continuum needs transactions, migrations,
relationships, provenance, status, backups, and human-inspectable local data.
FTS5 already provides phrase, prefix, proximity, boolean, snippet, and BM25
ranking support. It gives a deterministic retrieval baseline without adding a
model or a server. [SQLite FTS5 documentation](https://www.sqlite.org/fts5.html)

Use SQLite tables for all authoritative records and the filesystem for large
raw captures. Store checksums and paths in SQLite.

### Optional later: sqlite-vec beside SQLite

`sqlite-vec` can add vector search inside SQLite, which is attractive because
metadata and vectors remain in one local database. However, it is a separate,
pre-1.0 extension rather than a core SQLite feature, so it should be optional
and pinned to a tested version. It should not be introduced before the FTS5
baseline demonstrates a retrieval gap.

### Not recommended for the MVP: FAISS as the primary store

FAISS is a strong C++ similarity-search library and can search large dense-vector
indexes, but it is an index rather than Continuum's project repository. It does
not replace relational provenance, migrations, review state, or artifact
relationships. It would require a second persistence and synchronization layer.
[FAISS documentation](https://faiss.ai/)

### Not recommended for the MVP: Qdrant/Qdrant Edge

Qdrant provides local and embedded vector-search modes, filtering, and hybrid
search. It becomes attractive if Continuum grows to very large collections or
needs advanced vector operations. For a personal Windows MVP it adds another
storage system and lifecycle to package, back up, and migrate. Keep it as a
future adapter, not the authoritative store.
[Qdrant documentation](https://qdrant.tech/documentation/)

## Local embedding evaluation

### Recommended integration path: replaceable embedding interface

Define an interface with:

```text
model_id()
dimensions()
embed_documents(texts)
embed_query(text)
```

Store model ID, revision, dimensions, normalization, and input hash with every
embedding. The rest of Continuum must work when no embedding provider is
installed.

### Best fit for the current C++ application: llama.cpp embedding executable

llama.cpp is already C/C++-oriented and documents a local embedding executable
with mean pooling and normalized output. It avoids introducing Python into the
runtime and can use GGUF models. The tradeoff is that model packaging and
tokenization still need to be managed, and model quality must be benchmarked
for technical/project context. [llama.cpp embedding example](https://github.com/ggml-org/llama.cpp/tree/master/examples/embedding)

### Strong alternative: ONNX Runtime C++

ONNX Runtime provides a C++ API and Windows CPU/GPU packages. It is a good
choice when a selected sentence-transformer model is available in a compatible
ONNX form and Continuum needs a stable embeddable runtime. The tradeoff is a
larger integration and packaging surface than the current POC. [ONNX Runtime
C++ documentation](https://onnxruntime.ai/docs/get-started/with-cpp)

Sentence-transformer models are designed for fixed-size semantic vectors and
retrieval workflows, but query/document instruction conventions and model
licensing must be checked for the selected model. [Sentence Transformers
documentation](https://huggingface.co/docs/hub/en/sentence-transformers)

### Deferred options

- A Python `sentence-transformers` sidecar is useful for experiments but adds a
  second runtime and packaging burden to this C++ MVP.
- A large general-purpose local LLM is unnecessary for embeddings and would
  spend more memory and compute than a purpose-built encoder.
- Cloud embeddings violate the default local-first/cost constraint unless
  explicitly enabled by the user.

## Final technology recommendation

For the next implementation phase:

1. Add SQLite persistence and FTS5 retrieval.
2. Implement episode formation as reviewed proposals without embeddings.
3. Build a fresh-session benchmark and measure lexical retrieval recall and
   transfer success.
4. Add one embedding provider behind the interface only if the benchmark shows
   meaningful misses from vocabulary/paraphrase mismatch.
5. Start with llama.cpp if a C++/GGUF path is preferred; evaluate ONNX Runtime
   if the selected encoder is better supported there.
6. Keep SQLite as the source of truth; use sqlite-vec, FAISS, or Qdrant only as
   replaceable retrieval indexes.

## Why episodic memory is better than passing one JSON context

The traditional approach is a snapshot pipeline:

```text
conversation -> one summary JSON -> one transfer prompt
```

It is good when the conversation is short, the entire topic is relevant, and
the destination needs a one-time handoff. It is simple, cheap, transparent, and
easy to regenerate.

Its weaknesses appear as the project grows:

- each new handoff repeats unrelated history;
- compression can erase the reason behind a decision;
- one summary mixes current and obsolete implementations;
- there is no natural way to retrieve one old debugging episode;
- correcting a summary can overwrite history;
- the full package grows toward the destination model's context limit;
- code and provenance are vulnerable to model rewriting unless separately
  validated.

Episodic memory changes the unit of retention from “the conversation” to “the
meaningful event.” It can retrieve the debugging episode relevant to the
current task without sending every unrelated conversation. It also preserves
history and allows the current-state layer to distinguish authoritative code
from superseded experiments.

The improvement is not that episodes are inherently more accurate. It is that
they make selection, provenance, revision, and lifecycle explicit. A poor
episode extractor can still create bad memory, which is why proposals, source
links, validation, and fresh-session evaluation are mandatory.

## Cost comparison

| Concern | Single JSON snapshot | Hybrid episodic memory |
|---|---|---|
| Initial implementation | Low | Higher |
| Per-handoff tokens | Often grows with history | Bounded by retrieval budget |
| Historical recall | Depends on summary | Selective retrieval from episodes |
| Exact code preservation | Requires separate artifact channel | Same, with persistent references |
| Current-versus-obsolete state | Often ambiguous | Explicit status and supersession |
| Storage cost | Low | SQLite records plus optional vectors |
| Compute cost | AI summarization per handoff | Local indexing; embeddings only if enabled |
| Failure mode | Silent omission in summary | Retrieval miss or bad proposal, measurable |
| Debuggability | Inspect one JSON file | Inspect evidence, records, retrieval run |
| Best use | Small/one-off transfer | Long-lived project across many sessions |

The most efficient practical design is therefore not “replace JSON with a
vector database.” It is:

```text
immutable evidence + structured current state + episodic history
    + FTS5/metadata retrieval + optional local vectors
    -> small validated task package
```

This retains the simplicity of the current JSON transfer format while avoiding
the cost and information loss of repeatedly sending the entire history.

## Acceptance criteria for adding embeddings

Do not add embeddings merely because they are architecturally fashionable. Add
them only when a benchmark shows that FTS5 plus metadata fails on meaningful
paraphrased queries, and require:

- measurable recall improvement on the Continuum fixture corpus;
- no regression in current/superseded filtering;
- reproducible model/version metadata;
- offline operation after model installation;
- bounded indexing latency and storage;
- package-level task-resumption improvement in a fresh session.

