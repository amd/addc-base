# Final ADDC JSON contract

Final ADDC reports and canonical parser JSON are separate contracts. Parser
JSON is defined in `parser-json-contract.md`; changing one contract never
implicitly changes the other.

Every final report has the public `schema_version`, `tool_version`, `source`,
and `events` members. Each event preserves the established `metadata`,
`platform`, `event_report`, `data_array`, `validation`, `analysis`, `ppr`, and
`firmware` members. Recovery and diagnostics remain optional root members.
Missing and JSON `null` are distinct and must retain their existing meanings.
Integer values, hexadecimal strings, arrays, and object field order retain the
representations frozen by the compatibility corpus.

The schema version follows these rules:

- A major change is required to remove a field, change a field's type or
  meaning, reinterpret an existing value, or make an optional field required.
- A minor change is required for backward-compatible optional fields or new
  enum values. Consumers must ignore optional fields they do not understand.
- A patch change may clarify documentation or correct output without changing
  the documented shape, type, or meaning.

All C, C++, and CLI adapters serialize the same resolved Base report. Summary
APIs project ordered integer AFIDs and their event FRU from that report; they do
not reparse serialized JSON or apply separate precedence rules. Any deliberate
schema change requires updated golden/schema tests, API fixtures, and a corpus
difference classification before release.

## CPER header and FRU identity (schema 3.5.0)

AMD in-band CPER records (written by the amdgpu driver) and out-of-band
records (written by the AMC or BMC) use the same layout. Some fields hold
different content. Base shows each field with one meaning for both sources:

- `source.origin` is `"in_band"` when the Creator ID is the ASCII text
  `amdgpu`. Otherwise it is `"out_of_band"`. It is `null` when the source
  has no CPER header.
- `source.creator_id` and `source.platform_id` show ASCII text when the
  16 bytes are printable ASCII followed only by NUL bytes (for example
  `amdgpu` and `0x1002:0x75C1`). Otherwise they show the GUID text.
- `source.record_id` is a string. It shows ASCII text when the 8 bytes are
  ASCII (for example `5:0`). Otherwise it shows the integer in decimal.
- `source.timestamp` and `metadata.timestamp` decode century 20 (in-band)
  and century 21 (out-of-band) as 20xx.
- `platform.fru` has no trailing spaces or NUL bytes.
- `platform.fru_id` is `null` when the FRU ID valid bit is 0. A CUID shows in
  the text form of its producer (see below). ASCII text shows as text. Other
  content shows as GUID text.
- `platform.cuid` is `null` when the FRU ID is not a CUID. Otherwise it holds
  `encoding`, `kind` and, for a PrimaryID, the decoded payload fields:
  `serial`, `unit_id`, `revision_id`, `device_id`, `vendor_id`,
  `component_type` and `component_type_name`. A DerivedID or a temporary CUID
  holds hash output, so it has only `encoding` and `kind`.

### CUID encodings

The current CUID specification defines one encoding: an RFC 9562 UUIDv8 whose
printed octets are framed from a 122-bit payload by a normative octet mapping.
The amdgpu driver and the ROCm amdcuid library write this form. Base reports it
as `"encoding": "spec"`, and `fru_id` shows the 16 octets in printed order.
Base tells PrimaryIDs, DerivedIDs and temporary CUIDs apart with the rules of
that specification (bit 117 and the reserved bits).

The `"v84_compat"` encoding is in place for compatibility only. The older v84
specification gave a bit-range table that the current specification replaced
because the bit order was ambiguous. APML and PMFW read that table as one
128-bit integer: version at integer bits [51:48], variant at bits [65:64].
Base decodes this form so that CPERs from those producers still decode, and
`fru_id` shows the integer most significant byte first, as APML prints it.
The byte order of this form in the FRU ID is not confirmed, so Base reads the
bytes as a little-endian integer first, then as a big-endian integer.

Base decodes a value that does not conform, such as the reserved component
type `0xc`, and reports it with `component_type_name` `"Reserved"`. An
adopted firmware UUID (version other than 8) is opaque and is not decoded.
