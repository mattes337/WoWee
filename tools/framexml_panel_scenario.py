#!/usr/bin/env python3
"""Exercise a stock menu through LuaEngine mouse dispatch in a fresh fixture.

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
"""
    if disable_handler:
        setup += "MainMenuMicroButton:SetScript('OnMouseUp', function() end)\n"
    command = [str(binary), str(fixture), "--viewport:1024x768", "--lua:" + setup,
               "--drawn:MainMenuMicroButton", "--hit:96,672", "--mouse:96,672,L", "--mouse:96,672,",
               "--lua:assert(GameMenuFrame:IsShown(), 'STOCK_MENU_DID_NOT_OPEN'); "
               "assert(GameMenuButtonContinueText:GetText() == RETURN_TO_GAME, 'MENU_LABEL'); "
               "assert(GameMenuButtonContinueText:IsVisible(), 'MENU_LABEL_HIDDEN')",
               "--draw", "--drawn:GameMenuButtonContinueText", "--hit:96,672",
               "--mouse:96,672,L", "--mouse:96,672,",
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
    hit = log.count("hit at 96,672 -> MainMenuMicroButton") == 2
    drawn = "GameMenuButtonContinueText: DRAWN" in log
    passed = code == 0 and hit and drawn
    report = dict(result="pass" if passed else "fail", exit_code=code,
                  binary_sha256=sha256(binary), stdout_sha256=sha256(output / "stdout.log"),
                  source=next((x for x in log.splitlines() if x.startswith("== source:")), None),
                  stock_button_hit=hit, panel_button_in_draw_order=drawn,
                  handler_disabled=disable_handler, input=identity, command=command,
                  scope="Offline stock panel through LuaEngine mouse dispatch; test button position; no SDL, GPU capture or gameplay claim")
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
    print(json.dumps({k: result[k] for k in ("result", "exit_code", "stock_button_hit", "panel_button_in_draw_order")}))
    raise SystemExit(0 if result["result"] == "pass" else 1)
