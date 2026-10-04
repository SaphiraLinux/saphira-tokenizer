# Examples

Build from `src/`:

    make examples

This produces `build/encode_demo` and `build/mt_demo` (or `build-opt/`
if you built with `make opt`; the examples follow whatever `BUILD` is set).

## encode_demo

    ./build/encode_demo <tokenizer.json> "hello world" "a  b" "café"

One line of space-separated token ids per input, plus the pretoken spans on
stderr so you can see where the boundaries fell. Uses only the three public
calls: `gt_tokenizer_load`, `gt_tokenizer_encode`, `gt_tokenizer_free`.

## mt_demo

    ./build/mt_demo <tokenizer.json> 8 "hello world" "a  b"

Shares one read-only tokenizer across N threads via `gt_tokenizer_encode_mt`
and prints a checksum per thread. All checksums must agree; if they don't,
the sharing contract is broken.

## quickstart.py

Needs the audit venv (it imports the pinned Rust wheel for comparison):

    source /home/smalley/src-provenance/audit-venv/bin/activate
    python3 examples/quickstart.py <tokenizer.json> <path-to-C-build-dir>

Encodes a fixed battery with the C CLI and the Rust oracle side by side and
prints MATCH/DIFFER per case. This is the smallest possible differential.
