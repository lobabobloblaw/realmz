# Style-proof generation jobs

This directory defines generation intent for the 21 currently eligible
Classic references. `jobs.json` remains an immutable, zero-call statement of
intent; actual attempts, outputs, and model provenance are recorded separately
in `generation-receipts.json` and `operational-call-log.json`. Run the output
validator for the current generated/rejected/pending totals. The planned
24-asset proof remains incomplete and approval-ineligible because
the three repository-bundled City references are version-unverified and may be
used only for audit. Do not generate from them or invent authorization. They
must be replaced by references extracted from a SHA-256-gated, authorized Mac
7.1.2 City baseline.

## Files and immutability

- `style-spec.json` is the shared painterly dark-fantasy art direction and
  deterministic prompt-assembly contract. `jobs.json` binds it by byte SHA-256.
- `jobs.json` contains exactly 21 immutable pending jobs and exactly three
  refusals. Every job binds a pack-aware `ResourceKey`, Classic payload digest,
  decoded PNG digest, semantic constraints, dimensions, alpha handling,
  policies, prompt components, and expected output path.
- `jobs.schema.json` validates the fail-closed shape and pending/zero-call state.
- `variant-reference-policy.json` defines the separately hashed dependency and
  reference-input rules for item variants without changing historical style or
  prompt hashes.
- `prompt-amendments.json` retains the original hash-locked pre-call
  corrections already bound into UI-material and waterfall history.
  `masked-topology-amendments.json` and its separately locked schema add
  measured Classic-alpha corrections only for the still-uncalled jobs 09, 10,
  12, 18, and 19. Keeping the two registers separate preserves every existing
  job, prompt, and receipt hash.
- `generation-receipts.json` records actual art attempts, output provenance,
  post-processing chains, evidence, and review state.
- `operational-call-log.json` records provider failures that produced no art and
  therefore consume no generation ordinal.
- `raw/`, `outputs/`, `postprocess/`, and `review/` contain the immutable raw
  attempts, validated masters, deterministic intermediates, and review copies.
  Never update `jobs.json` statuses or its null `model_provenance` fields after
  a run.

The `input.path` fields are relative to a fresh generation-input handoff, not
to this directory. Before any model receives an input, build that handoff in a
new directory and run the required preflight:

```sh
python3 scripts/prepare_style_proof_references.py --root . \
  export-generation-inputs \
  --references assets/remastered/style-proof/classic-references \
  --output /new/generation-input-directory

python3 scripts/prepare_style_proof_references.py --root . \
  verify-generation-inputs \
  --output /new/generation-input-directory
```

The combined Classic contact sheet is an audit artifact, never a model input.

## Item bases and variants

Every production item job must be explicitly classified as standalone, base,
or variant before generation. A variant uses the optional `variant_of` job
field to identify one exact pack-aware base `ResourceKey` and its job. Numeric
proximity and unidentified-item display aliases are evidence only; neither may
silently create a visual-variant relationship.

The pre-call gate for a variant must refuse to emit a model call unless the
base master is human-approved, its approval predates the call, and both files
match their locked hashes. The model receives exactly two individually labeled
references in this order:

1. the variant's own locked Classic PNG, preserving its distinguishing traits;
2. the approved remastered base-object master, preserving shared identity,
   geometry, viewpoint, and material language.

The ordered role/path/hash records are content-hashed as one reference set and
must be copied into model provenance and the generation receipt. A missing,
rejected, merely generated, stale, or hash-mismatched base blocks the variant.
Receipts alone are not permission to infer that the model saw both images; the
actual ImageGen call must use every path emitted by the pre-call gate.

Immediately before dispatch, run the gate with the intended RFC 3339 call
start time and the freshly verified generation-input handoff:

```sh
python3 scripts/prepare_item_variant_call.py --root . \
  --input-dir /new/generation-input-directory \
  --job-id ITEM_JOB_ID \
  --call-started-at 2026-08-13T02:30:00Z
```

For a variant, `referenced_image_paths` in the canonical JSON output contains
the only permitted ordered pair of ImageGen inputs. Pass both paths verbatim,
then bind `generation_references` and `reference_set_sha256` into provenance.
The command exits nonzero without emitting a usable call record when any gate
fails. Full-catalog job/receipt manifests may be selected explicitly with
`--jobs` and `--receipts`.

The current job 13 (`cicn:6106`) has only historical rejected style-proof
attempts. Catalog evidence makes it a candidate `base_plus_effect` variant of
`cicn:6100`; any future production attempt must wait for an approved 6100 base
master and use both references. Job 14 is standalone. Jobs 15 and 16 are spell
animation frames and require separate animation-sequence metadata; animation
frames are never item variants. City relationships remain blocked until the
authorized Mac 7.1.2 baseline is available.

## Canonical prompt assembly

For each job, resolve the eight names in
`style-spec.json/deterministic_prompt_assembly/component_order` exactly in that
order:

- `style_prompt`: the shared string from `style-spec.json`.
- `subject`, `composition`, `background`, and `negative`: the corresponding
  string under the job's `prompt_components`.
- `semantic_preservation`: join the job's array verbatim with `; `.
- `policies`: join the job's `seams`, `text`, and `baked_lighting` values, in
  that fixed order, with `; `.
- `global_negative_prompt`: join the shared array verbatim with `; `.

Join the eight nonempty resolved strings with `. `. Do not normalize Unicode,
trim, reorder, append punctuation, or otherwise rewrite. SHA-256 is computed on
the assembled UTF-8 bytes. A generation receipt must store both that exact
resolved prompt and its digest. Every shared exclusion and job-specific
`negative` component is deliberately written as an explicit `Do not include`
imperative; runners must not strip or paraphrase those instructions.

For a job in either reviewed amendment register, append its exact amendment
text after the immutable eight-component base and before any ordinal-1 retry
supplement. Use `scripts/prepare_style_proof_prompt.py` before dispatch; it
fails closed if either register is absent or stale, if the job has already
consumed its call, or if measured Classic alpha differs from the topology
evidence. Receipts bind the particular register hash in
`prompt_amendment_provenance`, so the earlier four-job register and its
historical receipts remain unchanged.

## Alpha and output rules

Alpha mode comes from measured decoded PNG coverage, not merely the legacy
manifest's alpha-policy label. Portraits 05–08 and waterfall 20 are fully
opaque. Jobs 09–16 and 18–19 use binary source masks. For those masked jobs,
generation occurs over flat `#FF00FF`; the upscaled original binary mask is
authoritative, and chroma removal is permitted only outside it. Never key or
tolerance-remove pixels inside the mask: pink wings and magenta elemental
energy are gameplay content.

The locked job 19 prose predates a pixel-level alpha audit and describes
openings that the decoded resource does not actually encode. Its 32×32 alpha
has one 681-pixel connected foreground, a `(4,0)`–`(26,31)` bounding box, and
zero enclosed transparent components. Therefore Remastered job 19 preserves
that solid binary footprint: bar gaps and inset depth are opaque dark recess
paint, and `#FF00FF` is allowed only outside the outer gate silhouette. The
separate pre-call topology amendment is the governing correction; do not edit
the immutable job or synthesize holes during post-processing.

Generated outputs are lossless sRGB PNG masters. Text/OCR, dimensions, color
space, alpha coverage, transparent corners, seams where required, and baked
lighting must pass deterministic gates before human review. At most one
targeted regeneration is allowed for a rejected master. Neither generated nor
reviewed outputs make this proof approval-eligible while the three authorized
City replacements are absent.
