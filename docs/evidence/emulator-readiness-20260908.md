# Real emulator readiness audit — 2026-09-08

**Initial read-only audit:** an isolated real AzerothCore evaluation was
feasible using the existing client-data volume and pinned official container
images. No auth/world binary existed in the inspected host build/install
paths, and no active auth/world process or listener was found. Source checkout
alone was not counted as an available server. The separately authorized
follow-up provisioning and its actual results are recorded below.

Scope: `G:/azerothcore-wotlk`, `G:/WoW Projects/.merged/server`, Docker image
and volume metadata, and an explicitly authorized ephemeral Ubuntu helper
mounting **only** `docker_ac-client-data` read-only. The helper had no network
and a read-only root filesystem and was removed on exit. The existing database
volume was never mounted or queried. No account setup scripts were executed;
credential values and connection strings are omitted throughout.

## Source and host artifacts

| Observation | Evidence |
|---|---|
| Primary source HEAD | `798d08c58a8e00b7937050963119bb857344b640` |
| Source package metadata | `acore.json`: name `azerothcore-wotlk`, version `15.0.0-dev` |
| Working-tree differences | Four tracked log files under `docker/env/dist/logs`: Auth, Errors, Realm, Server. Log contents not read for credentials or reused as current execution evidence |
| Sibling source | `.merged/server` has an unresolvable `HEAD`; do not claim it as a pinned build source |
| Build/install paths | Primary `build` and `build_test` empty; `var/build` and `var/docker/build` contain only `.gitkeep`; `bin` contains shell launcher scripts, not server executables |
| Runtime filename search | No `authserver`, `worldserver`, `realmserver`, corresponding `.exe`, or extractor executables found in inspected trees; search included ignored files and excluded `.git`, third-party `deps`, and SQL content |
| Host state | No `authserver`, `worldserver`, `mysqld`, or `mariadbd` process; no listeners found on 3724, 8085, 3306 or 3307 at inspection |

Source/config SHA-256 identities:

| Primary-source relative path | SHA-256 |
|---|---|
| `CMakeLists.txt` | `cc30895abcc532a712f21d20766cacfe59365f1cb9ba7ca934430b845b8f7c94` |
| `acore.json` | `7ed57e51f9f5a4f860cc0cdde08659c66c99cc186dbae9a56ff49b05a14fd1e1` |
| `docker-compose.yml` | `443933b550d10bc4a921f1a4505e35fd5152885a8d7448efa31df1eec4f105b5` |
| `src/server/apps/authserver/authserver.conf.dist` | `05a5cf32696175df290a7a0bd29a09826a0957ba2f9af79bb8eb82019d7d05d4` |
| `src/server/apps/worldserver/worldserver.conf.dist` | `d86d2c143aac425730531ffd920fb1996a86495abd63e90ebf42d482dd3a75d6` |
| `src/tools/map_extractor/System.cpp` | `bc43d9022acf6ed588bafdb256cefe1187f7802d211906b42786180e1d0e6407` |
| `src/common/Collision/VMapDefinitions.h` | `8664d1bd5edcc72f8663bbf9011b67cc572a226c2967680e4fa58c95390c56f9` |

The sibling's CMake/package/compose bytes matched the three primary hashes
above, but that does not establish a complete matching source tree.

## Client data

Primary host `env/dist/data` contains 5,744 maps, 246 DBCs plus
`component.wow-enUS.txt`, and 14 camera M2s. Its vmaps/mmaps directories are
empty. Both source trees' `var/extractors` directories contain placeholders.
The sibling install tree has configs but no matching game-data set.

Every host map's first 12 bytes was checked: all 5,744 declare `MAPS`, format
version **9**, client build **12340**. This is a header compatibility check,
not provenance certification or a full map parser run.

The Docker client-data volume contains the missing collision/navigation data:

| Directory in volume | Files | File bytes reported by `du -sb` |
|---|---:|---:|
| `Cameras` | 14 | 34,768 |
| `dbc` | 247 (246 DBC + locale manifest) | 90,275,692 |
| `maps` | 5,744 | 291,014,951 |
| `vmaps` | 12,494 | 657,797,442 |
| `mmaps` | 3,780 | 2,192,760,616 |

Root `data-version` says `INSTALLED_VERSION=v19`. Sample vmap header starts
`VMAP_4.8`; sample mmap tile declares mmap version 19 and Detour version 7,
matching local source `MapDefines.h` and `DetourNavMesh.h`. Collision/navigation
files were counted and sampled, not exhaustively parsed or hashed. The
`enUS` manifest filename is locale evidence, not proof of original stock
assets or compatibility with a newer image.

Selected volume files, with SHA-256:

| File | SHA-256 |
|---|---|
| `dbc/Map.dbc` | `261bd7c2c9dbdba1f2966ca21106673ed722c2e3681d227f1eed261b6ce870d5` |
| `dbc/Spell.dbc` | `d5cce1a83550dcfa9eb2f0251dbb11fd24c272534b2b1a9b230924a44d817ab3` |
| `dbc/component.wow-enUS.txt` | `5408a41a8b17499650ddda83c4289144a1fa8851820c28865042f1d31574b5e5` |
| `maps/0002035.map` | `943f190e4f2de9abefe0f523cfec57377c7b07caa49c968bf50572afbe03f509` |

The last two hashes match the host files. Host tree hashes were also computed
by sorted relative path under `env/dist/data`, a zero byte, then each file's
binary SHA-256 digest, concatenated into an outer SHA-256:

- Cameras: `69b0d079e4103082d5bcb323bbe80a0191978b67330cd620df01256ed117f4d3`
- dbc including locale manifest: `0317642bebedbf9c8a1fcf9ac430971f3d2951d6685ff68a3ebae42fdbbe8808`
- maps: `0411093f271257ccaba78a6747e4090d34654ba8da3855ebca36c069a410d2e8`

## Docker and runnable-image candidates

Existing `docker_ac-client-data` and `docker_ac-db-data` are local Docker
volumes. Docker reported 3.232 GB and 1.481 GB respectively, with zero
container references before the read-only helper. The database volume's
schema, revisions, account contents and usability remain **unknown**.
The stopped RabbitMQ container and unrelated concurrent build helpers do not
constitute an emulator. No locally tagged AzerothCore server image was found.

Cached database candidates are `mysql:8` at
`sha256:c592c15aaf4a1961e15d82eb31ea5987dda862d1c4b1e93424438c0e91dc1f8d`
and `mariadb:10.6` at
`sha256:114d40be36852d0a019afd8458f7e1cc63fd300403b115b1f32e31130d5f671b`.
Their presence does not establish compatibility with the selected server.

Primary-source compose uses `mysql:8.4` and official `acore/ac-wotlk-*`
images. Read-only registry manifest inspection resolved these **linux/amd64
platform manifest digests** on September 8; layers were not pulled:

| Image repository (tag inspected) | Pinned platform digest |
|---|---|
| `acore/ac-wotlk-worldserver` (`master`) | `sha256:c8a17e2b2a0332a1af2cce50befa44cf373b918a367398984114ca5684179d59` |
| `acore/ac-wotlk-authserver` (`master`) | `sha256:6b02ed2ad5d30be3cc3aa9abe9ea603188b16121a8a70f6fb23894c3e1074aa9` |
| `acore/ac-wotlk-db-import` (`master`) | `sha256:c7ac7580579cb3f64217539ab64506d850abe764025ba4614f59738933579a90` |
| `mysql` (`8.4`) | `sha256:1d6b6a8fcee8ff758ff151d017f5203cd06792a0e698f0a593c9dfcb14609cf0` |

The three server/import image configs report creation around 09:01–09:02 UTC
on September 8. They expose no source-revision label in the inspected config;
the inherited version label `24.04` is a base-image label, **not the game or
AzerothCore version**. Obtain the actual server revision from the executable's
version/startup output before acceptance. Do not equate these images with the
local `798d08c...` source checkout.

## Existing configuration and account fixtures

Only allowlisted nonsecret fields were emitted from existing configs:

- Host auth: `RealmServerPort=3724`, `BindIP=0.0.0.0`, database updates enabled.
- Host world: `RealmID=1`, `WorldServerPort=8085`, `BindIP=0.0.0.0`,
  `DataDir=.`, `Expansion=2`, `ClientCacheVersion=0`,
  `Updates.EnableDatabases=7`.
- Docker world: data path `/azerothcore/env/dist/data`, log path
  `/azerothcore/env/dist/logs`; same realm/port/expansion settings. Multiple
  repeated `LogsDir` assignments exist; create clean test-owned config instead
  of treating that old file as a reproducible contract.

`tests/integration/test_e2e_gameplay.py` contains `ensure_test_account`
(lines 37–93), with database INSERT/UPDATE/commit operations. Its SHA-256 is
`4f82a5bb8cb7ee320007e7842d14fa37c6450b719ae959023fe75db9782099e5`.
`tests/integration/conftest.py` contains account/protocol fixture references;
SHA-256 `0eed505df5d402858b8844d3afc6a81bf7de7a61b7c7fec2fa7610fcc00bed3e`.
These are existing fixture source, not evidence that suitable dedicated
accounts exist. They were not executed or imported, and no credential values
were printed. Do not run their database setup against an existing volume.

## Isolated setup procedure

1. Create a new test-owned project, network, database volume and writable
   config/log directories. Use the four pinned images above, rather than the
   mutable `master` tag. The follow-up execution below records the actual pinned-image provisioning.
2. Mount existing `docker_ac-client-data` only at
   `/azerothcore/env/dist/data:ro` in the test worldserver. **Omit the
   `ac-client-data-init` service and its dependency:** the stock initializer
   manages client data, which this audit is preserving. Never mount
   `docker_ac-db-data` into the new project.
3. Keep MySQL private to the new network; initialize/import only the new DB
   volume. Generate new secrets in a private test configuration. The source
   compose names the connection settings `AC_LOGIN_DATABASE_INFO`,
   `AC_WORLD_DATABASE_INFO` and `AC_CHARACTER_DATABASE_INFO`; no existing
   connection strings should be copied.
4. Publish auth on loopback, for example `127.0.0.1:33724 -> 3724`, and world
   on `127.0.0.1:38085 -> 8085`, after rechecking availability. No external
   MySQL or SOAP publication is needed. Configure the new DB realm entry to
   advertise `127.0.0.1:38085` to the host client.
5. Record importer/server revisions, new schema versions, startup logs and
   actual data-loading results. The readonly v19 data may be incompatible
   with the pinned image; treat any version/missing-file error as a blocker,
   not permission to update the shared volume.
6. Create dedicated disposable accounts/characters only in the new DB using
   the server's actual account interface or a reviewed isolated fixture.
   Run identified-client login and multiplayer acceptance with clean per-run
   client settings. No fixture-based replacement of the server is required.

The audit alone did not establish image startup, source identity of runtime
images, DB/schema compatibility, v19 data compatibility, clean account setup,
login/world entry or gameplay scenarios; see the follow-up execution below. The volume inventory removes
the earlier uncertainty about missing vmaps/mmaps, but **ENV-03 remains open**.

Reproduction used `git rev-parse --verify HEAD`, filename inventories including
ignored files, selective file hashes/header reads, process/listener inventory,
`docker ps/images/volume inspect/system df`, `docker manifest inspect`, and
`docker buildx imagetools inspect` filtered to nonsecret identity fields.
Client-data inspection used `docker run --rm --network none --read-only
--mount type=volume,src=docker_ac-client-data,dst=/data,readonly ubuntu:24.04`
with directory counts, selected hashes and binary-header reads only.

## Follow-up isolated execution

Authorized local test project `wowee-eval-20260908-af5200` uses the four
pinned image digests above. Its compose file, generated private credentials,
configuration and sanitized diagnostic captures are under the locally ignored
`logs/fork-baseline/emulator/wowee-eval-20260908-af5200/` directory. Secrets
are not part of this evidence or the repository. Existing database/account
contents were not inspected or reused.

Docker mount inspection confirms the database uses only the newly created
`wowee-eval-20260908-af5200_db-data` volume and all services use the new
`wowee-eval-20260908-af5200-network`. The worldserver mounts
`docker_ac-client-data` with `RW=false`; the data initializer is absent. MySQL
has no published host port. Auth/world publish only `127.0.0.1:3724` and
`127.0.0.1:8085`, respectively; both were free before creation.

All four pinned images pulled successfully. The new MySQL became healthy
and the official importer reported `AzerothCore rev. a5e0e6b8f2bf+
2026-09-08 10:39:49 +0200 (master branch) (Unix, RelWithDebInfo, Static)
(dbimport)`. This is distinct from the local checkout revision. The `+`
marker is retained; the image digest fixes the artifact, but the log does
not establish a clean source checkout or identify every image-build change.

The importer exited with code **0**. The newly imported MySQL runtime reports
`8.4.11`; the world database reports `ACDB 335.17-dev`, cache ID `17`. Table
counts are 22 auth, 108 character and 312 world tables. Initial unknown-database
messages precede creation by the importer and are not evidence of a failed
import. Auth and world both report the same `a5e0e6b8f2bf+` runtime revision.
The world log reaches `ready...`, auth adds realm `AzerothCore` at
`127.0.0.1:8085`, and TCP connection probes to loopback 3724 and 8085 succeed.
These probes establish listeners, not authenticated client/world acceptance.

Two dedicated accounts, `WOWEE_EVAL_A` and `WOWEE_EVAL_B` (IDs 1 and 2,
expansion 2), were created only in the new auth database. The isolated fixture
uses fresh 32-byte salts and the AzerothCore SRP6 registration formula from
`src/common/Cryptography/Authentication/SRP6.cpp`: SHA-1 username/password
inner digest, little-endian exponent and verifier, generator 7 and the
source-defined 256-bit modulus. Passwords are generated and retained only in
the ignored private configuration. Realm ID 1 advertises the host loopback
endpoint. The character table contains **0 rows** at provisioning time;
character creation and actual client authentication remain subsequent tests.

**Explicit data blocker:** the pinned worldserver emits
`MMAP:loadMap: ... was built with generator v19, expected v20` for the existing
movement-map tiles. The server reaches readiness despite these rejections,
but pathfinding and full gameplay cannot be certified. The existing client
volume was not regenerated, modified or mounted writable. No pathfinding
setting was disabled to hide the mismatch. A compatible separately owned v20
data set or independently pinned compatible runtime remains necessary for
that acceptance gate. Auth/login/character UI tests can use this actual server
while recording the data limitation.

Nonsecret execution evidence under the ignored project directory includes
`pull.log`, `resource-isolation.json`, `db-import-startup.log`,
`auth-startup.log`, `world-startup.log`, `schema-evidence.log` and
`account-provision.log`. Auth's application revision is additionally in
`auth/logs/Auth.log`. The project remains running for subsequent client
testing. No pre-existing container, database, account or client-data content
was changed. ENV-03 remains partial pending actual identified-client
authentication, character/world entry, scenario coverage and compatible
pathfinding data.

## First identified real-client login attempt

`live-login-02` used the exact capture-validated Debug executable
`build-fork-windows/bin/Debug/wowee.exe`, SHA-256
`9136279b449772f891f9999594eb333c49f3086bc9d787a3bf8982d7959a93a2`.
The observed 1280x720 login screenshot placed the account field at
467..814 by 289..326; the trace clicked (640,306), pressed Return to focus
the password field, sent private SDL text input, and pressed Return. The
client logged loading its fresh fixture-local login configuration and started
authentication for dedicated account A against `127.0.0.1:3724`.

The real server returned a 32-byte salt challenge, but rejected LOGON_PROOF
with status 4. The client logged `s_nat=31`: its then-current
`SRP::computeProofs` converts salt, A and B to natural-length BigNum arrays,
whereas inspected AzerothCore `SRP6::VerifyChallengeResponse` hashes the
fixed-width arrays. This is a concrete candidate cause requiring a regression
and corrected-client rerun; no successful login is claimed. A private read-only
query of the new database confirmed account A's verifier matches the client's
uppercase username/password SHA-1 and little-endian exponent convention.
No credentials, salts, verifiers or session keys are published here.

The driver reported **fail**, despite process exit 0: all eight SDL events
completed, SDL_QUIT dispatched after 1800 completed update/render iterations,
and shutdown completed, but authentication/realm/character-list acceptance
markers were absent and auth errors were present. The server's rejection is
not treated as proof that the account does not exist; the challenge and
independent new-DB account verification establish otherwise.

The preceding `live-login-01` is **invalid-wrong-binary**: an older Release
executable was selected accidentally and its owned process was stopped. Its
fixture cwd and failed result were preserved; there is no evidence that old
executable honored the requested config-root override. No pre-existing user
files were restored or deleted speculatively. The driver now requires the
SHA-256 from separate build/capture evidence and rejects a mismatched binary
before reading credentials or creating any run output.

## Corrected client login: live-login-03

The rebuilt Debug executable SHA-256
`a319fbc1bd3c2be83338e74e87b56858a023bf8801709e4294c387e8232ab7f7`
was released after independent positive/negative GPU capture checks. The
real SDL trace repeated the same observed account-field click and used the
**unchanged account A password, salt and verifier**. Runtime evidence now
shows, in order, successful auth proof, realm-list receipt, successful world
authentication, `Found 0 character(s)`, and `Ready to select character`.

The driver reports **pass** for this bounded auth/realm/empty-character-list
scope: exit 0, eight completed SDL input events, normal SDL_QUIT dispatch
after 1800 completed update/render iterations, completed shutdown, validation
enabled and no ERROR/FATAL log entries. Evidence is in the ignored project's
`live-login-03/result.json` and `runtime/logs/live-login.log`. The optional PNG
is a **startup capture**, not the final character-list screen; its capture
acknowledgement does not establish the final visual state.

This establishes an identified real client's login and world authentication
to the isolated real emulator. It does not establish character creation,
world entry, multiplayer scenarios or gameplay/pathfinding correctness. The
server's v19/v20 movement-map mismatch remains unchanged and open.

## Delayed character-list capture: live-login-04

Exact Debug executable SHA-256
`4b4c8c78559e3aab51015652a9db2015de704cf48cc5f60c00a4d1e9dd355005`
repeated the unchanged account A trace and passed the same clean auth, realm,
world-auth and character-list checks. The log acknowledges screenshot queueing
after **600 completed update/render iterations** and subsequent save completion.
The delay is an update-count condition, not a server-state wait.

The PNG at the ignored project's `live-login-04/screenshot.png` was fully
decoded with Pillow as 1280x720 RGBA and visually inspected. It shows the
`Nobody here` notice, the statement that the account has no characters, and
Back, Refresh and New Hero buttons. The observed New Hero button occupies
approximately x711..856, y404..447; (780,425) is an interior point for a
subsequent actual UI trace. No creation was attempted in this run. The driver
exited 0 after all eight events and normal 1800-update shutdown, with no
ERROR/FATAL messages. This capture establishes the visible empty-list state;
character creation, world entry and gameplay gates remain open.

## Character-creation attempt: live-login-05

The same pinned `4b4c8c...355005` Debug binary, unchanged account A, and
observed New Hero click (780,425) were used for a real creation trace. Auth,
world authentication and the initial empty list succeeded. At completed
update 300 the trace clicked New Hero. The production creation screen loaded
the HumanMale model, composite skin and racial backdrop through
`CharacterPreview`; **before name-entry/submission**, frame 306 failed
`vkQueueSubmit` with `VK_ERROR_DEVICE_LOST` (-4). The driver reported an
invalid write at address 0 and later pending-command-buffer reset validation
errors during recovery. These observations associate the failure with opening
the preview; they do not identify a proven renderer root cause.

The client exited nonzero and the driver correctly reported **fail**. No
character-create success response, refreshed named list, completed input trace
or update-900 screenshot was observed. A read-only count against only the new
DB confirmed **0 characters** after the attempt. Its logs and
`character-count.log` are preserved under `live-login-05/`; no retry or direct
character-table mutation was performed. Character creation and world-entry
acceptance now have a concrete GPU failure blocker in addition to the
separately recorded movement-map compatibility limitation.

## GPU-assisted diagnostic replay: live-login-06

One diagnostic replay used the same pinned executable/account/creation input
with explicit `WOWEE_VULKAN_GPU_VALIDATION=1` and a 120-second timeout. The
log confirms the request and active layer instrumentation warnings. Auth/world
authentication succeeded, then opening the HumanMale character preview again
failed. GPU-AV reported `INVALID_EMPTY(): Internal Error, GPU-AV is being
disabled` with `Failed to wait for fence`. The process exited **3**, before
trace completion or normal shutdown; it did not reach the timeout. The
stdout log contains the same final diagnostic and no further shader-fault
identification. The new DB still contains zero characters.

This replay is **diagnostic failure**, not normal-mode certification or a
confirmed shader bounds diagnosis. No further replay was attempted. The
driver now exposes the opt-in `--gpu-validation` flag and identifies this
mode in its report, while continuing to strip inherited WOWEE overrides.

## Backdrop isolation replay: live-login-07

Debug binary SHA-256
`860f0bbfae466b509a137a0b264819ec27356a3b172cb7912b0b6cfd3497a57c`
repeated the creation trace with only `WOWEE_TEST_PREVIEW_NO_BACKDROP=1`,
normal validation enabled and GPU-AV off. The runtime explicitly logged
`Preview diagnostic: racial backdrop disabled`. Auth/world/empty-list
acceptance succeeded, but frame 306 again failed submission with device loss
and an invalid write at address zero, before creation submission. The
process stopped and the driver reported **fail**; no update-900 capture or
creation success occurred, and the new DB still has zero characters.

Disabling the backdrop alone does not prevent the observed failure. This
narrows the diagnostic result but does not prove that the model draw,
descriptor state, command submission or another specific component is the
root cause. No further replay followed this bounded isolation run.

## Model-draw isolation and authoritative creation: live-login-08

The same `860f0b...97a57c` binary was replayed with only
`WOWEE_TEST_PREVIEW_NO_MODEL_DRAW=1`; the backdrop remained enabled, normal
validation remained enabled, and GPU-AV was off. The normal UI trace sent
`CMSG_CHAR_CREATE` for `Woweetrial`; the real server returned success code
**47**, followed by a fresh character list containing that name. A read-only
query of only the new DB confirms GUID 1, account 1, race 1, class 1 and
level 1. Character creation occurred through the actual protocol, not a
direct character-table insertion.

The run is a **diagnostic-mode pass**: 16 input events, 1800-update normal
quit/shutdown, successful update-900 capture and no ERROR/FATAL log entries.
Pillow decoded the PNG as 1280x720 RGBA. Visual inspection shows the selected
Human Warrior Woweetrial on Choose a Hero; the model preview is blank under
the explicit isolation flag. The Enter World button is visible around
x910..1094, y594..638, but was not activated.

Disabling model draw avoided the observed device loss with the rest of this
preview path active. This is useful fault isolation, **not a repaired or
certified default renderer**. The driver reports `no-model-draw` and
`default_preview_certified=false`. Account A now has a character; future
traces must not assume its earlier empty-list state. World entry/gameplay
and the movement-map compatibility gate remain unverified.

## Constant fragment isolation: live-login-09

The `860f0b...97a57c` binary logged into account A with its existing character,
normal model/backdrop draws, normal validation and GPU-AV off. Only the fresh
runtime's `assets/shaders/character.frag.spv` was replaced with the compiled
constant-color diagnostic fragment SHA-256
`2698110bcbab8004a4038b55c5df87ddfde98b7a7655a612759d6fce9b805c84`.
The driver records original and replacement hashes; a post-run comparison
confirms the production shader remained unchanged.

Auth/world/list receipt succeeded, then opening Woweetrial's preview failed
at frame 55 with the same device-loss/invalid-write-at-zero signature. The
driver reported **fail** and no update-600 screenshot was produced. No
character creation was requested. A constant fragment alone does not avoid
the fault, narrowing the diagnosis beyond fragment lighting/material logic
without proving a vertex, index, descriptor or pass-lifecycle root cause.

## Bind-pose vertex plus constant fragment: live-login-10

One replay added the fixture-only bind-pose vertex shader SHA-256
`3e5f222559c10cab3d791534c75109f23599608daf5562dee0215a35b2aeeb2f`
to the previous constant fragment override, with the same pinned executable
and default model/backdrop draw path. Both fixture hashes were verified and
both production shader hashes remained unchanged after execution. The
vertex diagnostic preserves position/projection/model transforms and removes
bones/TBN work.

Auth/world/list receipt succeeded, then frame 55 again failed submission
with device loss and invalid write at zero. No graphics/upload checkpoint
was reported reached in this replay, and no update-600 capture was produced.
The driver reported **fail** and the process stopped. These two simplified
shader stages still do not prevent the failure; the result does not prove
a specific index, descriptor, pipeline or pass-lifecycle defect. No creation
or world entry was attempted.

## Synchronization-validation request: live-login-11

One unchanged-binary/default-shader/default-preview replay explicitly set
`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` and
`VK_KHRONOS_VALIDATION_SYNCVAL_SUBMIT_TIME_VALIDATION=true`, the installed
SDK settings names. GPU-AV was off. The driver records this as requested
synchronization validation; the runtime log does not independently confirm
activation of those layer settings.

Auth/world/list succeeded, then frame 56 failed submission with device loss
and invalid write at zero. The driver reported **fail**. No `SYNC-HAZARD` or
read-after-write diagnostic appeared before the fault, and no delayed capture
was produced. Ordinary pending-command-buffer reset validation errors followed
the loss. Lack of a synchronization diagnostic does not establish correctness
of upload/draw dependencies or rule out the independently inspected barrier
gap. No additional replay or character creation occurred.

## Buffer-dependency correction replay: live-login-12

The rebuild containing narrow buffer upload/draw dependencies
(commit `347dab9ef`), Debug binary SHA-256
`eda45028294806f9b1a46c134e8a83db660b383b707eb5ecefef98af2e4ff752`,
was tested with the same synchronization-validation request as run 11,
normal shaders/model/backdrop and GPU-AV off. Auth/world/named-list receipt
succeeded, but frame 55 again failed submission with device loss and an
invalid write at zero. No synchronization-hazard diagnostic or delayed
capture appeared. The driver reported **fail** and the process stopped.

The buffer-dependency correction did not resolve this observed preview fault.
Its correctness as an independent dependency fix is separate from the still
unproven cause of the GPU failure. No character creation, world entry or
additional replay occurred in this run.

## Offline scenario-contract regression review

After run 12, three failing-before unit regressions demonstrated that the
previous classifier could accept an account-name prefix, an account request
logged after authentication success, or protocol success logged after process
shutdown. The classifier now parses complete production INFO messages and
requires the intended endpoint/account, auth/realm/world success, parsed
character-list marker and ready marker in order. Normal completion must
follow the actual production order: quit dispatch, trace-completion marker,
then process exit acknowledgement. Creation additionally requires the named
CMSG request before code 47 and a fresh named list before quit. Failure
reasons identify missing phases without recording input payloads.

Eleven focused regressions pass. Read-only reclassification of saved runs
02 through 12 preserves every previous result: 03/04 pass their login scope,
08 passes its explicitly isolated rendering/creation scope, and the other
runs fail. No GPU or server replay was performed for this change. Input
dispatch remains the existing completed-update trace; these stricter observed
log-state assertions do not pretend that fixed scheduling has become a
server-conditioned input wait.
