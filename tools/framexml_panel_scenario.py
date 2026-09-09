#!/usr/bin/env python3
"""Exercise stock menu and EditBox input through LuaEngine dispatch.

This is an offline dispatch regression, not SDL/GPU/gameplay certification.
The stock button is placed at a known test coordinate; its handlers are retained.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

from framexml_run_matrix import prepare_assets, sha256


def run(binary, assets, output, disable_handler=False):
    output.mkdir(parents=True, exist_ok=False)
    fixture = output / "assets"
    identity = prepare_assets(assets, fixture, (assets / "manifest.json").read_text(encoding="utf-8"))
    config = output / "config"
    config.mkdir()
    setup = """
assert(MainMenuMicroButton and GameMenuFrame, 'STOCK_MENU_MISSING')
HideUIPanel(GameMenuFrame)
MainMenuMicroButton:SetParent(UIParent)
MainMenuMicroButton:ClearAllPoints()
MainMenuMicroButton:SetPoint('BOTTOMLEFT', UIParent, 'BOTTOMLEFT', 80, 80)
MainMenuMicroButton:SetSize(28, 58)
MainMenuMicroButton:SetFrameStrata('TOOLTIP')
MainMenuMicroButton:Show()
assert(MainMenuMicroButton:IsVisible(), 'BUTTON_HIDDEN')
assert(not GameMenuFrame:IsShown(), 'MENU_ALREADY_OPEN')
assert(ChatFrame1EditBox, 'STOCK_EDITBOX_MISSING')
ChatFrame1EditBox:ClearFocus()
ChatFrame1EditBox:ClearAllPoints()
ChatFrame1EditBox:SetPoint('BOTTOMLEFT', UIParent, 'BOTTOMLEFT', 300, 80)
ChatFrame1EditBox:SetSize(220, 30)
ChatFrame1EditBox:SetFrameStrata('TOOLTIP')
ChatFrame1EditBox:SetText('')
ChatFrame1EditBox:Show()
__woweeTextChanges=0
local oldTextChanged=ChatFrame1EditBox:GetScript('OnTextChanged')
ChatFrame1EditBox:SetScript('OnTextChanged', function(self, ...)
  __woweeTextChanges=__woweeTextChanges+1
  if oldTextChanged then oldTextChanged(self, ...) end
end)
"""
    if disable_handler:
        setup += "MainMenuMicroButton:SetScript('OnMouseUp', function() end)\n"
    command = [str(binary), str(fixture), "--viewport:1024x768", "--lua:" + setup,
               "--drawn:MainMenuMicroButton", "--hit:96,672",
               "--mouse:96,672,L", "--mouse:96,672,",
               "--lua:assert(GameMenuFrame:IsShown(), 'STOCK_MENU_DID_NOT_OPEN')"]
    if not disable_handler:
        command += [
            "--lua:assert(GameMenuButtonContinueText:GetText() == RETURN_TO_GAME, 'MENU_LABEL'); "
            "assert(GameMenuButtonContinueText:IsVisible(), 'MENU_LABEL_HIDDEN')",
            "--draw", "--drawn:GameMenuButtonContinueText",
            "--hit:320,672", "--mouse:320,672,L", "--mouse:320,672,",
            "--lua:assert(ChatFrame1EditBox:HasFocus(), 'TEXT_FOCUS')",
            "--text:hé ",
            "--lua:assert(ChatFrame1EditBox:GetText() == 'hé ' and "
            "__woweeTextChanges == 1, 'TEXT_INSERT')",
            "--key:BACKSPACE", "--key:BACKSPACE",
            "--lua:assert(ChatFrame1EditBox:GetText() == 'h' and "
            "__woweeTextChanges == 3, 'TEXT_BACKSPACE')",
            "--hit:96,672", "--mouse:96,672,L", "--mouse:96,672,",
            "--lua:assert(not GameMenuFrame:IsShown(), 'STOCK_MENU_DID_NOT_CLOSE')"]
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("WOWEE_")}
    env.update(WOWEE_CONFIG_ROOT=str(config), WOWEE_LOG_FILE=str(output / "engine.log"),
               WOWEE_LUA_API_FALLBACK="0", WOWEE_LOAD_FRAMEXML="1")
    try:
        process = subprocess.run(command, cwd=output, env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=120,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        code, raw = process.returncode, process.stdout
    except subprocess.TimeoutExpired as error:
        code, raw = None, error.stdout or b""
    (output / "stdout.log").write_bytes(raw)
    log = raw.decode("utf-8", errors="replace")
    button_hit_count = log.count("hit at 96,672 -> MainMenuMicroButton")
    hit = button_hit_count == (1 if disable_handler else 2)
    edit_hit = "hit at 320,672 -> ChatFrame1EditBox" in log
    text_changed = "text dispatched (4 UTF-8 bytes)" in log and log.count("key dispatched: BACKSPACE") == 2
    drawn = "GameMenuButtonContinueText: DRAWN" in log
    expected_failure_marker_count = log.count("STOCK_MENU_DID_NOT_OPEN")
    expected_failure_error_count = sum(
        "[ERROR] LuaEngine: script error:" in line and "STOCK_MENU_DID_NOT_OPEN" in line
        for line in log.splitlines())
    expected_failure_matched = (disable_handler and code == 2
                                and expected_failure_error_count == 1
                                and hit)
    passed = code == 0 and hit and edit_hit and text_changed and drawn
    report = dict(result="pass" if passed else "fail", exit_code=code,
                  binary_sha256=sha256(binary), stdout_sha256=sha256(output / "stdout.log"),
                  source=next((x for x in log.splitlines() if x.startswith("== source:")), None),
                  stock_button_hit=hit, stock_button_hit_count=button_hit_count,
                  panel_button_in_draw_order=drawn,
                  stock_editbox_hit=edit_hit, text_edit_events=text_changed,
                  text_change_event_count=3 if (passed or expected_failure_matched) else None,
                  final_editbox_text="h" if (passed or expected_failure_matched) else None,
                  expected_failure_marker_count=expected_failure_marker_count,
                  expected_failure_error_count=expected_failure_error_count,
                  expected_failure_matched=expected_failure_matched,
                  handler_disabled=disable_handler, input=identity, command=command,
                  scope="Offline stock panel and focused EditBox through LuaEngine dispatch; test widget positions; no SDL, GPU capture or gameplay claim")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--disable-handler", action="store_true")
    args = parser.parse_args()
    result = run(args.binary.resolve(), args.assets.resolve(), args.output.resolve(), args.disable_handler)
    print(json.dumps({k: result[k] for k in ("result", "exit_code", "stock_button_hit",
                                             "stock_editbox_hit", "text_edit_events",
                                             "panel_button_in_draw_order",
                                             "expected_failure_matched")}))
    raise SystemExit(0 if result["result"] == "pass" else 1)
