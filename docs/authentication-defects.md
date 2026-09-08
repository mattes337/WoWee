# Authentication defects found during fork evaluation

## DEF-003: SRP proof serialization and session-key zero prefix

The fresh client reached the dedicated loopback emulator through real SDL
input, then received LOGON_PROOF status 4. The challenge salt contained 32
bytes, but the client logged its natural BigNum width as 31. The account's
stored verifier independently matched the client registration formula;
the test account and salt were retained unchanged.

The client removed high-order zero bytes when hashing A, B and the salt.
The controlled WotLK/AzerothCore contract hashes the fixed 32-byte wire
representations. This affects the scrambling parameter, client proof and
expected server proof. A second issue in session-key derivation hashed all
secret bytes instead of skipping a leading zero prefix rounded up to an
even byte count. These are different ends of a little-endian byte array.

Commits `a8a18a7d1` and `e606b083c` preserve the wire widths and implement
the verified interleave convention. A synthetic server-side arithmetic
fixture uses `TEST:PASSWORD`, a deliberately padded salt and server exponent
147, which produces a padded public value B. It derives the server secret
from the actual client A and checks the session key and both proofs. It
failed before the fix with an unequal session key and passes afterward.
Six independent Python SHA1 vectors cover the interleave edges; five
invalid-length cases exercise its input contract.

Fresh Windows MSVC Debug CTests pass: SRP has 14 assertions across eight
cases; the interleave fixture has 11 assertions across two cases. See
[interleave provenance and vectors](evidence/srp-session-key-vectors.md).
The oracle shares the production BigNum/SHA1 primitives; the fixed Python
vectors provide a separate derivation for the interleave step. A short
client A is not forced by this fixture. Legacy/custom multiplier and
hash-endian modes remain unverified.

The actual post-fix client authentication run is pending. Unit results do
not establish server login, character creation or world-entry acceptance.
