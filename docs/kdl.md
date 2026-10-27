# KDL 2.0

Include `glaze/kdl.hpp` to read and write [KDL 2.0](https://kdl.dev/spec/).
Reading a C++ object with `read_kdl` parses directly into its members. Writing
with `write_kdl` emits directly to the output buffer. Neither typed path builds
an intermediate AST or DOM, and neither converts through JSON. This is the
default behavior, with no extra option required. The native document model is
available explicitly through `glz::kdl::document` when an application needs a DOM.

The typed reader keeps field-presence flags and container positions on the
stack, plus temporary storage for the current token or decoded string. Unknown
fields (when allowed), skipped metadata members, and slashdashed components are
validated and consumed without constructing subtrees. Strings, maps, pointers,
and dynamic containers can still allocate their own storage; escaped/multiline
string decoding may need scratch storage. `read_kdl` takes a complete buffer;
`kdl::stream_reader` and `kdl::read_stream` incrementally consume top-level records
from chunks or an input stream.

## C++ objects

Members of a reflected object become named nodes. Scalar members have one
argument; nested objects use child blocks. Glaze's automatic reflection and
explicit `glz::meta<T>` object definitions both work.

```cpp
#include "glaze/kdl.hpp"

struct endpoint
{
   std::string host{};
   int port{};
};

struct settings
{
   std::string title{};
   bool enabled{};
   std::vector<int> ports{};
   endpoint server{};
};

settings value{};
auto ec = glz::read_kdl(value, R"(
   title "Example"
   enabled #true
   ports 80 443
   server host=localhost port=8080
)");
if (ec) {
   // Handle ec.ec and ec.count (the consumed byte offset).
}

auto text = glz::write_kdl(value); // expected<std::string, error_ctx>
```

The writer produces:

```kdl
title "Example"
enabled #true
ports 80 443
server {
   host "localhost"
   port 8080
}
```

| C++ type | KDL representation |
|---|---|
| Reflected object or `std::string` keyed map | Named nodes at the root, child nodes within a member |
| String, boolean, integer, floating point, enum | One argument on the member's node |
| Sequence of scalars | Arguments on the member's node |
| Sequence of objects or nested sequences | A child block with one `-` node per element |
| `std::tuple`, `std::pair`, `glz::tuple` | Fixed positional arguments on the member's node |
| `std::optional<T>` and owning nullable pointers | The representation of `T`, or `#null` |
| `std::variant` | Alternative selected by node annotation or discriminator property |
| `glz::kdl::mixed<Args, Props, Children>` | Independent argument, property and child sections |
| `glz::kdl::node`, `document`, `value` members | Dynamic node body, child nodes, or scalar value |

Object readers also accept properties as members, as shown by `server` above.
Properties are applied first, then child nodes; later occurrences of a member
replace earlier ones. Scalar members require exactly one argument and no
properties or child nodes. Object members cannot contain positional arguments.
Sequences accept either arguments or child nodes, without properties. Fixed size
arrays require the exact number of elements.

The root must be a reflected object, string keyed map, `kdl::document`, or a single
`kdl::node`. Tuple/pair members require exactly their declared number of scalar
arguments. `std::unique_ptr<T>` follows the same mapping as `T`, including tuples,
variants and dynamic nodes. Custom member wrappers, borrowed strings and lazy
node members are supported as described below.

## Mixed typed and dynamic data

Use `kdl::mixed<Arguments, Properties, Children>` for a node containing all three
sections. Each section can have an independent C++ schema:

```cpp
struct server_properties { int port{}; bool enabled{}; };
struct server_children { std::string path{}; };
using server_node = glz::kdl::mixed<
   std::tuple<std::string, int>, server_properties, server_children>;
struct configuration
{
   server_node server{};
   glz::kdl::node plugin{};
   glz::kdl::document extensions{};
   glz::kdl::value setting{};
};

auto config = glz::read_kdl<configuration>(R"kdl(
   (service)server production 4 port=8080 enabled=#true { path "/srv" }
   (custom)plugin 1 name=demo { nested #true }
   extensions { arbitrary 1; arbitrary 2 }
   setting (unit)"value"
)kdl");
```

`server.arguments`, `.properties`, `.children` hold the typed sections and
`.tag` holds the optional node annotation. Arguments accept scalar sequences or
tuples; properties accept reflected objects, string keyed scalar maps, or
`kdl::properties`; children accept reflected objects, string keyed maps, sequences,
or `kdl::document`. `error_on_unknown_keys`, `error_on_missing_keys`, metadata
renaming and skip rules apply to typed property and child objects.

`kdl::mixed<>` defaults to dynamic arguments, properties and children. A
`kdl::node` member retains its complete node body, name and annotation; its C++
member key supplies the name when writing. A `kdl::document` member holds the
nodes inside its child block, preserving order and repeated names. A
`kdl::value` member preserves the scalar type and value annotation. Only these
explicitly dynamic sections construct DOM data; the surrounding typed fields
continue to parse directly.

## Variants

Node variants select an alternative using `(tag)` or a `type` property. Define
`glz::meta<Variant>::ids` for stable external names; otherwise Glaze's default
`name_v<Alternative>` names are used.

```cpp
struct circle { double radius{}; };
struct rectangle { int width{}; int height{}; };
using shape = std::variant<circle, rectangle>;
template <> struct glz::meta<shape>
{
   static constexpr auto ids = std::array{"circle", "rectangle"};
};
struct drawing { shape object{}; };

auto a = glz::read_kdl<drawing>("(circle)object radius=2.5");
auto b = glz::read_kdl<drawing>("object type=rectangle width=4 height=5");
```

The default writer uses `(tag)`. Set `meta<Variant>::tag = "type"` to write the
discriminator property instead; another string selects a custom property key
for both reading and writing. The discriminator can appear anywhere among the
node's entries. Duplicate discriminator properties use the final value. When
both a node annotation and discriminator are present they must agree. Missing
or unknown IDs report `no_matching_variant_type`; a non-string discriminator
reports `invalid_variant_object`. The discriminator property is reserved and
removed before binding a typed alternative. Variants inspect the node once to
find the discriminator and then parse the selected alternative directly,
without constructing an intermediate tree.

Variants in scalar positions (tuple arguments or properties) try compatible
scalar alternatives in declaration order. A value annotation selects the
matching alternative ID explicitly; scalar variant writes include this
annotation to preserve the chosen alternative.

## Comments in configuration files

Comments are accepted by default in typed reads, file reads, and DOM reads:

```kdl
// A line comment
title "Example" // A trailing comment
/* A block comment, including /* nested */ comments */
enabled #true
ports 80 /- 8080 443  // /- excludes the next argument
server host=localhost /- port=3000 port=8080
/- unused-server host=disabled { port 9000 } // Excludes the entire node
```

KDL's `/-` can exclude a node, argument, property, or child block. Line and block
comments are ignored without changing their surrounding values. Comment markers
inside quoted or raw strings remain literal text. An unterminated block comment
is an error. Slashdashed components and ignored unknown fields must still contain
valid KDL syntax; their values are not checked against the C++ schema.
Comments are not retained when writing a configuration back out.

Typed reading assigns fields as they are encountered, like Glaze's JSON reader.
Repeated valid properties and nodes use the later value; child nodes are applied
after properties. Each encountered typed value is checked against its target,
so an invalid earlier value still reports an error even if a later occurrence
would overwrite it. On an error, earlier assignments can remain in the target.
Read into a temporary object when an application needs to commit changes only
after the entire configuration succeeds.

## C++26 static reflection

KDL uses Glaze's shared [P2996 reflection backend](p2996-reflection.md). With
GCC 16, compile with `-std=c++26 -freflection`; Glaze automatically detects
reflection support. `-DGLZ_REFLECTION26=1` can also select it explicitly.
Non-aggregate classes and their members use the same reflection access policy as
Glaze's other formats, including private data members. Explicit `glz::meta<T>`
key names, enum names and skip rules retain precedence.

C-style numeric/object arrays, including multidimensional arrays, use the same
KDL representation as other fixed size sequences. Without P2996, an explicit
object metadata definition can expose these array members.

For enums without metadata, the default remains their underlying integer.
Enable `reflect_enums` to read and write enumerator names:

```cpp
enum class mode { development, production };
struct application
{
   mode environment{};
};

struct kdl_reflection_opts : glz::opts
{
   bool reflect_enums = true;
   constexpr kdl_reflection_opts() : glz::opts{.format = glz::KDL} {}
};

application value{};
auto ec = glz::read<kdl_reflection_opts{}>(value, "environment production");
auto text = glz::write<kdl_reflection_opts{}>(value);
// environment "production"
```

Unknown enumerator names and values without a named enumerator return
`unexpected_enum`. Explicit enum metadata keeps its configured spelling even
when `reflect_enums` is enabled. The option also applies inside nullable members
and sequences. Alternatively, opt in per enum without an options type:

```cpp
template <> struct glz::meta<mode> : glz::reflect_enum {};
// glz::is_reflect_enum<mode> is true; ordinary read_kdl/write_kdl use names.
```

To build and run the GCC static reflection tests:

```sh
cmake -S . -B build/kdl-gcc26 -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_STANDARD=26 -DCMAKE_CXX_FLAGS=-freflection \
  -Dglaze_ENABLE_REFLECTION26=ON -Dglaze_ENABLE_FUZZING=OFF
cmake --build build/kdl-gcc26 --target kdl_test kdl_reflection_test p2996_test
ctest --test-dir build/kdl-gcc26 -R '^(kdl_.*|p2996_test)$' --output-on-failure
```

## Document model

`glz::kdl::document` is an ordered `std::vector<glz::kdl::node>`. Each node contains:

- `name` and an optional `tag` string (`std::optional<std::string>`).
- Ordered `arguments`, a vector of `glz::kdl::value`.
- `properties`, a vector of string/value pairs. The reader resolves duplicate
  keys using the last value.
- Ordered `children`, a vector of nodes.

A value contains `data` and an optional `tag`. `data` is a variant of
`std::monostate` (null), `bool`, `int64_t`, `uint64_t`, `double`, `std::string`, and
`glz::kdl::number`. Numbers exceeding native integer or floating point storage
retain their KDL spelling in `number::text`; writing validates this spelling.
Native floating point values use double precision. Reading into a C++ numeric
member checks its range and rejects fractional numbers for integer targets.

```cpp
auto document = glz::read_kdl<glz::kdl::document>(R"(
   (service)server "production" port=(u16)443 {
      enabled #true
   }
)");
if (document) {
   auto serialized = glz::write_kdl(*document);
}
```

The parser supports identifier, quoted, raw and multiline strings; Unicode
escapes and whitespace; signed decimal, hexadecimal, octal and binary numbers;
`#true`, `#false`, `#null`, `#inf`, `#-inf`, `#nan`; type annotations; nested
comments, slashdash comments and line continuations. It validates UTF-8 and
rejects disallowed literal code points. KDL 1.0 specific syntax is rejected.
Empty documents are valid.

Writing emits escaped quoted string values, identifiers or quoted names, and
indented child blocks. Comments, original whitespace, numeric spelling within
native storage, and the relative ordering of arguments versus properties are
not retained. Annotations are preserved as strings and do not coerce
values or enforce annotation-specific schemas.

## APIs and options

```cpp
std::string buffer;
auto ec = glz::write_kdl(value, buffer);
ec = glz::read_kdl(value, buffer);

ec = glz::write_file_kdl(value, "settings.kdl", buffer);
ec = glz::read_file_kdl(value, "settings.kdl", buffer);

// The standard dispatch API accepts the same types.
ec = glz::read<glz::opts{.format = glz::KDL}>(value, buffer);
auto text = glz::write<glz::opts{.format = glz::KDL}>(value);
```

Unknown object keys fail by default. Set `error_on_unknown_keys = false` to
ignore them, or `error_on_missing_keys = true` to require the object's required
members. Metadata key names and `meta<T>::skip` apply to typed objects.
`skip_null_members` controls whether the writer omits null object members.
Writer indentation uses `indentation_char` and `indentation_width` (three spaces
by default), for both typed and DOM output. For tabs, pass an options type with
`char indentation_char = '\t'` and `uint8_t indentation_width = 1` to
`glz::write<Options{}>`. Readers
and writers limit recursive document nesting to 256 levels. Use the record
stream API below for incremental refill.

## Error handling

KDL supports Glaze's non-throwing and throwing API styles:

```cpp
configuration config{};
std::string input = "server production 4 port=8080 enabled=#true { path local }";

// Existing object: error_ctx has .ec and the consumed byte offset .count.
auto ec = glz::read_kdl(config, input);
if (ec) std::cerr << glz::format_error(ec, input);

// New object / output string: expected<T, error_ctx>.
auto result = glz::read_kdl<configuration>(input);
auto text = glz::write_kdl(config);
if (!result) std::cerr << glz::format_error(result.error(), input);

// Optional standard error-code adapter.
// #include "glaze/core/std_error_code.hpp"
std::error_code standard = ec.ec;

// The generic dispatch API also supports caller-owned contexts and options.
glz::context context{};
ec = glz::read<glz::opts{.format = glz::KDL}>(config, input, context);
```

Include `glaze/exceptions/kdl_exceptions.hpp` (or `glaze/glaze_exceptions.hpp`)
for `glz::ex::read_kdl`, `write_kdl`, `read_file_kdl` and `write_file_kdl`.
Failures throw `std::runtime_error`; read diagnostics include the source error
location and file reads identify the path. These headers disable their throwing
interfaces with `-fno-exceptions`.

```cpp
auto config = glz::ex::read_kdl<configuration>(input);
glz::ex::read_kdl(config, input);
std::string text = glz::ex::write_kdl(config);
glz::ex::write_file_kdl(config, "settings.kdl", text);
glz::ex::read_file_kdl(config, "settings.kdl", text);
```

Buffer writes return `error_ctx` in the ordinary API. Throwing buffer writes
return `void`; throwing raw-pointer writes return the number of bytes written.
Use a sized buffer such as `std::span<char>` when capacity checks are required.

## Tests

The `kdl_test` CMake target exercises the document model, direct C++ mappings,
comments, error locations, buffering, and file APIs. `kdl_mixed_test` covers
mixed sections, dynamic members, tuples, pointers, variants and indentation;
`kdl_exceptions_test` covers throwing interfaces. `kdl_direct_test` verifies
that numeric reads/writes with preallocated targets and buffers, long comments,
and large skipped subtrees perform no heap allocation. The upstream corpus can
also be supplied locally:

```sh
cmake -S . -B build/kdl -Dglaze_KDL_TEST_SUITE=/path/to/kdl/tests/test_cases
cmake --build build/kdl --target kdl_test kdl_direct_test kdl_conformance
ctest --test-dir build/kdl -R '^kdl_' --output-on-failure
```

The optional conformance target checks acceptance/rejection in both the DOM and
direct subtree-skipping paths, decoded values against `expected_kdl`, and writer
round trips. It does not download test data.


## SIMD and field dispatch

ASCII scanning uses the compile-time SIMD backend selected by Glaze:
SSE2, AVX2, AVX512BW, AArch64 NEON or WASM SIMD128. `glz::kdl::simd_backend`
reports the selected backend. Define `GLZ_DISABLE_SIMD` for the scalar path.
Unicode and grammar handling share the same scalar rules across backends.
All loads are bounded by the actual input length; no padding is required.
Reflected fields use Glaze's compile-time hash dispatch with an exact key
comparison after lookup, including unknown keys that hash to an existing field.

KDL field lookup uses the same compile-time `hash_info` and bounded
`decode_hash_with_size` machinery as JSON, followed by exact key comparison.
Hash strategy selection and seed search live in `core/reflect.hpp` and are shared
by both formats. `json/study.hpp` instead implements parameter sampling and is
unrelated to field hashing. Hash tests cover 128 reflected fields, 512 metadata
fields, and unknown keys including near matches; timing is workload dependent.
Table indices and invalid sentinels widen past 255 fields. Column analysis uses
allocation-free byte sets during constant evaluation, avoiding per-column sorting.
Very large metadata still incurs compile-time and table-storage costs.

Build the reproducible benchmark with `-Dglaze_KDL_BUILD_BENCHMARK=ON`, or compile
`tests/kdl_test/kdl_benchmark.cpp` with `-O3 -std=c++23 -Iinclude`. Compare the same
source using default flags, `-mavx2`, and `-DGLZ_DISABLE_SIMD`. Results depend on
input length, escaped characters, CPU and allocation behavior; short configurations
may see little benefit. SIMD reduces scanning time, not allocation costs. Local
measurements and commands are in
`tests/kdl_test/BENCHMARK.md`.

## Chrono, paths and custom bindings

`std::filesystem::path` reads/writes a string. `std::chrono::duration` writes its
native tick count and reads either a numeric tick count or a unit string, e.g.
`timeout "1.5s"`. Supported suffixes are `ns`, `us`, `µs`, `ms`, `s`, `min`, `h`,
`d`, `w`. Integral durations require an exact number of destination ticks and a
representable count; fractional ticks and overflow return `parse_number_failure`.
Calendar dates and system-clock time points reuse Glaze's date/ISO8601 rules.
Steady-clock time points use native elapsed counts; `glz::epoch_time` wrappers
use the selected epoch unit, with checked conversion to system-clock storage.

`glz::custom` getter/setter/member/lambda metadata, `glz::custom_read` and
`glz::custom_write` are supported. The shared Glaze custom dispatch is reused;
getter results are unwrapped once, including before null-member omission.
Low-level `glz::from<glz::KDL, T>` scalar hooks receive the original KDL entry
bytes, including an optional annotation. `glz::to<glz::KDL, T>` hooks emit a native
KDL scalar. Hooks must honor the usual context, iterator and buffer contracts.
Document-shaped low-level hooks remain responsible for their whole document.

## Read constraints

KDL uses the same metadata constraints as JSON: `glz::read_constraint` checks a
field before assignment, and `glz::self_constraint` checks a completed object.
No separate KDL validation API is needed.

```cpp
struct listener { int port = 80; };
template <> struct glz::meta<listener> {
   static constexpr auto value = object(
      "port", glz::read_constraint<&listener::port,
         [](const listener&, int port) { return port > 0 && port < 65536; },
         "port must be between 1 and 65535">);
};
listener target;
auto ec = glz::read_kdl(target, "port -1");
// ec.ec == glz::error_code::constraint_violated
// ec.custom_error_message == "port must be between 1 and 65535"
// target.port remains 80
```

Member-function predicates, `std::function` predicate members, concrete lambdas,
and callable assignment targets follow the shared Glaze contracts. Constraints
work on node members and scalar properties, including structured members and
sequences. A node field is read into a temporary, checked, then assigned; syntax
or constraint failure preserves that field's previous value. Earlier successful
fields can remain updated, matching JSON's partial-update behavior.

Register a whole-object predicate with
`static constexpr auto self_constraint = glz::self_constraint<predicate, "message">`
in `glz::meta<T>` or the type's nested `glaze` definition. It runs after a successful
object read, including nested/nullable/sequence objects. Its failure reports the
same error and message but leaves the parsed object in place, as in JSON. A lazy
object runs its constraints when materialized. Skipped/slashdashed fields do not
run predicates; malformed input and missing-field errors take precedence.

The shared `skip_read_constraint` and `skip_self_constraint` extended options
work with `glz::read<Options{}>` and `.format = glz::KDL`. Writing unwraps field
constraints and emits their underlying values without invoking predicates.
Official schema export follows this writer shape; arbitrary C++ predicates are
not translated into Schema validation rules. These are typed metadata checks,
not a runtime validator for external KDL Schema documents.

## Borrowed strings and raw strings

`std::string_view` members borrow unescaped bare, quoted or raw single-line
strings directly from the input. Keep the input buffer alive and unchanged.
Multiline literals also borrow when their decoded result occupies one contiguous
source span: an indented single content line, empty content, or zero-indent LF
lines. Dedenting several indented lines, removing interior whitespace, decoding
escapes, and normalizing CRLF/Unicode newlines can require discontiguous or changed
bytes. Those cases return `invalid_escape` for ordinary views; use `std::string`
for owning decoded storage. Owning strings decode normally.

`glz::raw_string<&T::member>` or the `raw_string` option borrows pre-escaped
single-line content without decoding. Its writer validates the content before
emitting it inside quotes without escaping it again. `glz::escaped` restores
normal escaping. KDL raw-delimited strings such as `##"literal"##` are already
literal content and can also be borrowed. Contiguous multiline borrowing follows
the rules above; general dedenting requires owning storage. These rules apply
through nullable and sequence members too.

## Lazy nodes

```cpp
struct expensive { std::vector<int> values; };
struct config { glz::lazy<expensive> subtree; };
std::string input = "subtree { values 1 2 3; }";
auto cfg = glz::read_kdl<config>(input);
if (cfg) {
   auto subtree = cfg->subtree.get(); // expected<reference_wrapper<expensive>, error_ctx>
   if (subtree) subtree->get().values.push_back(4);
}
```

`glz::lazy<T>` captures a validated native node source slice, including its name
and annotation. Type binding happens on the first `get()` and the result/error
is cached. `source()` exposes the borrowed slice; `materialized()` reports whether
a successful value is cached. Keep the input alive even after materialization if
`T` contains borrowed fields. Lazy nodes work as members and child sequence
elements; scalar-position properties and positional tuple entries require eager
types. Writing materializes and serializes the cached value, including edits.
Const access may populate the cache; synchronize concurrent access externally.

## Chunked and stream input

```cpp
glz::kdl::stream_reader<endpoint> reader;
std::vector<glz::kdl::stream_record<endpoint>> saved;
auto consume = [&](auto record) { saved.push_back(std::move(record)); };
auto ec = reader.feed("endpoint { host local", consume);
if (!ec) ec = reader.feed("host; port 8080; }\n", consume);
if (!ec) ec = reader.finish(consume);
// Or: ec = glz::kdl::read_stream<endpoint>(istream, consume);
```

Each top-level node is one typed record. `stream_reader<>` defaults to dynamic
`kdl::node`; typed records bind directly without building a DOM. Framing preserves
lexical state across chunks, including UTF-8, raw/multiline strings, comments,
slashdash, braces and continuations. `finish()` is required to accept an EOF-terminated
last node or report truncation. Error offsets are absolute input byte offsets.
Records already delivered remain delivered if a later record fails.

Each `stream_record<T>` owns shared backing for its input, so borrowed views and
lazy fields remain valid while that record/backing is retained. Moving only the
value out does not retain the backing. The framer retains the current unfinished
record plus the caller's current feed chunk, rather than an entire document.
`buffered_bytes()` exposes pending bytes; the largest record determines memory
usage. Callbacks are synchronous and must not reenter the same reader.

## Native paths

```cpp
auto port = glz::kdl::read_at<int>(source, "/server[1]/@port");
auto host = glz::kdl::read_at<std::string>(source, "/server/host");
auto view = glz::kdl::get_view(source, "/server/#0");
auto doc = glz::read_kdl<glz::kdl::document>(source);
if (doc) {
   auto target = glz::kdl::get(*doc, "/server/:tag");
   auto ec = glz::kdl::set(*doc, "/server/:name", std::string{"renamed"});
}
```

`/name` selects the first matching sibling; `[n]` is a zero-based occurrence
index among siblings with that name. `#n` selects an argument; `@key` selects a
property. DOM `:name` and `:tag` expose node metadata. An empty path addresses
the whole document. Escape `~ / [ ] @ # :` in names as `~0` through `~6`.
`parse_path` compiles paths for repeated use. DOM `get` returns an expected
variant of pointers; `set` requires the replacement's exact target type and
changes only existing targets. `get_view`/`read_at` support node/argument/property
source locations, validate the whole source, and do not construct a DOM.
Duplicate properties follow last-value-wins semantics. Source views borrow input;
metadata source projections use the DOM API instead.

## Native diff, patch and formatting

`kdl::diff(before, after)` compares native documents, retaining node names, tags,
argument/property values and order, repeated siblings and children. Its operations
use absolute sibling-index vectors, so duplicate names are unambiguous.
`kdl::apply_patch(document, patch)` is transactional: any invalid operation leaves
the original unchanged. Insert, erase, replace and individual name/tag/argument/
property edits are supported. Apply a diff to its original base; it does not
perform conflict detection against unrelated concurrent edits.

`kdl::write_patch` and `read_patch` serialize operations directly as KDL, e.g.
`set-name "/0" "renamed"`, `set-tag "/0" #null`, or
`insert "/1" { added 42; }`. The reader rejects malformed operations.
`kdl::minify(source)` and `kdl::prettify<Opts>(source)` validate and canonically
format through the native document model. They preserve semantic values, tags,
argument/child order and repeated nodes, and discard comments and original
spelling. Duplicate properties are collapsed according to KDL semantics.

## Native queries

`kdl::query(document, expression)` returns pointers/views into the document.
Compile once with `compile_query` for repeated evaluation. `query_kdl(source,
expression)` returns selections serialized directly as KDL: selected nodes keep
their names/bodies, scalar projections become `value` nodes, counts become `count`.

Examples:

```text
/server/host[?@enabled == #true && @port >= 80]/@port
//host | count()
/server/host[::-1]/#0
/*[?:tag == "production"]/:name | first()
```

The native query dialect supports name/wildcard navigation, `//` recursive descent,
argument/property/name/tag projections, negative indices, Python-style slices
with nonzero strides, and filters with comparisons, `!` existence negation,
`&&` and `||` (AND before OR). Literals use KDL scalar spelling; missing selectors
fail comparisons and satisfy negated existence tests. Comparisons do not coerce
strings to numbers. Pipelines support `count()`, `first()`, `last()`, `reverse()`.
This documented KDL dialect is inspired by JMESPath; it is neither full JMESPath
nor the official CSS-style KDL Query specification. No JSON adaptation is involved.

## Official KDL Schema export

`kdl::schema<T, Opts>()` returns `expected<kdl::document, error_ctx>` and
`kdl::write_schema<T, Opts>()` returns its text. Both export the official
[KDL Schema 1.0 vocabulary](https://github.com/kdl-org/kdl/blob/main/SCHEMA-SPEC.md):
a single `document` with `info`, named `node` rules, `prop`, `value`, `children`,
`min`/`max`, `required`, `type`, `enum`, numeric `format`, and `definitions`.
Recursive references use official KDL Query selectors such as `[id="t0"]`.
There is no private `glaze-kdl` schema dialect.

The schema describes the canonical layout emitted by `write_kdl`, rather than
all alternative property/child layouts accepted by the reader. Ordinary object
members become named node rules; scalar sequences become repeated values;
compound sequences become `-` children; mixed sections retain argument, property
and child rules. Fixed arrays and tuples retain their cardinalities. Nullable
scalars add `null` to the allowed types/enumeration. Metadata names, write skips
and `skip_null_members` control exported fields and occurrence requirements.
Lazy members use their underlying type. Strings and paths use `type string`;
durations use their native numeric tick representation, matching writer output.
C++ field names/types are not emitted as nonstandard schema properties.

```cpp
struct endpoint { int port{}; std::string host; };
auto schema_text = glz::kdl::write_schema<endpoint>();
if (!schema_text) {
   // Handle schema_text.error().ec.
}
```

Scalar-only variants combine alternative types in one official `type` node, for
example `type "number" "string" "boolean"`. Identical format constraints are
retained; finite enumerations can merge when every alternative is enumerated.
A `variant<bool, string, monostate>` exports without diagnostics. For
`variant<int, string, bool>`, the type union is exported but the alternative-specific
`i32` constraint still requires a diagnostic: applying it globally would constrain
the string alternative too. Scalar-only node variants with a property discriminator
also retain a diagnostic for the discriminator/body correlation.

The official vocabulary cannot express every C++ binding constraint. In particular,
variant body/discriminator correlations, null-or-compound alternatives, and arbitrary
custom serializers cannot be inferred exactly. The default APIs return
`feature_not_supported` for these bindings. To explicitly obtain a conservative
schema that accepts a superset, use:

```cpp
auto report = glz::kdl::export_schema<MyType>();
for (const auto& diagnostic : report.diagnostics) {
   // Review each constraint that was widened.
}
auto text = glz::write_kdl(report.value);
```

`export_schema` always uses official vocabulary; approximation notes use the
standard `description` field. Diagnostics are returned separately and are never
silently discarded by `schema`/`write_schema`. This API exports schemas; it does
not provide a general runtime KDL Schema validator.

## Native RPC

```cpp
struct service {
   int sum(int x, int y) { return x + y; }
};
service object;
glz::kdl::rpc_registry rpc;
rpc.bind<&service::sum>("sum", object, {"x", "y"});
auto response = rpc.call("sum 2 3 id=\"request-1\"");
// (ok)reply id="request-1" { result 5; }
```

`bind(service)` also registers member functions exposed in explicit Glaze metadata.
Arguments may be positional, named properties (`sum x=2 y=3`), or named child nodes
for structured types. Parameter names passed to `bind` are copied. `id` is reserved
for correlation. Missing, extra, duplicate and invalid parameters fail before
invocation. Requests must contain exactly one active node; complete grammar is
validated before any function runs. Const/reference parameters bind to owned
input values for the duration of the call. Return values, including objects, are
written directly into a native KDL `result` node; void returns `#null`.
Failed `glz::expected` returns map to `invalid_call`.

`call` returns `expected<string, error_ctx>`; `respond` converts errors to native
`(error)reply code=... offset=...` text. The registry performs local dispatch only,
with no network transport, scheduling or service ownership. Keep registered
services alive, and synchronize concurrent use of mutable services externally.
