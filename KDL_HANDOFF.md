# KDL 2.0 implementation handoff

Goal: Add KDL 2.0 on `feature/kdl2.0`, following Glaze's format APIs, reflection, buffer/error handling, formatting and tests.

Status: Implementation and documentation complete. Changes remain uncommitted on the feature branch.

Implementation:
- Standalone `glaze/kdl.hpp`, `KDL` format ID, `set_kdl`, standard read/write dispatch and file helpers.
- Native document model with optional node/value annotations (including empty tags), ordered arguments/children and last-value-wins properties. Out-of-range numbers retain validated text.
- UTF-8 validated KDL 2.0 parser, canonical writer, nested comments/slashdash, continuations, raw/multiline strings and Unicode escapes/whitespace. Nesting capped at 256.
- Native mappings for reflected objects/metadata, string keyed maps, scalar/nested sequences, enums, nullable values. No JSON conversion. Mapping boundaries documented in `docs/kdl.md`.
- Empty KDL documents accepted by generic dispatch; other formats retain their prior empty-input behavior.
- CMake unit target and optional upstream conformance target; README and MkDocs navigation updated.

Validation:
- Clang Debug with ASan/UBSan: KDL unit tests and all 338 upstream corpus cases pass, including decoded semantics and writer round trips for 243 valid documents.
- GCC C++23 strict-warning unit build; all six new public headers checked for standalone Clang compilation.
- JSON/TOML/YAML sanitizer regression targets pass.
- `clang-format --dry-run --Werror` and `git diff --check` pass.

Reproduce: build directory `build/kdl`, corpus `/tmp/kdl-main/tests/test_cases` (downloaded from kdl-org/kdl main). Configure `-Dglaze_KDL_TEST_SUITE=/path/to/kdl/tests/test_cases` to enable `kdl_conformance`. Run `ctest --test-dir build/kdl -R '^(json_test|toml_test|yaml_test|kdl_test|kdl_conformance)$' --output-on-failure`. LeakSanitizer process inspection requires running CTest outside this session's sandbox; its narrowly scoped command prefix has been approved.

Next: No implementation work remains. Review or commit when requested.

Follow-up completed: C++26 static reflection with local GCC 16.2.1.
- KDL reuses the existing shared P2996 backend for non-aggregate/private object members.
- Added numeric/object C-style arrays and multidimensional array traversal, avoiding array-to-pointer decay during nullable checks; explicit metadata also enables these arrays in C++23.
- Added `reflect_enums` support throughout scalar, nullable and sequence reads/writes, preserving explicit enum metadata precedence and numeric fallback when disabled.
- Added conditional `kdl_reflection_test`; documented compiler flags, options and CMake commands in `docs/kdl.md`, linked from the P2996 guide.
- GCC reflection CMake build: `build/kdl-gcc26` (`CMAKE_CXX_STANDARD=26`, `CMAKE_CXX_FLAGS=-freflection`, `glaze_ENABLE_REFLECTION26=ON`). KDL unit/conformance/static-reflection and original `p2996_test` all pass (4/4 CTest targets).
- Automatic feature detection confirmed separately without defining `GLZ_REFLECTION26`: 4 static-reflection tests, 39 assertions pass.
- Ordinary Clang C++23 ASan/UBSan KDL unit and all 338 official conformance cases pass (2/2 targets); formatting and whitespace checks pass.

No follow-up implementation work remains. Changes remain uncommitted on `feature/kdl2.0`.

Follow-up complete: Default typed KDL reads/writes now avoid an AST/DOM.
- `parser::body` and `nodes_each` share grammar validation between direct consumers, explicit DOM reads, and subtree skipping. Slashdashed nodes/entries/children and unknown/meta-skipped subtrees are parsed without building nodes.
- `read_direct.hpp` binds entries to reflected members/maps/sequences as consumed; stack-only schema field flags and counters; nullable and fixed/dynamic/proxy sequences supported. `read_scalar` retains the scalar conversion rules. Successful earlier assignments can remain on error, matching direct-reader conventions.
- Typed writer emits fields/arrays directly to the caller buffer, with no intermediate document. Explicit `kdl::document` continues to select DOM behavior. No mode flag is needed.
- Comments are accepted by default: `//`, nested `/* ... */`, and KDL `/-`. String literals retain comment markers. Ignored components still receive KDL grammar validation.
- Tests added for typed/file comments, skipped malformed syntax, error offsets, duplicates/required fields, nullable sequences, list and inplace_vector, typed/skip depth limits and context reuse.
- `kdl_direct_test` verifies zero heap allocations reading numeric configuration into preallocated targets and writing into a fixed buffer; input includes a 100 KB comment and 20,000 discarded/unknown nodes.
- Upstream conformance now runs all 338 cases through both DOM and the direct skip path; valid DOM values and writer round trips are still checked.
- C++23 Clang ASan/UBSan: 3/3 KDL CTest targets pass. GCC 16.2 C++26 reflection: all KDL plus original P2996 tests pass. All seven KDL public headers compile standalone; clang-format and diff whitespace checks pass.
- Documentation updated with default direct mode, supported comment examples, allocation boundaries, and partial-update/type-validation behavior.

No implementation work remains; changes are uncommitted on `feature/kdl2.0`.

## Expanded follow-up completed (2026-10-27)

User requested BOTH mixed modes, all error styles and the entire feature checklist.
All implemented; no remaining task or running build. No agents used; changes remain
uncommitted on `feature/kdl2.0`.

- `kdl::mixed<Arguments, Properties, Children>` independently binds positional
  arguments, properties and child nodes; defaults are dynamic value/properties/document.
  Optional node annotation is retained. Native model types opt out of reflection.
- Typed objects can embed `kdl::node`, `document`, `value`; node also works as a
  standalone root (exactly one node required). Only explicitly dynamic sections
  materialize DOM data; ordinary typed and skipped paths still avoid an AST.
- `std::tuple`, `std::pair`, `glz::tuple` positional arguments; owning nullable
  pointers including `unique_ptr` to tuple/variant/node and scalar types.
- Node `std::variant` scans the discriminator then parses the chosen alternative
  directly. Accepts `(tag)` or `type` property (custom key via meta.tag), uses
  meta.ids or name_v defaults; writing uses annotation unless meta.tag is defined.
  Both selectors must agree; last discriminator property wins, including when
  an earlier duplicate is non-string. Scalar-position variants support annotated
  IDs or first compatible scalar alternative and preserve IDs on write.
- `is_reflect_enum` / `meta<Enum> : glz::reflect_enum` works with P2996 and ordinary
  KDL APIs without per-call option macros. Explicit enum metadata keeps priority.
- Typed and DOM writers honor indentation_char and indentation_width.
- All Glaze error styles: error_ctx, expected, format_error, std_error_code adapter,
  generic options/context APIs and named `glz::ex::*_kdl` throwing wrappers, including
  files and raw-buffer byte counts. Exception wrappers available standalone and via
  glaze_exceptions.hpp; disabled under -fno-exceptions.
- Fixed existing generic raw ex::write recursion / wrong return handling in
  core_exceptions.hpp, now delegates to glz::write and returns error_ctx.count.
- New kdl_mixed_test and kdl_exceptions_test CMake targets cover round trips,
  comments, duplicates, invalid selectors/values, required/unknown fields, pointers,
  missing/extra tuple arguments, tabs/spaces, buffer/file exceptions and umbrella APIs.
- docs/kdl.md documents all mappings, reserved discriminator, partial updates,
  supported roots, errors and examples; docs/exceptions.md and P2996 guide updated.

Final validation:
- Clang C++23 ASan/UBSan: 9/9 KDL + exceptions + JSON/TOML/YAML tests pass.
- GCC 16 C++26 -freflection: all six KDL tests + original p2996_test pass (7/7).
- Reflection test compiled separately WITHOUT GLZ_REFLECTION26 definition:
  5 tests / 45 assertions pass, including per-enum marker and pointer.
- All 338 upstream conformance cases still pass through DOM and direct skip;
  valid document semantics and writer round trips verified. Direct preallocated
  numeric read/write allocation checks still pass.
- Eight public KDL headers compile independently under Clang23; exception header
  also checked with -fno-exceptions. clang-format and git diff --check pass.

Builds/corpus locations and sandbox CTest note are recorded above.
Next: no implementation work remains; review/commit only when requested.

## Active SIMD request (2026-10-27)

User requested KDL SIMD support. No agents authorized. Plan: add bounded, portable
ASCII run scanning with compile-time SSE2/AVX2/AVX512BW/NEON64/WASM selection,
reusing Glaze detection and GLZ_DISABLE_SIMD. Integrate validation, strings,
comments, whitespace/tokens and writer escaping; scalar Unicode/error handling
remains authoritative. Add differential/boundary tests and reproducible benchmark,
run sanitizer + scalar + native ISA + GCC26 conformance/regression. Work in progress.

Expanded active scope: user also requires chrono/filesystem::path and full glz::custom
metadata / custom_read / custom_write integration. SIMD scanner now integrated in
parse/write, preliminary old KDL/conformance tests passed. New standard/custom/scan
tests just added and building in build/kdl. shared core/custom read dispatch and
write_wrappers unwrap reused. Low-level scalar from/to adapters added (public root
contract guarded via top_level_supported), custom readers receive original source
entry slices from parser body. Need resolve new test build/run failures; add docs,
SIMD benchmark and run full sanitizer/scalar/native/GCC26. Check custom nullable
getter skip_null_members currently likely failing; unwrapping field before omission
needed. Chrono duration units read uses checked scaling; calendar shared helpers;
epoch duration_cast needs range-safe conversion review. No agents.

## New expanded scope from user (pending after current three features)
User also requests borrowed string_view/raw_string, streaming/chunked refill,
lazy<T>, KDL path addressing, diff/patch, schema export, query language, RPC,
text minify/prettify, perfect-hash field dispatch. Async question sent whether
ecosystem tools may explicitly adapt to existing JSON-bound modules or must
operate natively; parser itself remains native either way. Do not abandon current
SIMD/chrono/custom verification. Latest GCC26 build failed: read_state<tuple<>>
evaluates object<T> through core's tuple first-element concept and gets hard error;
fix by excluding positional tuples before object detection. Also static_assert
!read_supported<wrapped_count,KDL> differs under P2996; inspect/document_type
wrapper resolution and precedence. C++23 full build passed before latest exact
duration helper. Builds idle. New chrono.hpp helper currently exact decimal
integral conversion limited to uint64 mantissa; consider decimal long arithmetic
or explicit coverage. Scan microbench initial SSE2/scalar: longstrings ~7x, comments
~9x, writes~8x; need final sequential AVX2/SSE2/scalar timing.

User clarified ecosystem requirement: ALL tools must operate directly on native KDL; NO JSON adaptation layer. Implement native node/argument/property/tag semantics for diff/patch, queries, RPC, schema. Preserve this explicit requirement.

Current advanced work (in progress):
- Core hash field dispatch now uses decode_hash_with_size<KDL,...> + visit + exact
  key comparison; no JSON conversion.
- scalar_token internal borrowed representation added in token.hpp; single-line
  quoted/raw and bare strings borrow input, escaped single-line strings defer
  decoding through a parser-supplied thunk. Native DOM conversion materializes
  owning strings. Types/read_direct generalized scalar tokens and source spans.
- std::string_view supported for no-decode strings; default rejects decoded
  escaped/multiline forms with invalid_escape. raw_string wrapper view can borrow
  escaped source too, without allocations. Raw/escaped wrapper type and factories
  moved from json/raw_string.hpp to shared core/raw_string.hpp (JSON specializations
  remain in JSON header), preserving existing API. KDL reader/writer supports
  wrappers and raw_string option. raw writer validates pre-escaped content.
- New kdl_borrow_test and extended allocation assertions in kdl_direct_test; build
  running as PTY session 74059 in build/kdl. Must inspect result and run tests.
- Prior scalar_token (without deferred escapes/raw wrappers) full five KDL
  unit/mixed/custom/standard/conformance sanitizer tests passed.
- GCC26 issues fixed: object excludes tuples before shared reflection; local
  serialized_type resolves actual get_member lambda result rather than shared
  resolve_write_type_t (which treats lambda meta value as a reflected lambda in
  P2996). kdl_standard/custom GCC26 passed after fix.
- chrono.hpp exact duration unit conversion now uses decimal rational factor
  cancellation and portable two-word long division before native range checks;
  1000 decimal fractional inputs and near-limit/custom wide periods pass sanitizer
  and GCC26 tests.
No documentation for these latest extensions yet. Next: finish borrowed tests;
add format-neutral lazy<T> with KDL member capture, native chunked/framing reader,
then native path/format/diff/patch/schema/query/RPC extensions as explicitly
requested. Need final full tests, docs, standalone builds and sequential benchmarks.

# AUTHORITATIVE CURRENT STATE / RESUME HERE (2026-10-27)

## User authorization and current objective
Original KDL 2.0 implementation + C++26 reflection + AST-free typed parsing +
mixed modes/error APIs was complete (history above). User then explicitly asked:
1. SIMD (can reference JSON), chrono/filesystem::path, custom metadata/hooks.
2. ALSO borrowed string_view/raw_string, streaming/chunked I/O, glz::lazy<T>,
   native KDL path addressing, diff/patch, schema export, query language/JMESPath,
   RPC, text minify/prettify, perfect-hash field dispatch.
3. Clarification answered: **ALL ecosystem tools must directly operate on native
   KDL. NO JSON adaptation layer.** Core remains native/no intermediate AST for
   typed targets. Do not stop after only first three features.
No agents authorized; do not spawn. No commit requested. Continue autonomously.

## Completed code in current expansion
- scan.hpp bounded ASCII runs selects SSE2/AVX2/AVX512BW/NEON64/WASM SIMD at
  compile time via Glaze macros; GLZ_DISABLE_SIMD -> scalar. Covers validation,
  quoted/raw strings, comments, whitespace, tokens/identifiers and writer escaping.
  Public constexpr kdl::simd_backend. Writer append uses checked bulk dump.
- kdl_scan_test compares every byte at register/tail boundaries against scalar;
  protected-page and exact-sized writer tests. kdl_scan_scalar_test same tests
  with GLZ_DISABLE_SIMD; kdl_conformance_scalar added. All passed before latest
  borrow refactor. AVX2 executable passed; AVX512BW compile-only passed (host lacks
  AVX512). ARM/WASM code not executed.
- Standalone kdl_benchmark.cpp + optional glaze_KDL_BUILD_BENCHMARK target. Initial
  SSE2 vs scalar release results ~7x long-string read, ~9x comment read, ~8x
  long-string write; small-config approximately unchanged. Need final sequential
  rerun SSE2/AVX2/scalar and report exact honest numbers.
- chrono/path and glz::custom support in types/read_direct/write. Shared
  core/custom read dispatch and unwrap_write_value reused, getter invoked once;
  nullable omission after unwrap; const/mutable forwarding through maps/variants.
  Native chrono calendar helpers reused. Durations read bare native counts OR
  unit strings ns/us/µs/ms/s/min/h/d/w. Writes bare native counts (JSON parity).
  chrono.hpp does exact integral decimal rational conversion with gcd and portable
  two-word long division (no floating rounding); overflow/fractional count rejected.
  Date/time/steady-clock/epoch wrapper support; epoch cast checked for overflow.
- kdl_standard_test checks 1000 decimals + int64/uint64/wide custom period limits,
  chrono/path round trips/errors. kdl_custom_test checks getters/setters/lambdas,
  error contexts, skip/no-input, mixed props, wrapped scalar, low-level custom
  from/to<KDL,T>, mutable getter once, null omission. Both passed Clang sanitizer
  and GCC26 before deferred-escape borrow refactor.
- Parser body supplies original entry source spans to custom scalar readers.
  Low-level native scalar to/from adapters added, with top_level_supported guard
  keeping document-shaped roots. Native custom scalar from hooks receive original
  entry bytes (incl annotations); standard pointer/iterator signature works.
- C++26 fixes: object excludes tuple/pair/glz tuple BEFORE calling shared
  reflectable (tuple<> otherwise hard-errors). Local serialized_type resolves
  actual get_member(meta wrapper) rather than shared resolve_write_type (that
  helper mistakenly treats lambda meta value as a reflected lambda in GCC26).
- Field dispatch uses shared JSON hash_info/decode_hash_with_size<KDL,...> +
  visit + exact key equality, replacing for_each linear scan. Need dedicated
  large-schema and hash collision/unknown-key tests/benchmark.

## Latest borrowed-string work (JUST COMPILED, tests in progress)
- New token.hpp internal scalar_token variant adds string_view (public kdl::value
  remains owning). Parser body emits tokens; numeric/scalar consumers generalized
  via const auto& and text(v). Native DOM converts tokens to owning values.
- Bare identifiers and single-line quoted/raw literals borrow original input.
- Single-line escaped strings validate escape grammar with stack-only scratch
  and retain raw span + decoder thunk. text(v) lazily decodes into token-owned
  scratch only if an owning/semantic consumer needs it. Raw wrapper views bypass
  decoding. DOM moves decoded string into its storage. Multiline currently falls
  back to original owning/dedent parser.
- std::string_view accepts no-decode literals; escaped/multiline default views
  return invalid_escape (no dangling scratch); non-string -> syntax_error.
- raw_string_t/escaped_t definitions and glz::raw_string/escaped factories moved
  from json/raw_string.hpp to NEW core/raw_string.hpp. JSON header still contains
  JSON specializations, imports core declarations. KDL types includes shared core
  header (no JSON import), supports wrapper/opts reads and writer raw literal
  validation without re-escaping pre-escaped bytes. Must run JSON regression to
  verify shared header move.
- New kdl_borrow_test checks pointers, long/Unicode/plain/raw/bare/nullable/sequence,
  raw escaped view and write validation. kdl_direct_test adds zero-allocation
  assertions for 64KB borrowed strings and 32KB raw escaped view.
- Latest build `cmake --build build/kdl --target kdl_borrow_test kdl_direct_test
  kdl_test kdl_custom_test kdl_conformance -j 3` finished successfully (session74059).
- Running CTest session **90852**: kdl_test/direct/borrow/custom/conformance. Last
  observed kdl_test passed and direct started. Poll it and resolve any failures.
- Prior basic token refactor (before deferred escapes/raw wrapper) 5/5 sanitizer
  KDL unit/mixed/custom/standard/conformance passed.

## Still REQUIRED / suggested implementation path
A. Finish borrowed tests, GCC26 + JSON regression after shared header move; later
   docs must remove obsolete borrowed/custom unsupported claims.
B. Add format-neutral core/lazy.hpp glz::lazy<T> (no existing lazy<T> in repo;
   JSON actually has lazy_json_view). Suggested cache optional<expected<T,error_ctx>>,
   source span + reader thunk, .get() parses on demand and returns expected ref;
   raw input lifetime explicit. Native KDL member capture: nodes_each can expose
   node begin offset as optional third callback arg; skip_body validates without
   materializing; record entire raw node and thunk parse exactly one node into T.
   Writer should preserve unmaterialized span/body or explicitly handle get.
C. Native streaming/chunking. Existing parser requires complete string_view; do
   NOT claim streaming just by accumulating entire document. Suggested incremental
   top-level node framer maintaining lexical state (nested comments/raw/multiline
   strings/continuations/braces/Unicode newline) across feed boundaries; retain at
   most current node, dispatch complete node through existing direct grammar,
   preserve state across nodes incl seen/missing fields, or stream reader returning
   individual nodes/typed records. Include istream/refill adapters, push chunks,
   errors offsets, split-every-byte tests and bounded-memory tests. Borrowed/lazy
   targets must not dangle on refill (reject or explicitly own per-record backing).
D. Native KDL path API: /server/host child lookup plus explicit argument/property
   selectors and index for repeated node names. Should support source-span seeking
   without full DOM, read_as_kdl native mapping and pointer get/set for DOM.
E. Native minify/prettify strings; parser+DOM canonical writer acceptable initially
   but must preserve KDL semantics/tag/order/repeated nodes and validate errors.
F. Native diff/patch operations covering name/tag/args/props/children and ordered
   repeated nodes; transactional apply, tests. No JSON conversion.
G. Native schema export from C++ to KDL schema. Need inspect official KDL schema
   draft (specific/niche -> browse authoritative spec when implementing), handle
   reflection, mixed, nullable, enums, variants and recursive refs.
H. Native query language/JMESPath-like navigation/filter/projection over KDL model.
   Existing JSON jmespath.hpp only ~1183 lines, mostly path/slices, not full query
   language. Can reuse generic tokenization by extracting shared header, but no
   JSON model bridging. Document supported KDL query syntax truthfully.
I. Native RPC dispatch via reflected member functions/metadata, KDL args/properties
   -> typed parameters, result serialized as native KDL response; do NOT send any
   network calls/messages. Existing JSON invoke.hpp small, could move shared
   invoke_t type declarations to core but native RPC endpoint also needed.
J. Large-schema hash tests; docs/API examples for every requested feature; final
   ASan/UBSan all KDL + JSON/TOML/YAML/exceptions, GCC26+P2996, scalar conformance,
   SIMD AVX2 run/AVX512 compile, standalone headers, format/diff checks.

## Environment / commands
Repo /home/yk/Code/glaze-kdl2.0-, branch feature/kdl2.0, all changes uncommitted.
Build dirs build/kdl (Clang C++23 debug ASan/UBSan -fno-exceptions regular targets),
build/kdl-gcc26 (GCC16.2 C++26 -freflection GLZ_REFLECTION26=1).
Corpus /tmp/kdl-main/tests/test_cases (338 cases, 243 valid).
ctest --test-dir build/kdl ... requires sandbox_permissions=require_escalated for
LeakSanitizer ptrace, existing approved prefix. Keep user commentary <=60sec.
No doc updates yet for current three/advanced requests (docs/kdl.md still older).
No skills needed. Keep this handoff current. No agents.

LATEST RESULT: session90852 completed, all five latest sanitizer targets PASS,
including kdl_borrow_test and zero-allocation kdl_direct_test after deferred escapes
+ raw wrapper refactor. No running builds/tests remain. Resume at item B (lazy)
while scheduling GCC26/JSON regression from item A.


## Current extension progress (resume after earlier authoritative state)
Implemented and initial Clang sanitizer tests passed:
- core/lazy.hpp glz::lazy<T>, borrowed validated node spans with cached typed get(),
  explicit input lifetime, const mutable cache, typed writer materialization. lazy
  supports node members (not scalar-position properties/tuple elements).
- kdl/stream.hpp incremental top-level framing, feed/finish + istream read_stream,
  shared-backed stream_record<T> protects borrowed/lazy targets after refill.
  No DOM for typed records. Full 338 official cases pass 1/7/8192-byte chunks,
  including slashdash newline/comment and CRLF continuations. Tests bounded record
  buffering and every split size. Record framer holds current node and feed chunk.
- kdl/path.hpp DOM get/set, source get_view/read_at with native node/arg/prop paths,
  repeated-name [index], escapes; DOM :name/:tag supported. Source metadata views
  not implemented. kdl/format.hpp native semantic minify/prettify.
- kdl/patch.hpp index-addressed ordered native diff/transactional apply_patch;
  write_patch/read_patch serialize native KDL, edits all node sections/children.
- kdl/query.hpp native compiled path queries: wildcards, // descendants, filters,
  comparisons + &&/||/!, Python slices/negative indices, projections, count/first/
  last/reverse pipelines, query_kdl wire output. Not official CSS KQL or full
  JMESPath; document supported dialect honestly. First test passed; numeric/slice
  normalization fixes made since last build, need retest.
- kdl/rpc.hpp local rpc_registry binds member functions explicitly/metadata;
  positional/named/structured params, typed native result, id, error responses.
  Request grammar validated before invocation. Clang rpc test passed.
- kdl/schema.hpp native Glaze binding schema with recursive registered type refs,
  mixed/nullable/variant/enum/sequence/custom/chrono info; custom dialect, not
  official KDL Schema 1.0 (official spec read via web). Compile/test mostly passed;
  test enum spelling expected bare strings incorrectly; fixed to quoted, retest.
- kdl_hash_test generates 128-field object + 1000 unknown/collision probes. Built,
  not yet run. Current hash code reused existing shared hash dispatch.
- Shared raw_string move verified JSON sanitizer json_test PASS.

Running: GCC26 new targets build session42162 (poll). No agents.
Need: rebuild query/schema, run all new tests; fix GCC26; review new APIs including
query edge cases, source path metadata restrictions, chrono/custom exact behavior;
update docs (currently stale), full sanitizer/regression/scalar/reflection checks,
standalone headers/format/diff, final SSE2/AVX2/scalar benchmarks. Build streams and
conformance changes integrated into kdl.hpp. All changes uncommitted.


## FINAL authoritative completion (2026-10-27)
All authorized extensions now implemented, documented and validated. No running
builds/tests remain, no agents used, no commit made. Branch feature/kdl2.0.

Final code fixes since prior entry:
- RPC scalar parameters reuse direct_reader argument/finish, so nullable structured
  parameters accept #null and child-node objects without template hard errors.
- Source read_at<node> empty path correctly enforces exactly one root node.
- Queries handle numeric type comparisons, extreme negative indices/strides and
  Python reverse-slice clamping. Tests extended.
- Static-reflection schema enum/private member tests added.
- A malformed RPC editing replacement temporarily broke compilation; restored
  the parsing callback, rebuilt and all final checks below passed AFTER the fix.

FINAL verification:
- Clang23 C++23 ASan/UBSan: 22/22 CTest targets PASS: all 18 KDL targets plus
  exceptions_test, json_test, toml_test, yaml_test.
- GCC16 C++26 -freflection: 20/20 PASS: all KDL targets + p2996_test.
- Reflection auto-detection, no GLZ_REFLECTION26 definition: 6 tests / 47 assertions.
- All 338 official cases through DOM, AST-free skipping AND 1/7/8192-byte streaming
  chunks; both SIMD and scalar conformance targets pass.
- 20 public KDL/core/exception headers compile independently under Clang C++23.
- AVX2 scan test: 3 tests / 912 assertions pass; AVX512BW compile-only passes.
  ARM NEON/WASM unexecuted locally (x86 host).
- clang-format dry-run and git diff --check pass. Zero-allocation direct/buffer/
  borrowed/raw tests still pass; 128-field and 1000 unknown-key hash tests pass.

Documentation: docs/kdl.md now covers SIMD, standard/custom bindings, borrowing,
lazy, chunked/istream records, paths, native diff/patch/format, queries, schemas
and local RPC. Important explicit API scope:
- Queries are documented native JMESPath-inspired KDL syntax, NOT full JMESPath
  or official CSS KQL. Supports //, filters, boolean operators, slices/projections,
  count/first/last/reverse. All operate native model; no JSON adaptation.
- Schema is versioned native `glaze-kdl` binding-schema dialect, NOT advertised as
  KDL Schema 1.0 validation output. Describes mixed/nullable/variants/recursive refs;
  custom-hook constraints explicitly user-defined.
- Lazy captures node members/child sequence nodes, not scalar-position props/tuple
  entries. Borrowed input must live; stream_record retains shared per-record backing.
- Source paths support nodes/args/props; DOM also supports :name/:tag. Scalar
  read_at supports scalar_type<T> (includes native value/chrono/path/views).
- Stream interface yields individual top-level typed records; buffers current node
  plus feed chunk. Does not merge a whole streamed doc into one reflected config.
- RPC is local registry/dispatch with member-function/metadata bindings, native
  typed params/results, ids/errors; no transport/network effects.

Benchmarks: tests/kdl_test/BENCHMARK.md records final sequential scalar/SSE2/AVX2
runs, hardware/compiler/commands and limitations. On i9-13900H/Clang23 -O3:
longstring reads 1381.0/10140.0/18892.7 MB/s; comment reads
1700.2/13322.9/27619.6; longstring writes 2402.1/18705.2/32155.4.
Small-config 166.1/182.6/174.5 MB/s; no claim of universal improvement.

Next: No required work remains; review/commit only if requested. All changes
remain uncommitted. Build dirs and corpus remain as recorded above.


## Active official Schema correction (2026-10-27)
User explicitly requires official KDL Schema; prior custom dialect superseded.
Rewrote schema.hpp to official document/info/node/prop/value/children/definitions
and KQL [id="tN"] refs. Canonical writer-output layout exported. schema<T>() now
expected<document,error_ctx>; write_schema strict errors for unrepresentable
bindings. export_schema<T>() explicitly returns official conservative document +
separate diagnostics (variant/null compound/custom serializer). No private dialect.
Docs updated. Added official vocabulary tests/golden, recursive/nullable scalar/
mixed/array coverage and kdl_schema_conformance test-only interpreter loads actual
upstream examples/kdl-schema.kdl when corpus supplied. Currently BUILDING in
session68346 (poll before running tests). Earlier build issues were test local/
anonymous type linkage, moved to global/named namespace. Need run tests ONLY after
successful build (avoid stale prior executables), fix any issues; GCC26 reflection
schema test + sanitizer regression + standalone/format checks remain.
No agents, no commits.


## Official Schema correction COMPLETE (2026-10-27, supersedes custom dialect)
User requirement implemented. schema.hpp now exports official KDL Schema 1.0:
- document/info/node/prop/value/children/definitions, type/enum/format, min/max,
  required and official KQL [id="tN"] refs. Recursive definitions registered before
  traversal. No private vocabulary or cpp/bits/ref-id extensions.
- Canonical write_kdl output is the declared target (not every reader layout).
  Object/mixed/map/sequence/tuple/array/scalar enum/nullable scalar/chrono/path/lazy
  supported. Date vs date-time and native writer numeric formats matched.
- schema<T>() now expected<document,error_ctx>. write_schema<T>() returns error
  feature_not_supported when exact binding constraints are unrepresentable.
  export_schema<T>() returns {value, diagnostics} for explicit conservative export
  of variants/null-compound/custom serializers using ONLY official vocabulary,
  standard description notes and separate diagnostics.
- docs/kdl.md updated with official spec link, API change/examples/limits.
- kdl_schema_test: golden official structure, recursive refs/mixed/props/enum/
  nullable/scalar/cardinality/date formats and explicit approximation checks.
- Optional kdl_schema_conformance target loads upstream examples/kdl-schema.kdl
  using corpus root. Test-only interpreter checks actual upstream structural,
  reference, type/enum/cardinality rules; positive generated schema + real writer
  output pass, deliberately invalid roots/extra properties/unknown fields/wrong
  scalar types rejected. It is not a public general-purpose schema validator.
- Fixed test-only local/anonymous reflection linkage issues and numeric DOM signed
  vs unsigned equality in golden test (compares canonical serialized form).

FINAL AFTER ALL EDITS: Clang ASan/UBSan six targeted schema/KDL/custom/mixed/corpus
CTest targets PASS (6/6). GCC16 C++26 official schema/metaschema/reflection PASS
(3/3). Schema/umbrella standalone C++23 -fno-exceptions compile, clang-format and
git diff --check pass. Full 338 grammar corpus and streaming paths remain passing.
No running work, no agents, no commit. Previous 22/22 and 20/20 broad regression
results belong to prior completed expansion; current correction used targeted
rebuilds/tests above, not stale binaries. No further required work remains.


## Active read constraint integration (2026-10-27)
User asked reading constraint checks, then clarified follow existing project/JSON
style. Inspected JSON: uses shared glz::read_constraint + self_constraint, NOT
runtime Schema document validation. Implementing these same metadata APIs/options/
errors in KDL; no new parallel schema validator API. User update communicated.
- Extracted unchanged core read_constraint dispatch into detail helper accepting
  native read callback; JSON's existing from delegates. KDL direct node/scalar/
  property consumption uses same helper (temporary -> constraint -> assignment).
- KDL finish invokes shared self_constraint after syntax/missing-fields success,
  respecting skip_self_constraint; skip_read_constraint handled shared core.
- Shared write_wrappers recognizes read_constraint as transparent, so KDL writer
  and official writer schema see underlying values. Constraints do not run on write.
- New kdl_constraint_test covers field/member-function/std::function/lambda,
  scalar/vector/object, nested/self, property, duplicates/error offset/message,
  skip opts, nullable/list/lazy and writer/schema unwrapping. First run 31/32
  passed; one test mistakenly expected std::map preexisting member after reader
  clears root map; replaced with reflected enclosing object (rebuild needed).
- Build Clang json_test still running session2488; GCC constraint build session91151
  running/pending poll. No running external schema validator work or agents.
Need finish tests/docs and shared core JSON/TOML/YAML regressions, GCC26 checks.


## Read constraints COMPLETE (2026-10-27)
Latest user clarification implemented: follow existing JSON/project mechanisms.
KDL now directly supports shared glz::read_constraint and glz::self_constraint,
constraint_violated/custom_error_message, skip_read_constraint and
skip_self_constraint. No runtime external-Schema validator added (JSON itself
uses typed metadata predicates). Docs/kdl.md Read constraints section explains
semantics/API and distinction from schema export.

Changes:
- Shared detail::dispatch_read_constraint factors existing core reader logic via
  native read callback. Existing from<Format, wrapper> delegates, preserving JSON.
- KDL nodes/scalar properties use shared callback with temporaries; constraints
  pass before assignment. Object self constraints run from finish after syntax/
  missing-field success. Failed self constraints leave parsed object, JSON parity.
- Shared writer shape/unwrap supports constraint wrappers. Const wrapper factory
  now preserves const (remove_reference_t) so const constrained objects write.
  KDL and JSON const/callable-target regression assertions added.
- kdl_constraint_test covers member functions, std::function, concrete lambdas,
  callable assignment targets, scalar/vector/object/property constraints,
  cross-field self, nullable/sequence/lazy, skip opts, duplicates, skipped fields,
  syntax errors, preserved field values, error offsets/messages, write/schema.
- Corrected test expectation for root map-clearing behavior by using an enclosing
  reflected object. No product semantics changed for maps.

FINAL checks AFTER last edits: Clang C++23 ASan/UBSan 11/11 PASS (KDL constraint,
unit/direct/custom/mixed/schema/metaschema/corpus + JSON/TOML/YAML regressions).
GCC16 C++26 3/3 PASS (constraint/schema/reflection), including final extra const
JSON writer regression. Three changed public/umbrella headers compile standalone
-fno-exceptions. clang-format and git diff --check pass. No agents, no commits,
no running builds/tests; no remaining required work.


## ACTIVE project conventions audit (2026-10-27)
User: inspect project conventions, find deviations and fix. No agents authorized.
Read .editorconfig/.clang-format, README feature/API contracts, tests/CMakeLists,
cmake/ci/standalone-headers and windows-header-compatibility, SIMD CI workflow.
No AGENTS/CONTRIBUTING files found in repo or ancestor dirs. Explicit contracts:
3-space formatting, header independence/include-cleaner, preserve Windows macros
(min/max etc), no exceptions baseline, shared error APIs, SIMD backend CI coverage.

Findings/actions so far:
1. Many new KDL headers used std::min/max and numeric_limits<T>::min/max without
   macro protection, violating Windows CI. Changed to (std::min)/(limits::max)()
   in all KDL headers via script, not yet clang-formatted or fully tested.
2. Added tests/kdl_test/kdl_header_compatibility_test.cpp + foreach CMake entry,
   simulates min/max macros after glaze.hpp, checks preservation then includes
   kdl umbrella. Build session7114 FAILED because libstdc++ <numeric> itself is
   not macro-safe if first included after macros. Fix test by including <numeric>
   (and any needed standard headers) BEFORE defining macro; library headers must
   still keep min/max protection. No actual Windows compiler installed.
3. Ran clang-tidy misc-include-cleaner on schema.hpp; log /tmp/kdl-tidy-schema.log
   (48 warnings; missing direct STL/core/common.hpp includes, and unused write.hpp
   false positive because format specialization side effects -> use IWYU keep).
   Need audit/fix includes across new headers with clang-tidy, respecting required
   specialization includes. clang-tidy installed.
4. New KDL ecosystem error/expected-returning public methods lack [[nodiscard]],
   unlike core read/write style. Consider adding appropriately. Stream read_stream
   reports file_open_failure on I/O failure -> should use existing error contract
   appropriate error (no file_read_failure enum). Need inspect analogous APIs.
5. SIMD CI .github/workflows/simd-backends.yml only builds/tests JSON validators,
   omits new KDL scanner. Should integrate kdl_scan_test into existing SSE/AVX/
   ARM/AVX512 builds/tests including SDE executable loop; WASM section standalone
   compile pattern can mirror KDL. Existing env UTF8_TESTS regex includes only JSON.
6. CMake new kdl_schema_conformance + scalar tests lack target_code_coverage;
   surrounding targets use it. Review formatting/coverage, long foreach line.

Current progress commentary told user no extra rules files, using repo config/CI;
Windows macro incompatibility found and being fixed. No approval question needed.
No prior authorized features should be removed. ALL KDL stays native, no JSON
adapters. Full previous functionality/tests recorded above.

Next: finish audit+fixes, add targeted macro regression; clang-tidy include-cleaner
batch audit (can run processes concurrently, no subagents); standalone/noexc checks;
full relevant Clang sanitizer and GCC26 regressions, formatting/docs update if
APIs touched, update this handoff. All changes uncommitted.


## Active scalar union / multiline / hash follow-up (2026-10-27)
Latest user requests support for scalar Schema unions, multiline borrowing, extreme
hash sizes. No agents. Prior conventions audit remains in scope.
- schema scalar_union now merges official type args and retains constraints common
  to all alternatives. Alternative-specific format/enum correlations still emit
  explicit diagnostics (int/string/bool loses i32, cannot silently claim exact).
  Scalar-only node variants without meta.tag use value union rather than any_body.
- parse borrow_multiline probes allocation-free for contiguous decoded spans:
  single indented content line, empty literal, zero-indent LF multiline/raw.
  Discontiguous dedenting/escapes/normalized newlines fall back to owning parser.
  A std::string_view cannot represent discontiguous storage. Need tests/corpus!
- study.hpp is parameter sampling, NOT perfect hashing. KDL already uses same
  hash_info/decode_hash_with_size as JSON. Need larger-field tests/benchmark.
- Windows compatibility test now preincludes numeric (libstdc++ numeric cannot
  be first-included after min/max). Build running session12681 for schema,
  metaschema, borrow, Windows macro test. Need check result.
Next: targeted new tests, finish conventions includes/CI/coverage, docs, broad
Clang/GCC26/corpus checks. No commits.


Progress: scalar union + contiguous multiline initial new tests pass under Clang
and GCC26, official metaschema/corpus pass. Include-cleaner audit all KDL headers
now fixed direct includes (two-pass logs /tmp/kdl-include*.log); final three
reflect includes just fixed, verify. Shared stream file_open_failure matches
file/file_ops.hpp fread errors; no deviation. Added nodiscard public free helpers,
coverage calls and KDL scanner SIMD CI integration including WASM/SDE.
Important extreme test found REAL shared hash bug: uint8_t table construction
loop indices wrap for N>=256, and uint8_t(N) invalid sentinel truncates. Fixed
loops to size_t, sentinel bucket_value_t<N>, permits unique_index N==256; bucket
value width now grows to size_t beyond65535. Also replaced column vector/sort
with allocation-free seen-byte/seen-(byte,size) sets to fit default Clang constexpr
limits for512 fields. Preserve signed-char sized hash encoding exactly.
512 getters use function pointers (+lambda) to avoid unique lambda-type template
blowup. Added JSON512 regression and synthetic256/512 keys tests.
Current builds: Clang full25 targets session16320 -> /tmp/kdl-final-build.log;
GCC initial6 targets session23794 completed but hash test source was edited during
build: rebuild before final results. Public headers standalone session42936.
Need complete builds/tests, inspect final diagnostics, document shared fix and
finish handoff. No commits/agents.


## AUTHORITATIVE COMPLETE: conventions + scalar unions / multiline / large hashes

All currently authorized follow-ups complete. No agents, no commits; no running
builds/tests. Earlier ACTIVE sections above are historical and superseded.

- Official Schema scalar-only variants merge type names, common constraints and
  finite enums. Strict bool/string/null and same-format numeric unions succeed.
  int/string/bool exports type union but diagnoses lost per-alternative i32 format;
  property-discriminator/body correlations remain explicit conservative exports.
- Multiline string_view borrowing is allocation-free when decoded bytes form one
  contiguous source span (single indented content line, empty content, zero-indent
  LF lines; raw and quoted). General multi-line dedentation/escape/newline rewriting
  still needs owning storage: impossible to represent in one std::string_view.
  Added pointer/semantic/rejection tests and 64KB zero-allocation regression.
- KDL already shares JSON compile-time hash machinery; json/study.hpp is parameter
  sampling, unrelated. New512-field test exposed shared >=256 index/sentinel
  overflow; fixed all table-construction loops, sentinel widths, and exact256
  unique-index path. Byte/size-column analysis now uses allocation-free sets
  instead of sorting, retaining first-unique-column semantics and signed-char
  encoding while fitting Clang default constexpr limits. Tests cover256 unique
  byte keys,512 front hashes,512 full-hash metadata fields, invalid sentinel,
  near matches and independent JSON512 read. 3423 hash assertions pass.
- Conventions audit finished: protected min/max invocations across KDL headers;
  Windows macro regression; direct header includes (all16 KDL detail/public
  headers clean under misc-include-cleaner); nodiscard public error-returning
  helpers; coverage hooks for scalar/conformance targets; KDL scanner added to
  SSE/AVX/ARM/WASM/SDE CI. WASM excludes OS-only protected-page test.
  read_stream file_open_failure retained: shared file/file_ops.hpp uses the
  same error for actual fread I/O failures.
- docs/kdl.md explains union precision, multiline continuity and shared hashing.

FINAL after all edits: Clang C++23 ASan/UBSan25/25 PASS (all21 KDL targets +
JSON/TOML/YAML/exceptions). GCC16 C++26 -freflection23/23 PASS (all KDL + p2996).
Official338 grammar corpus, SIMD/scalar paths and streaming still pass; official
metaschema/writer validation passes. Twenty public/exception/core headers compile
standalone with -fno-exceptions. Full KDL include-cleaner audit has zero warnings.
clang-format dry-run and git diff --check pass. Native Windows/ARM/WASM/AVX512
execution not available locally; added CI coverage does not imply those jobs ran.
Next: no required implementation remains; review/commit only when requested.


## Upstream sync COMPLETE (2026-10-27)
User requested syncing main and current branch with upstream. Added upstream
https://github.com/stephenberry/glaze.git. Fetched5 new main commits; both local
main and feature/kdl2.0 fast-forwarded from1476d6b7 to a92a6b50, and both pushed
atomically to fork origin via existing SSH credentials (HTTPS push had no auth).
Origin fetch confirms both remote branches match upstream/main. Feature now
tracks origin/feature/kdl2.0; main retains origin/main tracking.
All60 dirty paths preserved via stash include-untracked, then applied cleanly;
only opts.hpp auto-merged (unchanged KDL diff, upstream21-line offset). Compared
all untracked file bytes to stash and verified tracked patch differs only opts
base hash/offset. KDL changes remain UNCOMMITTED and consequently NOT uploaded.
Backup stash retained: “KDL work preserved before upstream sync 2026-10-27”.
Full Clang ASan/UBSan rebuild and25/25 KDL+JSON/TOML/YAML/exceptions CTest PASS
after sync; diff check clean. No running work, no new commits.


## Segmented publication (2026-10-27)
User explicitly authorized committing local KDL changes in segments and pushing
to fork feature/kdl2.0. Six logical commits: shared hash fix; shared wrappers/
constraints/lazy and exception fix; KDL core; native ecosystem; tests/CI; docs.
Earlier uncommitted-status entries are historical. No product code changes since
post-upstream25/25 sanitizer verification; the umbrella was briefly narrowed for
the core commit then restored byte-for-byte in the ecosystem commit. Main stays
at upstream a92a6b50; only feature/kdl2.0 receives these six commits. Stash backup
from upstream sync is retained. Next action after documentation commit: push
feature/kdl2.0 to origin via SSH and verify remote HEAD equals local HEAD.
