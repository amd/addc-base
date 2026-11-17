# Fuzz targets

`base64_decode_fuzz.cpp` exercises the single Base-owned RFC 4648 codec used by
the CPER parser and decoders. Accepted input must decode and re-encode to the
same canonical string.

`cper_parser_fuzz.cpp` feeds arbitrary bytes through both parser modes and
checks the invariant that tolerant mode accepts every record accepted by
strict mode. Both targets also have deterministic smoke corpora in the normal
Meson test suite.

The Base PR gate runs Clang's libFuzzer with AddressSanitizer and
UndefinedBehaviorSanitizer for 30 seconds and also runs the full test suite
under both sanitizers.
