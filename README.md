# FF12 TZA RNG Tracker — Tesla Overlay

Nintendo Switch 1 / Atmosphère 1.6.2 target overlay for **FINAL FANTASY XII THE ZODIAC AGE**.

Current version: **0.3.0**

This is an RNG monitor and optional RNG lock for Tesla overlays. By default it only reads the live MT19937 object and predicts future outputs. Enabling the lock writes the MT19937 state while the lock remains enabled.

Target Title ID:

`0100EB100AB42000`

## Current status

- The overlay builds successfully with the current devkitPro container.
- The generated overlay is listed and launchable from Tesla-Menu 1.1.2 with nx-ovlloader 1.0.6.
- Optional single-value RNG% locking with a background worker thread.
- Build output: `ovlff12rng.ovl` (approximately 341 KB with embedded NACP metadata).
- The host-side prediction tests can run without devkitPro.
- The default configuration targets Switch build `C2932C4D1C84ED7D`.

Validated target build ID:

`C2932C4D1C84ED7D`

## What it does

- Detects the current application through Atmosphère `dmnt:cht`.
- Reads the live process' memory through `dmnt:cht -> ReadCheatProcessMemory`.
- Uses the main NSO mapping supplied by dmnt, so ASLR does not require a hard-coded runtime address.
- Resolves the RNG through a MAIN-relative pointer, so ASLR and heap relocation are handled without hard-coded runtime addresses.
- Reads the MT19937 state (624 x 32-bit words) and index.
- Reconstructs/peeks future MT19937 outputs.
- Displays the raw RNG, `RNG % 100`, the next N percentage values, and a configurable success probability/range in the Tesla overlay.
- Can lock the next RNG percentage to a single selected value.

## RNG lock

Version 0.3.0 adds an optional write mode:

- Select **Lock value** to cycle the desired value in the range `00..99`. Each press advances by one and wraps from `99` to `00`.
- Select **Lock** to write that single value into the current and future MT19937 state words.
- Every subsequent game RNG draw resolves to the selected percentage while the lock remains enabled.
- Select **Lock** again to turn the lock off.

The lock preserves the high-order bits of each tempered output, changing only the value modulo 100. This minimizes distortion while forcing the displayed and game-facing percentage. Because FFXII TZA uses standard MT19937, the UI prediction is generated from the same state that the lock writes.

While **Lock** is enabled, a lightweight worker thread reasserts the lock approximately every 50 ms. The lock therefore continues after focus returns to the game and survives MT19937 twists that reset `mti` to `0/624`. Returning to the Tesla plugin list destroys the plugin GUI and disables the lock; reopening it shows **Lock** as `OFF`, which is expected. Press **Lock** again to re-enable it.

Locking modifies live game memory through `dmnt:cht -> WriteCheatProcessMemory`. Keep the feature disabled while you want pure observation.

### Important lock semantics

- The lock controls `RNG % 100`, not the complete 32-bit raw RNG value.
- The high-order bits of each MT19937 output remain variable, so formulas that use the full raw value can still produce different results. This is expected for actions such as Cure, whose healing calculation can consume more information than the two-digit percentage view.
- A fixed full-raw-value lock would require preserving or choosing one exact 32-bit value; the current version intentionally does not do that.
- If the plugin UI is open, its regular refresh also maintains the lock. When focus returns to the game, the dedicated worker thread continues maintaining it every approximately 50 ms.

## Important FFXII RNG note

Existing FFXII TZA RNG tools document a 0..99 percentage view derived from the RNG and note that many manipulable game events use `RNG value mod 100`. This overlay therefore exposes that same `mod 100` view, but the meaning of a particular success range remains event-specific. Configure `success_min` / `success_max` for the event you are observing.

## RNG address configuration

The default configuration is for Switch build `C2932C4D1C84ED7D` and uses this pointer chain:

```text
pointer slot  = MAIN + 0x0178F1A0
RNG object    = *(uint64_t *)pointer slot
state         = RNG object + 0x00000258
mti           = RNG object + 0x00000C18
```

The important correction is that `0x0178F1A0` is **not** the MT19937 state address. It is a pointer slot in MAIN. Reading the state directly at that address would interpret the pointer bytes as MT state and produce invalid predictions. The state and index are located relative to the dereferenced pointer value.

The Atmosphère cheat source for this build uses one `580F` master-code operation. It reads the RNG object pointer from the MAIN-relative slot, then adds the MT19937 field offsets to that pointer. Consequently `main_pointer` performs exactly one dereference.

The MAIN-relative slot stores a normal ARM64 64-bit pointer. Set `pointer_size=64`.

The index is a signed 32-bit `mti`. Values `0..624` are valid; `624` means the next output triggers an MT19937 twist. `625` is accepted as the conventional uninitialized marker, but a value in that range should be treated as a signal to re-check the target rather than as an ordinary in-game cursor.

### Verified display semantics

The `RAW` and `RNG%` fields show the next MT19937 output that the game has not consumed yet. In other words, they correspond to the next value that an RNG Cure list labels as the current action. After the action is consumed, `mti` advances and the overlay moves to the following value.

This was verified in Switch build `C2932C4D1C84ED7DEF7CEB1042B78951`:

| Action | Cure-list raw before action | Overlay `RAW` before action | Cure-list raw after action | Expected overlay `RAW` after action |
|---|---:|---:|---:|---:|
| Cure (591 HP) | 1447767833 | 536461266 (`0x1FF9BFD2`) | 536461266 | 3115232946 (`0xB9AEAEB2`) |
| Cure (572 HP) | 536461266 | 3115232946 (`0xB9AEAEB2`) | 3115232946 | 2093562269 (`0x7CC9399D`) |
| Cure (623 HP) | 3115232946 | 2093562269 (`0x7CC9399D`) | 2093562269 | The next MT19937 output |

Each Cure advanced `mti` by exactly one, from 10 to 12 in the captured sequence, and the `Next RNG%` table shifted by one position after every action. This confirms that the pointer chain, MT19937 state, cursor, tempering, and read-only live refresh are correct.

`99` is highlighted in red and `00` in green in both the current `RNG%` field and the `Next RNG%` table. All other values use the normal text color.

Supported address modes:

- `main_pointer`: `rng_offset` is a MAIN-relative pointer slot; `state_offset` and `index_offset` follow the dereferenced pointer.
- `main`: `rng_offset` is a MAIN-relative object address.
- `heap`: `rng_offset` is a heap-relative object address.
- `absolute`: `rng_offset` is an absolute object address.
- `pointer_size`: selects `32` or `64` when dereferencing `main_pointer`.

The default layout is:

```ini
title_id=0x0100EB100AB42000
rng_base=main_pointer
pointer_size=64
rng_offset=0x0178F1A0
state_offset=0x00000258
index_offset=0x00000C18
poll_ms=100
preview_count=24
success_min=0
success_max=0
```

For this layout, `index_offset - state_offset == 0x9C0`, which is exactly `624 * 4`. Older direct-address configurations using `state_offset=0` and `index_offset=0x9C0` remain possible with `rng_base=main`, but they are **not** the correct default for build `C2932C4D1C84ED7D`.

## Quick start

Build with the provided Docker wrapper:

```sh
./build-docker.sh
```

The wrapper mounts the current directory in the official `devkitpro/devkita64` image and runs `make`. The final overlay is written to `ovlff12rng.ovl`.

After a successful build, install the generated overlay and configuration file as described in [Install](#install). If the overlay was previously copied to the SD card, overwrite that old copy; Tesla-Menu does not automatically reload files changed while it is open.

## Build dependencies

Use a normal devkitPro Switch setup:

- devkitA64
- libnx
- a Tesla overlay loader such as `nx-ovlloader`
- libtesla

The Makefile expects libtesla at `lib/libtesla`.

Initialize the submodule first if `lib/libtesla` is empty:

```sh
git submodule update --init --recursive
```

Alternatively, build with a local devkitPro installation:

```sh
export DEVKITPRO=/opt/devkitpro
make
```

### Clean build

Use `make clean` before rebuilding from a clean state. When building in Docker, clean through the same container so files generated by the `nobody` user can be removed:

```sh
docker run --rm -v "$PWD:/project" -w /project \
  -e DEVKITPRO=/opt/devkitpro devkitpro/devkita64 make clean
./build-docker.sh
```

Run the host-side MT19937 verification without devkitPro:

```sh
./tests/run_host_tests.sh
```

## Install

Copy the generated `.ovl` to:

`sdmc:/switch/.overlays/ovlff12rng.ovl`

Copy the example configuration to:

`sdmc:/config/ff12rng/config.ini`

The exact filename is required. Rename `config.ini` if your SD card tool changes it during transfer.

Reboot the console after installing or replacing the overlay. Open Tesla while FFXII TZA is running, then select **FF12 TZA RNG Tracker** from the overlay list.

### Expected startup states

- The entry appears in Tesla-Menu as **FF12 TZA RNG Tracker**.
- If `config.ini` cannot be opened, the overlay reports a configuration error and its expected path.
- If the cheat process is unavailable, it reports that `dmnt:cht` is unavailable or that it cannot attach.
- If the pointer slot, pointer value, state address, or `mti` is invalid, the overlay reports the specific read error.
- While another title is attached, it reports `waiting for FFXII TZA`.

### Configuration path

The overlay mounts the SD card through libnx before reading:

`sdmc:/config/ff12rng/config.ini`

The file must be a regular file with that exact name. Verify that it is not `config.ini.txt`, `config (1).ini`, or a desktop shortcut. Keep the example comments; the parser ignores them.

## Troubleshooting

- `Please set DEVKITPRO in your environment` means the local devkitPro environment variable is missing. Use `./build-docker.sh`, or set `DEVKITPRO` before running `make`.
- If Docker cannot resolve the devkitPro image, pull the image explicitly with `docker pull devkitpro/devkita64`, then rerun `./build-docker.sh`.
- Confirm that `lib/libtesla` is initialized before using a local devkitPro toolchain.
- If Tesla-Menu does not list the plugin, verify that `sdmc:/switch/.overlays/ovlff12rng.ovl` is the newly built file. Tesla-Menu 1.1.2 silently skips overlays without embedded NACP metadata.
- The compatible v0.3.0 build embeds NACP metadata and is approximately 341 KB. Older builds without NACP or with the previous pattern-lock UI should be replaced.
- Replace the `.ovl` file while Tesla is closed or from a PC, then reboot the console. Do not merely rename or copy alongside the old file; the exact path above must contain the new build.
- If the overlay still reports `CONFIG ERROR / cannot open config`, verify the exact SD-card path above. The overlay handles mounting the SD card internally; no separate mount step is required.
- If the status is online but percentages look wrong, compare the displayed pointer chain (`Slot`, `Obj`, `State`, `Mti`) with a memory editor. The lock should only be used on the validated build.
- If the lock appears to stop after MT19937 wraps to `Mti: 0/624`, install the v0.3.0 build with the background lock worker. Older builds only rewrote the state while the plugin UI was actively refreshing.

## dmnt API correction

Atmosphère's documented cheat/debug IPC is `dmnt:cht` (not `dmnt:chk`). The service includes `ForceOpenCheatProcess`, process metadata/mappings, and read/write process memory. Internally dmnt implements these operations through the kernel debug SVCs.

The overlay intentionally uses the `dmnt:cht` path instead of opening its own debug handle. That lets Atmosphère's dmnt own the debug session/event handling and avoids making the Tesla overlay itself responsible for debug-event continuation.

## Limitations

1. The default RNG offsets are build-specific and target `C2932C4D1C84ED7D`; another game update needs a new pointer-chain offset set.
2. The optional RNG lock modifies the MT19937 state; it is off unless explicitly enabled.
3. The lock fixes the derived percentage, not the full 32-bit raw value; game formulas using the full raw value may still vary.
4. The displayed percentage is a derived `MT19937 output % 100`, matching the convention used by public FFXII TZA RNG helper tools; it is not a universal statement that every game action has that exact probability rule.
5. Reading the state and index is a live-memory snapshot; the game may advance the RNG concurrently with the read. The implementation minimizes this window by reading the configured state/index span in one dmnt transaction.
6. The UI displays a compact preview, while the reader internally captures all 624 state words required for MT19937 prediction.

## Source layout

- `source/main.cpp` — Tesla GUI, polling loop, and target-title filtering
- `source/dmnt_cht.*` — minimal `dmnt:cht` IPC client
- `source/ff12_rng.*` — MT19937 state reader, predictor, cursor tracking, success calculations
- `source/config.*` — simple INI parser
- `config/config.ini` — example configuration
- `.gitmodules` — libtesla dependency declaration
- `THIRD_PARTY.md` — dependency/license pointers
