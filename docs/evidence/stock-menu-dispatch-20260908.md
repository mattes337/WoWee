# Stock menu dispatch scenario

`tools/framexml_panel_scenario.py` runs the original MainMenuMicroButton
mouse-down/up handlers through the runner's production LuaEngine mouse
dispatcher. The fixture places the button at a known coordinate, preserving
its stock 28x58 size and inherited hit insets. It does not call its handlers or
`Click()` directly. Settings, copied interface files and saved variables are
fresh for each run; missing-global fallback is disabled.

The scenario checks the actual hit target before both clicks, requires the
stock GameMenuFrame to open and close, verifies its localized Continue label
and visibility, and requires that FontString in the production draw order
after drawing. A button container itself need not be a draw-order entry.

Three independent fixtures pass on the same identified runner. In a fourth,
replacing only the stock OnMouseUp handler with a no-op produces the real
`STOCK_MENU_DID_NOT_OPEN` assertion failure and exit 4. A subsequent stock
mouse-down branch also makes the close assertion fail. Neither command echo
nor an unrelated startup error is counted as this negative control.

[Run identities, hashes and outcomes](stock-menu-dispatch-20260908.json).

Earlier development fixtures are retained: 01/02 missed the hit target after
the test shrank the button without accounting for its 18-pixel top hit inset;
03 opened/closed correctly but incorrectly required the button container in
draw order instead of its visible FontString. Their failures were not credited
as successful runs or production defects.

This is a bounded TEST-05 step using LuaEngine dispatch. It does not cover the
SDL application route, EditBox text injection, stable GPU capture, normal
microbutton placement, or gameplay. The full TEST-05 parent remains open.
