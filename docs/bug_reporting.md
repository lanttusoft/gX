# Offline bug reports

<!-- //ai: local diagnostics, with explicit submission and no background writes. -->

Open **Help → Report bug…** or press **F10** in GFX editor authoring mode.
Drafts remain in memory when the form closes; quitting loses them. Opening or
editing the form does not create files.

Title and description are required. The form also accepts type, severity,
reproduction steps, when/how it happened, and the affected feature. Click
**Save local report** to write a uniquely named JSON file under `bugs/`.
Repeated submissions never overwrite existing reports. The status shows the
saved path or failure. Title/description buffers hold 128/256 bytes;
reproduction steps hold 1024 bytes, other text fields 256 bytes.

## Bug status codes

Each report has a numeric `status_code`: `0` means **OPEN** (the default for new
reports and any legacy report without a code); `1` means **FIXED**, after the
reported behavior has been verified fixed. The 34 reports present when this
field was introduced were initialized to `0`; no issue is marked fixed based
only on a suspected code change. Severity remains separate from status.

List, filter, summarize or change statuses without rewriting the report contents:

```bash
python3 scripts/bug_status.py list
python3 scripts/bug_status.py list --status OPEN --severity Medium
python3 scripts/bug_status.py list --type Rendering --no-feature
python3 scripts/bug_status.py summary
python3 scripts/bug_status.py summary --json
python3 scripts/bug_status.py set report-1791055042-0.json 1
```

`list` filters combine with AND; `--status` accepts `OPEN`/`FIXED` or `0`/`1`,
and severity/type matching is case-insensitive. `--feature` selects reports
whose title starts with `[FEATURE REQUEST]`; `--no-feature` excludes them.
`summary` is read-only and reports totals plus severity/type counts for all
reports and for open reports; `--json` emits the same counts for scripts. It
never prints titles, descriptions or paths.

`python3 scripts/bug_status.py initialize` is an idempotent migration for new
legacy reports: it adds `0` only where the field is missing and preserves
existing codes. Status edits preserve legacy indentation and reject duplicate
top-level `status_code` lines. Reports remain local and gitignored, including
their status changes.

## Triage conventions

A report becomes **FIXED** only after the behavior is verified against current
code or release notes; cite that evidence in the private triage log. Severity
describes the original impact and is not rewritten when a fix lands.

Feature proposals are titled `[FEATURE REQUEST]` and stay **OPEN** until the
capability exists; they are not bugs and must not be closed on intent alone.
The form's type list has no proposal category, so they keep `type: Other`.
Use `--feature` / `--no-feature` to separate them, and read
`open_feature_requests` from `summary` when counting open defects.

Reports include UTC submission time, the runtime engine-version label, the
build revision, and the preset path. The label comes from
`gX_Data_String_gX_version` and must match `readme.txt`. The revision is a
best-effort 12-hex Git prefix generated at build time, with `-dirty` when
tracked files were modified; source archives without their own Git root report
`unknown`. It is context, not a substitute for the release tag and archive
SHA-256. A preset path is context, not a dump of unsaved scene edits.

## Choosing severity

Severity describes **impact**, not frequency or annoyance. Frequency belongs in
the description. These labels help triage; they do not promise fix order.

| Severity | Meaning | Example |
| --- | --- | --- |
| **Low** | Cosmetic/minor inconvenience; the task still works. | Misaligned label or harmless tooltip typo. |
| **Medium** | Feature impaired; a practical workaround lets you continue. | Broken control with an alternative editing route; rendering artifacts without blocked work. |
| **High** | Core task blocked without a practical workaround, or repeated crashes prevent normal work. | Cannot load a valid scene or save new work; repeated crashes during ordinary editing. |
| **Critical** | Existing data lost/corrupted, or a security/privacy risk. | Saving destroys an existing scene or overwrites unrelated files. |

A rare destructive save can be Critical; a constant cosmetic glitch can be Low.
A crash is not automatically Critical: describe lost work, recurrence, and any
workaround. If unsure, choose **Medium** and describe the impact; triage can
revise it. Suspect corruption? Stop repeating the action and protect copies
of affected files before investigating.

## Writing a useful report

Report **one issue per submission**, with a specific title rather than “broken.”

1. **Context:** scene/preset, settings/effects, exact Git revision or release if
   known. Mention unsaved edits; they are not captured.
2. **Steps:** numbered clicks/keys in order, starting from the initial state.
3. **Expected versus actual:** what should happen, and what happened instead.
4. **Frequency and impact:** always/intermittent/once, work blocked/lost, any
   workaround. Say if reproduction is unknown.
5. **Evidence:** optional screenshot for visual issues, reviewed for privacy.
   Logs are not automatically collected.

Example: “SSGI bands on flat ground”; load a named preset/camera, enable SSGI;
expect smooth indirect light, observe horizontal bands; happens every time,
disabling SSGI works around it. Medium fits usable authoring; High may fit if
it blocks the required rendering task.

“Crash” is a type; severity is its impact. Choose “Other” if no type fits.
Missing capabilities are feature proposals unless documented existing behavior
is broken; this prototype has no separate proposal workflow.

## Screenshots and privacy

**Attach diagnostic state** is also off by default. On submission it records
the active camera (position/target/up/FOV/projection), dock viewport rectangle,
selected-model index (`-1` means none), renderer flags, SSAO/GTAO/SSGI and SSR
tuning, CSM/reflection, godray/fog/lensflare and bloom settings, plus compositor
switches. It includes scene name/description and preset timestamp metadata;
inspect these for private context before sharing.

This is an in-memory snapshot of current values, not a scene/resource export.
Models, lights, textures, resource paths/contents, arbitrary memory, logs,
shader buffers, crash state and simulation history are not included. Selection
indices are local to the current scene, not persistent asset identifiers.
No temporary preset files are created. The report and preset/camera-list saves
share schema encoders; renderer changes are not made to collect the snapshot.

Screenshots are **off by default**. Opting in saves the current framebuffer
as a PNG alongside the JSON. It may contain UI or private scene content;
inspect it before sharing. Its dimensions match the actual framebuffer,
not necessarily the requested render dimensions.

Attachment failure is recorded even if the text report succeeds. Write
failure can leave a partial report or attachment; inspect the displayed path.
Files are not automatically deleted or retried. No uploads, telemetry,
autosaves, resource copies or program dumps are performed. `bugs/` is
gitignored to protect private reports.

## Validation and next steps

`make gfx-bug-report-check` exercises the real writer through an opt-in Debug
fixture: escaped/Unicode text, repeated saves, screenshot attachment,
blank/invalid-option rejection, blocked-directory failure and a no-write
draft. It also checks diagnostic opt-in, live values, the build revision and
the exact top-level allowlist to catch accidental scene/asset export. It writes
to `/tmp/opencode/bug_report_check/` and preserves settings
and the legacy editor screenshot. The hook is compiled out of Release.
This gate is included in `make check`.

1. Validate the writer, failure paths and existing admission/GPU gates.
2. Author tests Help/F10, submits a real report and inspects its screenshot.
3. Review missing reproduction context before expanding the format.
4. Decide feature-proposal handling, richer revision metadata and consent-based
   feature dumps separately; none are automatic in this prototype.

SSR remains the next proposed screen-space proof batch after prototype
signoff: reconstruction/reflected rays → traversal/brackets → hit/fallback
contracts → asymmetric native/GPU evidence.
