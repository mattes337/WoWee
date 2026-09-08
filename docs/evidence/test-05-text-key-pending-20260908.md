# TEST05 text/key pending validation

The runner input patch is committed at `a5bf19221`; the combined stock panel/EditBox scenario is committed at `ef6f98e18`. The focused parser contract passed on Windows from the isolated D: build with 181 assertions in 8 test cases. The Windows argument conversion was separately proved at `e73f553e7`: UTF-16 `wmain` arguments convert to exact UTF-8 for `é` and U+1F600, while an unpaired surrogate is rejected. A runner containing these commits has not been built or executed. No stock EditBox runtime result is claimed.

After host recovery, pin and hash the newly built runner before using this manifest:

```powershell
$repo = 'G:\WoW Projects\wowee'
$runner = Join-Path $repo 'build-fork-windows\bin\Debug\framexml_run.exe'
$assets = Join-Path $repo 'Data\extracted'
$out = 'D:\wowee-test05-20260908'
Get-FileHash -Algorithm SHA256 -LiteralPath $runner
New-Item -ItemType Directory -Path $out

1..3 | ForEach-Object {
  python (Join-Path $repo 'tools\framexml_panel_scenario.py') `
    --binary $runner --assets $assets `
    --output (Join-Path $out ("combined-positive-{0}" -f $_))
  if ($LASTEXITCODE -ne 0) { throw "combined positive $_ failed" }
}

python (Join-Path $repo 'tools\framexml_panel_scenario.py') `
  --binary $runner --assets $assets `
  --output (Join-Path $out 'combined-disabled-handler') --disable-handler
if ($LASTEXITCODE -eq 0) { throw 'disabled-handler negative unexpectedly passed' }
$negativeLog = Get-Content -Raw -LiteralPath (Join-Path $out 'combined-disabled-handler\stdout.log')
if ($negativeLog -notmatch 'STOCK_MENU_DID_NOT_OPEN') { throw 'negative failed for an unexpected reason' }

& $runner $assets '--text:' *> (Join-Path $out 'text-empty.log')
if ($LASTEXITCODE -ne 2) { throw 'empty text preflight did not exit 2' }
& $runner $assets '--key:CTRL+C' *> (Join-Path $out 'key-control.log')
if ($LASTEXITCODE -ne 2) { throw 'invalid key preflight did not exit 2' }

python (Join-Path $repo 'tools\framexml_run_matrix.py') `
  --binary $runner --assets $assets --output (Join-Path $out 'matrix')
if ($LASTEXITCODE -ne 0) { throw 'FrameXML runner matrix failed' }
```

The combined positives must each report the stock menu button hit twice, the independent `ChatFrame1EditBox` hit, the localized menu label in draw order, three chained `OnTextChanged` calls, final text `h`, and exit 0. The negative must fail because the stock menu handler was replaced. These checks are offline LuaEngine dispatch only; SDL, GPU capture, and gameplay remain outside this evidence.

The isolated emulator can be recovered without reprovisioning accounts or volumes. Do not print or read `.env`, `secrets.json`, private traces, or account data:

```powershell
$db = 'wowee-eval-20260908-af5200-db-1'
$auth = 'wowee-eval-20260908-af5200-auth-1'
$world = 'wowee-eval-20260908-af5200-world-1'
docker start $db
if ($LASTEXITCODE -ne 0) { throw 'existing evaluation DB did not start' }
$deadline = (Get-Date).AddMinutes(5)
do {
  $dbHealth = docker inspect --format '{{.State.Health.Status}}' $db
  if ($LASTEXITCODE -eq 0 -and $dbHealth -eq 'healthy') { break }
  Start-Sleep -Seconds 2
} while ((Get-Date) -lt $deadline)
if ($dbHealth -ne 'healthy') { throw 'evaluation DB did not become healthy' }
docker start $auth $world
if ($LASTEXITCODE -ne 0) { throw 'existing evaluation auth/world services did not start' }
docker inspect --format '{{.Name}} {{.State.Status}}' $db $auth $world
```

This starts only the three named existing containers and preserves their volumes. The completed db-import container must remain stopped. Running container state alone does not prove server readiness: verify the existing auth/world readiness markers before login checks. These commands have not been executed during host exhaustion.
