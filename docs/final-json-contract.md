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
