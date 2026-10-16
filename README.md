# bg3-achievement-enabler-macos

Keeps Steam/GOG achievements enabled in **Baldur's Gate 3 on Apple Silicon Macs** while mods are loaded.
No Script Extender, no NativeModLoader, nothing injected at runtime: a small command-line tool patches three
instructions in the game executable, and can put them back.

This is a clean-room reimplementation of what Nexus Mods mod 24458 ("Achievement Enabler for macOS - Apple
Silicon") describes on its mod page. None of that mod's files or source were used. Everything here comes from
the game's own symbol table and disassembly.

> This is not a `.pak` mod. BG3 decides whether to grant achievements in native code, which data, Osiris and Lua
> mods can't reach on macOS. So there's nothing to pack: you run a small command-line tool.

## How it works

On the server side, each of these functions walks the loaded modules and gives up as soon as one isn't a
standard Larian module:

| Function                                              | Check                                                |
|-------------------------------------------------------|------------------------------------------------------|
| `esv::AchievementManager::UnlockAchievement`          | `bl ls::ModuleShortDesc::IsStandardModule` → `tbz w0, #0, <bail out>` |
| `esv::AchievementManager::IncreaseAchievementCounter` | same                                                 |
| `esv::OsirisGameFunctions::UnlockAchievement`         | same                                                 |

`on` replaces each of those three `bl` calls with `mov w0, #1`, so every module looks standard *to the
achievement code only*. `IsStandardModule()` itself is untouched, and so are its other callers (mod browser,
mod-mismatch warnings, savegame and load-order checks).

The patch sites aren't hardcoded. Each run parses the Mach-O, finds the arm64 slice, looks up the four symbols,
and requires exactly one `bl IsStandardModule` + `tbz w0, #0` pair in each function's prologue. If anything is
off (missing symbol, zero or several matches, unexpected bytes), the tool reports **UNKNOWN BUILD / BYTES** and
writes nothing. So it should keep working across game updates, as long as Larian doesn't restructure these
functions.

Verified on Steam build `25605617` (arm64 UUID `3B6F6492-114E-3EA2-BC64-DFDDBD361434`).

### Safety details

- **Backup:** before the first patch of a build, the untouched executable is copied to
  `~/Library/Application Support/bg3-achievement-enabler-macos/backups/<arm64 UUID>/`. The copy is an APFS clone,
  so it's instant and takes no extra space until the files diverge. A backup from an older build is deleted when
  a new build gets backed up.
- **Never edited in place:** the patched copy is staged next to the executable, signed, verified with
  `codesign --verify --strict`, and then `rename()`d over the original. A failure at any step leaves the game
  untouched.
- **Signature:** changing bytes invalidates Larian's Developer ID signature, and arm64 code must be signed to run.
  The tool re-signs the executable ad hoc (identifier `com.larian.bg3` kept). The game has no hardened runtime,
  so its Larian-signed frameworks still load. The bundle-level seal is left alone; Steam's own install already
  fails it, because the 145 GB `Contents/Data` folder isn't sealed. `restore` brings back the original,
  Larian-signed file byte for byte.
- It refuses to run while the game is running.

## Get the tool

The repo ships a ready-to-run binary at `bin/bg3ae-macos`, for Apple Silicon Macs on macOS 14 or later. It's a
single self-contained file that only links macOS's own system libraries, so nothing else needs installing.

To build it yourself instead, you need Xcode command line tools (or Homebrew LLVM), CMake ≥ 4.0 and Ninja. From the
repo folder:

```bash
cmake --preset release
```

```bash
cmake --build build/release --config Release
```

`--config Release` is needed because the presets use the Ninja Multi-Config generator. The result is
`build/release/Release/bg3ae-macos`; use that path wherever this README says `bin/bg3ae-macos`.

## Two ways to run it

Both run the same program. The only difference is how you type the command in Terminal. Open Terminal and `cd` into
the repo folder first.

### Option 1: copy it into `/usr/local/bin` and run it by name

When you type a command without a path, the shell looks for it in the folders listed in your `PATH`.
`/usr/local/bin` is on every Mac's default `PATH`, so a program copied there runs by name from any folder, like `git`.
Your `PATH` itself doesn't change.

```bash
sudo mkdir -p /usr/local/bin
```

```bash
sudo cp bin/bg3ae-macos /usr/local/bin/
```

`sudo` asks for your Mac password, because `/usr/local/bin` normally belongs to the system. The first command only
creates the folder if it doesn't exist yet. Then, from any folder:

```bash
bg3ae-macos status
```

To update it, copy the new file over the old one the same way. To remove it, delete `/usr/local/bin/bg3ae-macos`.

### Option 2: run it by its path

No copying: you type where the file is. From the repo folder:

```bash
./bin/bg3ae-macos status
```

`./` means "in this folder". Typing just `bg3ae-macos` here gives `command not found`, because the shell only
searches the `PATH` folders. From any other folder, use the full path, for example
`~/Downloads/bg3-achievement-enabler-macos/bin/bg3ae-macos status`. You can also move the file somewhere handy, like
the Desktop, and run `~/Desktop/bg3ae-macos status`.

Since you usually run the tool once per game update, this is the lighter option: deleting the file removes it
completely. If the file sits in Desktop, Documents or Downloads, macOS may ask once whether Terminal can access that
folder. Allow it, or the tool can't start.

### If macOS refuses to run it

- **A dialog says Apple could not verify it is free of malware:** the file was downloaded through a browser, for
  example as the repo's ZIP, and macOS quarantined it. The binary is only ad-hoc signed, not notarized. A
  `git clone` doesn't set the quarantine flag. To clear it:

  ```bash
  xattr -d com.apple.quarantine bin/bg3ae-macos
  ```

- **`permission denied`:** the file lost its executable flag. Run `chmod +x bin/bg3ae-macos`.

Whichever option you pick, the backup the tool makes lives separately in
`~/Library/Application Support/bg3-achievement-enabler-macos/backups/`. Keep that folder while the game is patched,
because `restore` needs it to bring back Larian's original signature. Without it, Steam's "Verify integrity of game
files" still restores the executable.

## Use

Quit Baldur's Gate 3 completely. The commands below use option 1. With option 2, type the file's path instead of
`bg3ae-macos`, for example `./bin/bg3ae-macos status`.

```bash
bg3ae-macos status
```

You should see three `[original]` sites and `Patch:  OFF`. If you see `UNKNOWN BUILD / BYTES`, stop: this build
isn't supported.

```bash
bg3ae-macos on
```

This ends with `Achievement patch: ON`. Launch the game normally from Steam or GOG.

| Command   | Effect                                                                                   |
|-----------|------------------------------------------------------------------------------------------|
| `status`  | Shows the detected install, build UUID, each patch site, the backup, and ON/OFF/PARTIAL  |
| `on`      | Backs up the executable (first time per build), then patches it                          |
| `off`     | Writes the three original instructions back. The file stays ad-hoc signed               |
| `restore` | Copies the backup back. Byte-identical to the original, Larian signature included        |

Options: `--app "<path>/Baldur's Gate 3.app"` or `--exe <path>` to skip auto-detection (Steam libraries from
`libraryfolders.vdf`, then `/Applications` and `~/Applications` for GOG), and `--backup-dir <dir>`.

Exit codes: `0` ok, `1` error, `2` unsupported build (nothing changed).

## Testing in game

1. `status` shows `Patch:  ON`.
2. Start the game with at least one non-Larian mod active in the Mod Manager. Any "mods disable achievements"
   notice in the menus may still appear, because only the server-side achievement checks are bypassed.
3. Earn an easy achievement on a fresh or existing save. In Act 1, for example, "Descent from Avernus" unlocks
   by escaping the nautiloid. Check that the Steam overlay (Shift+Tab) or GOG Galaxy pops it.
4. Counter-based achievements (`IncreaseAchievementCounter`) should advance too.
5. If the game is killed at launch (crash report says *Code Signature Invalid*), run `restore`. Please report it,
   because the ad-hoc signing assumption would then be wrong for your macOS version.

## After a game update or "Verify integrity of game files"

Steam replaces the executable, so the patch is gone and the game is back to stock. Run `status` again. If the new
build's sites are still recognised, run `on`, which also takes a fresh backup. If it says
`UNKNOWN BUILD / BYTES`, the functions changed and the tool needs updating.

## Caveats

- Apple Silicon only: just the arm64 slice is patched. Running the game under Rosetta (x86_64) isn't covered.
- GOG installs in `/Applications` may need Terminal to have **App Management** permission (System Settings →
  Privacy & Security), or the final rename fails with "Operation not permitted".
- Modifying the game executable may be against Steam/GOG/Larian terms. Achievements are client-reported, so use
  your own judgement.
