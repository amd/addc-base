# AMD ADDC base library

`addc-base` is the library that turns a raw AMD Common Platform Error
Record (CPER) into AMD Field IDs (AFIDs). It parses the binary
record, selects decoders from the CPER section and platform metadata, and
classifies each supported error event. The primary result is a concise list of
AFID and field-replaceable unit (FRU) pairs suitable for consumers such as a
BMC, AMC, or AMD SMI.

The library can also return the analysis pipeline's decoded event report as
structured JSON. `addc-base` stops at producing AFIDs: mapping an AFID to a
service action is intentionally outside this repository.

The CPER parser is independently implemented from AMD-owned and applicable
public specifications. It does not incorporate, derive from, link against, or
depend on libcper source code. References to libcper describe output
compatibility only.

The stable C ABI is declared in
[`include/addc/lib_c.h`](include/addc/lib_c.h). A native C++ convenience API is
declared in [`include/addc/lib.hpp`](include/addc/lib.hpp); because it exposes
C++ standard-library and `nlohmann_json` types, it is a source API rather than
a compiler-independent binary ABI. Its JSON types are named through the aliases
`addc::json_value` and `addc::ordered_json_value` in
[`include/addc/json.hpp`](include/addc/json.hpp); consumer code should use
these names rather than `nlohmann::` types.

## Requirements

- Meson 1.3 or later
- Ninja (for Meson's default backend)
- CMake 3.20 or later (alternative to Meson; see [CMake](#cmake))
- A C11 compiler
- A C++20 compiler
- `nlohmann_json` (Meson and CMake fall back to a pinned download)

## Third-party software

`addc-base` has one direct third-party software dependency:

| Component | Purpose | Version | Upstream source | License |
| --- | --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing and serialization | System-provided, or 3.12.0 when using the Meson fallback | [v3.12.0 source archive](https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip) | MIT |

The fallback archive is pinned in [`subprojects/nlohmann_json.wrap`](subprojects/nlohmann_json.wrap)
with SHA-256
`b8cb0ef2dd7f57f18933997c9934bb1fa962594f701cd5a8d3c2c80541559372`.
This table is a human-readable dependency inventory; release tooling may also
produce a machine-readable SBOM in a standard format such as SPDX or CycloneDX.

## Source repository manifest

`tools/generate_source_manifest.py` generates a source repository manifest. The
manifest derives the application version and binary targets from Meson,
records third-party source and license metadata, inventories every repository
file with its SHA-256 hash, and explicitly reports media assets and
internal-only release exclusions.

```shell
meson setup manifest-build --buildtype=release --force-fallback-for=nlohmann_json
python3 tools/generate_source_manifest.py \
    --build-dir manifest-build --output addc-base-source-manifest.json
```

Version bumps require no separate manifest edit because the version is read
from `meson.build`. Update
[`manifest/manifest-metadata.json`](manifest/manifest-metadata.json) when a
third-party component, license, media asset, or release exclusion changes.

## Build and install

```shell
meson setup builddir --buildtype=release
meson compile -C builddir
meson install -C builddir
```

The installed library provides `addc-lib.pc` for `pkg-config` consumers.
The C++ convenience header additionally requires nlohmann/json; this public
header dependency is declared by the pkg-config package.

### CMake

Meson is the primary build. A CMake build is provided for projects that build
`addc-base` from source, such as AMD SMI. It produces the same `libaddc_lib`
with the same exported API:

```cmake
add_subdirectory(addc-base)        # or FetchContent_MakeAvailable(addc-base)
target_link_libraries(my_target PRIVATE addc::addc_lib)
```

Installed builds also provide `find_package(addc-base)` with the same
`addc::addc_lib` target, and `addc-lib.pc`.

The CMake options mirror `meson_options.txt`:

| CMake option | Meson option | CMake default |
| --- | --- | --- |
| `ADDC_PROJECTS` | `projects` | `all` (comma- or semicolon-separated list) |
| `ADDC_ENABLE_LIB` | `enable_lib` | `ON` |
| `ADDC_ENABLE_CLI` | `enable_cli` | `ON` only for a top-level build |
| `ADDC_ENABLE_TESTS` | `enable_tests` | `ON` only for a top-level build |
| `ADDC_ENABLE_FUZZING` | `enable_fuzzing` | `OFF` |
| `ADDC_ENABLE_INSTALL_HEADERS` | `enable_install_headers` | `ON` |
| `ADDC_WERROR` | `werror` | `ON` only for a top-level build |
| `ADDC_LIB_STATIC` | — | `OFF` (see [Linking](#linking-shared-or-built-in)) |
| `ADDC_INSTALL` | — | `ON` (`OFF` with `ADDC_LIB_STATIC`) |

When `addc-base` is built as a subproject, it skips the CLI and tests and does
not treat warnings as errors. CMake uses an existing
`nlohmann_json::nlohmann_json` target or an installed `nlohmann_json` package.
Otherwise it downloads the archive pinned in the wrap file. For offline builds,
set `FETCHCONTENT_SOURCE_DIR_ADDC_NLOHMANN_JSON` to an extracted copy of that
archive.

CMake reads the version and soversion from `meson.build`. Any other change to
`meson.build` (sources, product conditions, defines, or tests) must be mirrored
in `CMakeLists.txt`. The PR gate runs `tools/check_cmake_parity.py` for every
product and fails if the two builds compile different sources or `ADDC_*`
defines, register different tests, or export different API symbols.

#### Linking: shared or built in

By default `addc_lib` is a shared library (`libaddc_lib.so.1`) that the
consumer ships alongside its own binaries. To build it into the consumer's own
shared library instead, set `ADDC_LIB_STATIC` before adding the tree:

```cmake
set(ADDC_LIB_STATIC ON)
add_subdirectory(addc-base)        # submodule, FetchContent, or a copied tree
target_link_libraries(amd_smi PRIVATE addc::addc_lib)
```

The static archives are compiled with hidden visibility, so only the `ADDC_API`
functions can be exported from the consumer's library, and a version script
such as AMD SMI's `amdsmi_*` list hides those too. Install rules are not
supported in this mode. A copied tree must include at least `CMakeLists.txt`,
`cmake/`, `meson.build`, `include/`, and `src/`. Sources are selected per
product by `CMakeLists.txt`, so they cannot be globbed into a consumer's own
source list. [`tests/amdsmi_consumer`](tests/amdsmi_consumer) is a minimal
AMD SMI-style consumer that CI builds in both modes.

## C/C++ library interface

The public C ABI uses C linkage, an opaque reusable decoder context, status
codes, fixed-width types, and caller-owned output buffers. The same header can
be included by C and C++ applications.

`addc_cper_get_summary()` is the direct CPER-to-AFID interface. It uses a
two-call contract: first query the number of results, then provide an array for
the library to fill.

```c
#include <addc/lib_c.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int print_cper_afids(const uint8_t *cper, size_t cper_size) {
    addc_context_t *ctx = addc_context_create();
    if (ctx == NULL)
        return 1;

    addc_error_info_t error = {0};
    error.struct_size = sizeof(error);
    size_t entry_count = 0;
    addc_status_t status = addc_cper_get_summary_ex(
        ctx, cper, cper_size, "event.cper", NULL, &entry_count, &error);
    if (status != ADDC_STATUS_SUCCESS) {
        fprintf(stderr, "%s at byte %llu: %s\n", error.source,
                (unsigned long long)error.byte_offset, error.message);
        addc_context_destroy(ctx);
        return 1;
    }

    if (entry_count == 0) {
        addc_context_destroy(ctx);
        return 0;
    }

    addc_cper_summary_entry_t *entries = calloc(entry_count, sizeof(*entries));
    if (entries == NULL) {
        addc_context_destroy(ctx);
        return 1;
    }

    error.struct_size = sizeof(error);
    status = addc_cper_get_summary_ex(
        ctx, cper, cper_size, "event.cper", entries, &entry_count, &error);

    if (status == ADDC_STATUS_SUCCESS) {
        for (size_t i = 0; i < entry_count; ++i)
            printf("AFID %d, FRU ID %s, FRU text %s, context %s\n",
                   entries[i].afid, entries[i].fru_id,
                   entries[i].fru_text, entries[i].additional_context);
    }

    free(entries);
    addc_context_destroy(ctx);
    return status == ADDC_STATUS_SUCCESS ? 0 : 1;
}
```

Every AFID emitted by a decoded event is returned by the summary interface.
This includes `ADDC_UNCLASSIFIED_AFID` when an event cannot be classified more
specifically. A CPER may produce zero, one, or multiple AFID/FRU entries.

Use `addc_cper_decode_json_ex()` when the complete event report is needed
instead of the concise summary. The `_ex` operations preserve optional source
metadata and return structured failure details without transferring ownership.
The original `addc_cper_decode_json()` and `addc_cper_get_summary()` functions
remain ABI-compatible wrappers. All operations accept CPER bytes in memory and
reuse the registries cached by `addc_context_t`.

The same caller-owned convention is used by `addc_cper_parse_json()`,
`addc_mca_decode_json()`, and `addc_mca_supported_projects_json()`. Older
functions that return allocated strings remain available for compatibility;
new integrations should not use them. `ADDC_STATUS_IO_ERROR` and
`ADDC_STATUS_UNSUPPORTED` distinguish environmental failures and unavailable
operations from malformed input and decode failures.

The analysis path keeps the parsed `CperRecord` representation typed through
section analysis and keeps binary section payloads as bytes. Base64 encoding is
performed only when producing the public JSON intermediate representation or a
consumer-facing field. The JSON intermediate representation returned by parse
operations is a compatibility and diagnostic adapter, not an internal handoff.

Parsing is tolerant by default. When malformed input is safely repaired, the
result contains a top-level `recovery` object with every original and
substituted value. Non-fatal decode and recovery messages appear in the
top-level `diagnostics` array only when present; each entry has a level, code,
message, and optional context. A frontend may emit the same entries through its
debug logger. Strict contexts reject any record that would require repair.

`include/addc/lib_c.h` is the authoritative C ABI contract, including status
values, structure layout, buffer sizing, and ownership rules. Its implementation
is C++ and converts internal results and exceptions into that C-compatible
contract.

Native C++ consumers may use `include/addc/lib.hpp`, including the typed
`get_error_summary_entries()` equivalent. This interface exposes C++
standard-library types and is not the compiler-independent C ABI.

Product, schema, and API versions are separate contracts:

- `addc_version()` reports the Base product release.
- `addc_schema_version()` / `ADDC_SCHEMA_VERSION` identify the public result
  schema.

The canonical parser contract is documented in
[`docs/parser-json-contract.md`](docs/parser-json-contract.md); the independent
final report and summary evolution rules are in
[`docs/final-json-contract.md`](docs/final-json-contract.md).
- `addc_api_version()` / `ADDC_API_VERSION` identify the stable C ABI.

`addc_context_t` is immutable after creation and may be reused by concurrent
decode calls. Destruction must wait until those calls complete. Creating one
context per thread is also supported.

## Filesystem path trust model

`addc-tool` is a local command-line utility. Its input argument and optional
`--output` argument intentionally refer to the exact paths selected by the
invoking user. The tool does not restrict those paths to a particular
directory and does not grant filesystem permissions beyond those already held
by its process.

Likewise, the C++ functions whose names end in `_file` open the exact path
supplied by their caller. Path authorization belongs to the calling
application. A privileged service or an application that receives paths from
an untrusted source must resolve and validate each path against its own allowed
root, including its symlink policy, before calling these functions. Such
applications should preferably read the authorized file themselves and use the
byte/span APIs, such as `analyze_cper_json()` or
`get_error_summary_entries()`.

## Tests

```shell
meson test -C builddir --print-errorlogs

# or, with CMake
cmake -S . -B build-cmake -G Ninja
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

The test suite includes a program compiled as C that verifies the public C ABI,
including structure size and layout, two-call buffer sizing, status returns,
and CPER summary extraction.
