# Canonical parser JSON contract

The native CPER producer is the compatibility authority. `parse_native_json`
creates its JSON, and `normalize_parser_json` is the single backend-neutral
seam. The normalizer currently preserves the producer value exactly and does
not add schema keys.

The canonical root is an object containing `header`, `sectionDescriptors`, and
`sections`. The descriptor and section arrays have equal length and matching
indexes. Existing optional header, descriptor, recovery, and section fields
remain optional. Existing integer, hexadecimal-string, readable-pair, Base64,
and field-order conventions remain unchanged. Unknown fields are preserved.

Each descriptor's `sectionOffset` and `sectionLength` identify a checked range
in the original CPER bytes. The typed adapter retains those bytes plus the
complete canonical descriptor and section values. Unknown, excluded,
ambiguous, newer, or temporarily unmigrated sections use `OpaqueSection`.
Recovery repairs and boundary diagnostics are retained separately from the
canonical JSON. Parser JSON evolves only with an explicitly approved parser
schema revision; final ADDC report schema versions do not implicitly change it.
