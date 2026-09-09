# TEST05 text/key runner validation

Validation completed on 2026-09-09 against the frozen Windows Debug runner:

The machine-readable aggregate is [runner-input-final-20260909.json](runner-input-final-20260909.json).

- runner SHA-256: `6eddda77a7b6297131d3ad95ff687acee40a2aa2e78ed0b8dafc8e8c91d9880b`
- embedded source: `3b516d92b90dc42ab5d50d64b60e24a7e9e4297b`
- source fix: `3b516d92b` (UTF-8 code-point caret movement)
- fixture manifest SHA-256: `cd64a47d94f4ad0b9691d12ab2696e04de002b831789eab66303c0ac004d14de`

The three independent combined runs in `D:\wowee-input-accept-20260909-002` passed with exit 0. Each run opened the stock game menu through `MainMenuMicroButton`, found its localized continue label in draw order, focused the real `ChatFrame1EditBox` through the hit-test/mouse path, inserted `hé `, delivered two `BACKSPACE` keys, observed three `OnTextChanged` calls, ended with text `h`, and closed the menu. The fixture-local config and SavedVariables were fresh for every run.

| Run | `result.json` SHA-256 | `stdout.log` SHA-256 |
| --- | --- | --- |
| 1 | `d8de4e6877511368c21be8fec9dd1e12327016257847766bcdd3f3f46b010812` | `f32ebb1e76aea1559d1128f43e64aed2cb131a6d26957f053ff6699484c72334` |
| 2 | `c7cacb9e928ad99de01274d1b373d87e2683553aeaa9512134d4aa4625eb0897` | `ef74b4b1f6a337f37899d7e5dde7b104486a0fb49046aa8980813f2fa583b06c` |
| 3 | `96582b58b7d5c5d0b576dd3ca0aef6e069dc326d441d02dff2d5ddeb7ab11ce6` | `fd7df4c61d9bb8dd8193761981835d4d8721c119d8f40858717e5f9ae539a36c` |

The full CLI matrix passed all 27 cases. Its report is `D:\wowee-input-accept-20260909-002\matrix\report.json`, SHA-256 `323fd916c96fe646a1d39b398d0d433ea6fe99a768c5c12785589ea4fcc51a5d`. This includes valid text/key dispatch plus empty text, unsupported key, malformed option, startup, Lua error, timeout, and missing-asset negatives. The matrix removes inherited `WOWEE_*` variables before installing its isolated test environment.

The disabled-handler probe in `D:\wowee-input-negative-final-20260909` passed its expected-failure classifier. Replacing the stock button's `OnMouseUp` produced engine exit 2, exactly one causal `LuaEngine: script error` for `STOCK_MENU_DID_NOT_OPEN`, and exactly one hit on `MainMenuMicroButton`. The result SHA-256 is `9f80b9b80ff8d12ae87d436e3f0115d49acd17f68ade4e26dac8dce1a164c79b`; its stdout SHA-256 is `68d79bc33429e214360820019b279f55fef9011391c650512d673a011c20e592`.

The pre-fix evidence remains intact in `D:\wowee-input-accept-20260909-001`. All three positives and the targeted matrix case failed `TEXT_BACKSPACE`: byte-wise caret movement removed only the UTF-8 continuation byte of `é`. The other 26 matrix cases passed. This failure led to the code-point stepping fix and was not overwritten by the passing rerun.

This evidence covers offline FrameXML/LuaEngine dispatch. It does not claim SDL text events, GPU rendering, or live gameplay input.

The isolated MSVC `test_text_markup` target passed 136 assertions in 15 cases, including valid 1/2/3/4-byte scalar boundaries, two Backspaces over `hé `, malformed/truncated input progress and retained markup/link behavior. Test executable SHA-256: `5b5999549e4226b4e6bf04fdfc336a6773db0c1f4ada3b40dbefb490cfc42b10`. Unicode grapheme-cluster navigation is not claimed.
