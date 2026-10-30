# Lean reassessment — 2026-10-03

<!-- //ai: measured assessment and proposals, not authorization to increase budgets. -->

## Outcome

Do not raise the cap automatically when a feature fills it. First reclaim
measured capacity from non-hot-path code; then decide which capabilities belong
in each shipping profile. A useful new capability legitimately costs bytes:
reuse is not a promise of zero growth, and a fixed budget needs an explicit
product scope rather than an endless sequence of feature-sized increases.

**Recommended next implementation:** Release-only `-Os` for
`src/common/shader_compiler.c` and `src/renderer/gfx/schema.c`, followed by
existing admission, shader-dialect and preset/camera regressions and GPU gates.
The isolated combined link saves **33,472 bytes** without cutting a feature.
The two-file Release optimization is now applied; the measurements below retain
their original isolated-experiment status. See the implementation validation
record below for production-build results rather than treating those probes as
behavior/performance certification.

## Frozen measurement

Assessed HEAD: `b8cfd25`; GCC 16.2.1; default GL3.3 Release. Existing raylib
worktree modifications and the author's untracked scene were untouched.
Re-linking existing objects into a scratch binary reproduced all four gated
section sizes exactly; the production objects/binary were not replaced.

| Metric | Current | Approved cap | Headroom |
| --- | ---: | ---: | ---: |
| Payload: `.text + .rodata + .data` | 3,906,555 | 3,906,576 | 21 |
| BSS | 2,577,472 | 2,705,000 | 127,528 |

Payload sections are text 3,233,103; rodata 531,452; data 142,000 bytes.
Stored baseline payload is 3,639,995: current growth is 266,560 bytes.
The whole ELF is 4,586,736 bytes; file size is not the gated payload.

### Retained input contributions

GNU ld map inputs reconcile exactly to the gated sections and BSS. This is
more reliable than classifying only named symbols. Strings/constants can be
merged across object files: the map can charge a large shared pool to the first
input (here `main.c`). That is **not** evidence that main owns those strings.
Treat those pools globally instead of making false source-level savings claims.

| Group | Retained payload excluding pooled strings/constants |
| --- | ---: |
| raylib, including bundled platform implementation | 1,118,951 |
| renderer | 760,468 |
| ImGui and bridge | 625,747 |
| engine GUI (distinct from ImGui) | 260,843 |
| authoring tools | 189,121 |
| common | 168,622 |
| other engine | 139,665 |
| RHI/backend | 83,074 |
| scenes | 54,681 |
| audio | 18,044 |
| runtime/other | 247 |
| Globally pooled strings/constants | 449,447 |
| Alignment fill across the three payload sections | 37,645 |

These add to 3,906,555. They are retained-byte contributions, **not** guaranteed
removal savings. Dependencies, shared data, strings, and padding change on relink.
The ImGui/tool rows cannot simply be subtracted to predict an editor-free build.

Largest bundled raylib inputs include audio 354,116, textures 252,752 and models
240,736 bytes (excluding globally pooled constants). Disabling codecs/model
formats would require a resource/guest-asset capability audit; it is not a free
deduplication and is not recommended as an unreviewed shortcut.

## Isolated experiments

All probes use the existing compiler flags/objects and link with garbage
collection; results are not estimated by subtracting symbol sizes.

| Probe | Payload | Savings | Meaning |
| --- | ---: | ---: | --- |
| Shader compiler `-Os` | 3,873,819 | 32,736 | Keep capabilities; change size/speed tradeoff. |
| GFX schema serializer `-Os` | 3,905,819 | 736 | Keep capabilities; change size/speed tradeoff. |
| Both `-Os` | 3,873,083 | 33,472 | Combined measured result; headroom 33,493. |
| Cut Vulkan-dump call root in scratch main | 3,879,611 | 26,944 | Exclude a CLI capability; not a production patch. |

BSS is unchanged for the optimization probes. Cutting the dump root reduces
BSS by 32 bytes. Savings from separate probes must not be added blindly.
`-Os` can affect compilation/load latency; validate those costs and shader
output/contracts before adopting it. Runtime rendering/math remains outside
the proposed optimization scope.

The Vulkan rewrite is retained by the intended `--vk-dump-shader` handler in
`src/main.c`, despite the raylib-only runtime. Follow-up validation found that
the ES/Vulkan dump options are not registered in `src/main_parse.c`: the baseline
CLI probe launched the application and timed out rather than dumping a shader.
Thus a retained handler is not evidence of a working CLI capability. No parser
repair or feature exclusion is part of this size change. A profile decision
should explicitly restore or reject those requests, not silently ignore them.

The GPU oracle is **not purely a test fixture**: `gfx_pipeline.c` invokes it
for both audit rendering and an editor progressive preview. Removing it solely
because of its name would remove authoring functionality. Review these uses
separately before proposing any exclusion.

## Budget proposal (not adopted)

1. **Immediate capacity recovery:** apply the two measured optimization changes,
   validate, and retain the existing absolute caps/baseline. Do not sacrifice
   functionality or obscure residual failures to save bytes.
2. **Visible costs:** record before/after payload, BSS and capability/performance
   consequences for meaningful feature batches. Use retained linker inputs to
   find candidates, but accept savings only after relink measurements.
3. **Planning reserve:** target at least 32 KiB payload headroom after a batch.
   This is a proposed planning signal, not a new failure threshold or promise
   that 32 KiB covers every compiler change. A near-full build triggers review,
   not forced shortening of useful instructions or an automatic cap bump.
4. **Product profiles:** evaluate runtime/demo, authoring, and development
   diagnostic builds. Share source modules and schemas; avoid cloning the engine.
   Define capabilities before baselines, then gate every profile with explicit
   payload/BSS budgets and a report of the full authoring/distribution footprint.
   Removing tools from one profile must not hide their cost or remove them from
   the product. The existing kernel build is not an editor-free 3D runtime.
5. **Deliberate capacity decisions:** if a reviewed capability genuinely needs
   growth after reclamation, approve a bounded product/release envelope and
   rationale. Do not rebaseline or add exactly enough bytes per feature merely
   to obtain PASS. Keep toolchain-driven baseline changes distinct from growth.

Splitting profiles is larger work and should follow the immediate reclaim,
not block reported bug investigation. Engine GUI and ImGui are different
dependencies; a profile needs reference-root/entry-point analysis, explicit
unavailable-feature errors and build/test coverage before size claims.

### Metric caveats

The current blocking script sums precisely three sections, not every allocated
ELF section or the complete resident footprint. For example, `.data.rel.ro`
contains another 11,232 bytes of relocatable read-only program data, outside
this payload sum. Unwind tables, loader metadata, resources, heap allocations,
and GPU storage also have costs. Keep the existing metric unchanged during
reclamation; separately propose an allocated-image/resource-footprint advisory
and review any future metric migration explicitly. Compression of packaging
does not reduce the current payload and is not a solution to this gate.

## Evidence and validation status

Local assessment evidence is retained under `/tmp/opencode/lean_reassessment/`:
`release.map`, `attribute.py`, `attribution.json`, `probe.py`, `experiments.json`
and compile/link logs. Scratch probes are not shipping binaries or test-suite
substitutes. No caps/baselines or production compile settings changed in this
assessment. Existing admission/GPU certification belongs to the assessed HEAD;
experimental binaries have size measurements only.

### Implementation validation

The subsequent two-file `-Os` change preserves the existing CLI, serializer,
Debug settings and main renderer optimization. Caps/baseline are unchanged.
Validation evidence is kept separately under `/tmp/opencode/lean_optimization/`;
The production payload is **3,873,083 bytes**, saving **33,472 bytes** and leaving
**33,493 bytes headroom** under the unchanged cap. BSS remains 2,577,472 bytes.

- Admission: PASS.
- Headless native shader API witness: 16 ES/Vulkan outputs byte-identical between
  reconstructed baseline O3 and the production Os compiler object. This bypasses
  the absent CLI wiring, not the compiler implementation. Initial CLI timeout
  evidence is retained; those flags are not certified as working.
- Existing preset, camera-render/section-roundtrip and hosted GLSL130 gates run
  against the actual Release binary: PASS.
- Reflection and bloom gates: PASS. Feature matrix: FAIL only on the unchanged
  particles/probes/mirror residuals. GL-state: zero errors, 13 retained warnings.
- Settings/art hashes remain stable; pre-existing raylib/authoring scene untouched.

Whole headless-witness process median was 4.824 ms baseline versus 5.164 ms
optimized (one run per fixture, including startup and file I/O). This observation
is not a compiler-only benchmark, a universal speed bound or proof of unchanged
latency. No performance thresholds, size caps or known matrix residuals were
relaxed to admit this change.
This is capacity recovery, not adoption of the proposed profile/reserve policy.

The offline reporter has author real-use evidence from two local JSON/PNG
reports. The author also confirmed the SSGI fix is stripe-free in the live scene.
After capacity reclamation, investigate the 3D-label-position and lens-flare
reports, then resume the proposed SSR proof batch.
