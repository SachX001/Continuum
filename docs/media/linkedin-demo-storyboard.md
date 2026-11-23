# Continuum LinkedIn Demo Storyboard

Target length: 60–90 seconds.

## 0–8 seconds — The problem

Show a Claude conversation with an initial implementation, a debugging discussion, and a corrected implementation.

Caption: “AI sessions lose engineering context when conversations end.”

## 8–20 seconds — Capture

Show the Continuum CLI detecting Claude Desktop and extracting the conversation.

Highlight the ordered messages and the extraction-failure safeguard.

Caption: “Continuum captures the conversation as local evidence.”

## 20–35 seconds — Preserve code and provenance

Show the parsed artifacts or generated compilation prompt containing two separate code artifacts.

Highlight:

- exact source content;
- `source_message_index`;
- `revision_of`;
- `supersedes_id`.

Caption: “Revisions stay separate. The current implementation does not become a merged code blob.”

## 35–48 seconds — Review memory

Show proposal listing and a review action accepting a decision or superseding an older record.

Show the current-memory view excluding superseded records while the history remains inspectable.

Caption: “Memory changes are reviewable, traceable, and reversible in history.”

## 48–65 seconds — Compile locally

Show the Ollama command and the generated validated package.

Show that exact artifacts are injected from the local evidence manifest after semantic compilation.

Caption: “Local Ollama compilation reduces repeated context transfer without sending source to a cloud API.”

## 65–82 seconds — Fresh-session result

Show a new Claude session identifying the current implementation, explaining why it supersedes the earlier version, and keeping the source blocks separate.

Caption: “A fresh AI session continues from the correct engineering state.”

## 82–90 seconds — Close

Show the GitHub repository and architecture diagram.

Caption: “Continuum — evidence-first memory transfer for engineering AI workflows.”

## Recording notes

- Use a sanitized conversation fixture; do not expose personal conversations, tokens, or private repository data.
- Keep the terminal font large enough to read in a vertical LinkedIn crop.
- Use one continuous cursor path and avoid unrelated windows.
- Export at 1080p if possible, with captions burned in or provided as subtitles.
- End with the GitHub URL and the architecture diagram for a clear recruiter takeaway.
