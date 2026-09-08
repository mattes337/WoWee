# WoW SRP session-key interleave regression

Primary convention: local AzerothCore `src/common/Cryptography/Authentication/SRP6.cpp`
lines 50–81, checkout `798d08c58a8e00b7937050963119bb857344b640`
at `G:/azerothcore-wotlk`. Its `SHA1Interleave` splits a 32-byte little-endian S,
counts initial zero bytes, rounds that count up to even, then hashes each half
starting at half that offset. It interleaves the two SHA1 digests into 40 bytes.
The helper in `include/auth/srp_session_key.hpp` follows that convention using
the existing production `Crypto::sha1`; this is not a new SRP implementation.

The fixed vectors in `tests/test_srp_session_key.cpp` were generated independently
with Python hashlib, not with the helper or OpenSSL wrapper:

```python
import hashlib
for n in [0, 1, 2, 3, 32]:
    s = bytes(n) + bytes(range(n + 1, 33)) if n < 32 else bytes(32)
    skip = (len(s) - len(s.lstrip(b"\0")) + 1) // 2 * 2
    a = hashlib.sha1(s[skip::2]).digest()
    b = hashlib.sha1(s[skip + 1::2]).digest()
    print(s.hex(), bytes(x for pair in zip(a, b) for x in pair).hex())
```

One leading zero skips two bytes, including the following nonzero byte. The
one- and two-zero fixtures therefore intentionally share a key. Three zeros
skip four bytes. All-zero S hashes two empty byte sequences, avoiding out-of-range
reads. An unchanged nonzero case protects the common path. A sixth fixture
independently calculated from synthetic `TEST:PASSWORD`, salt `AA` repeated 31
then `00`, server exponent 147 and client exponent 245 checks the concrete
handshake-derived S supplied by the independent protocol audit:

```
S_LE=0030bbc02ca2a089aefaae951b33cf344c0fddf79bf1c4527f022a2803402e46
K=1421793e778fe54d7e75e8c42793fd0b7b547a1cd6cb16904d76433003ef93705012e6fd901ac590
```

The helper rejects lengths other than 32 rather than accepting ambiguous padding.
It does not itself reject zero S as a handshake policy; callers retain SRP
validation responsibilities. The target links production `crypto.cpp` and
OpenSSL::Crypto in the existing full test build. No OpenSSL requirement is
introduced into the minimal headless build. Session-key vectors alone do not
prove a successful live authentication or world login.


## Validation (2026-09-08)

The production integration and handshake oracle are committed in
`e606b083c4813da85394054013a8d8e81ab75bb1`, depending on helper commit `a8a18a7d1`.
The Windows MSVC Debug CTest run passed both `srp` (14 assertions, 8 cases) and
`srp_session_key` (11 assertions, 2 cases).

An independent targeted Ubuntu 24.04 run compiled those same test sources with
production `srp.cpp`, `big_num.cpp`, `crypto.cpp` and `logger.cpp`, and vendored
Catch2. GCC 13.3.0 and OpenSSL 3.0.13 passed both binaries with the same counts:
**25 assertions in 10 cases, exit 0**. All compiled sources, including Catch2,
used `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie`;
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` applied throughout. No sanitizer
findings were emitted. System OpenSSL itself was not rebuilt with sanitizers.

The isolated source snapshot was mounted read-only. Its exact revision and
per-file SHA256 values are saved in `build-srp-sanitizer/identity.json`; compiler
commands are in `build-srp-sanitizer/run.sh`, and the complete package/compiler/
test transcript is `build-srp-sanitizer/linux-output.txt`. Vulkan headers were
asserted absent. This targeted OpenSSL fixture is separate from the minimal
headless baseline and did not rerun unrelated tests. It verifies synthetic
protocol edge cases, not a real-account login outcome.
