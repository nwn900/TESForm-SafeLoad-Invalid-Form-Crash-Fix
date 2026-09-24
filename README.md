# TESForm SafeLoad – Invalid Form Crash Fix

**TESForm SafeLoad** is a Fallout 4 Script Extender (F4SE) plugin for one
specific save-loading crash path: an invalid or stale `TESForm` pointer being
used for a virtual `GetFormEditorID()` call while Fallout 4 handles a
form-type-mismatch warning.

> **Release status:** 0.1.0 is an experimental prerelease. The plugin was
> observed installing its exact-signature hook on Fallout 4 1.10.163.0 and
> logged two intercepted invalid editor-ID dispatches (`UNREADABLE_VTABLE`)
> on 2026-09-22. Those entries show the guard substituted its placeholder;
> they do not establish that the complete save load succeeded or that the
> unguarded game would necessarily have crashed. End-to-end crash prevention
> remains **unverified**.

## Compatibility

The only qualified target for this release is:

- **Fallout 4 for Windows PC, runtime 1.10.163.0**
- **F4SE 0.6.23.0**
- Steam installation was the distribution tested locally; other distributors
  with the same displayed runtime were not separately checked.

No other Fallout 4 runtime is declared compatible by this release. This
includes 1.10.980/1.10.984, 1.11.x, Fallout 4 VR 1.2.72, and Windows Store
builds. These versions are **unqualified**, not evidence that the same crash
does or does not occur there. The plugin's signature scanner may recognize a
matching callsite on another PC build, but a match alone is not compatibility
or gameplay-prevention validation. VR is deliberately inactive.

See [the compatibility notes](docs/COMPATIBILITY.md) for the evidence and
limits behind this statement.

## What it does

In the reported non-Far-Harbor crash path, Fallout 4 obtains a form while
loading a save, detects a form-type mismatch, and asks that form for its editor
ID to construct a warning. On the qualified 1.10.163.0 executable, that
indirect virtual dispatch is at `Fallout4.exe+0x0CDF18A` and uses vtable slot
`0x1D0` (`TESForm::GetFormEditorID`). If the object or its vtable is stale or
invalid, the indirect call can fault before the warning and changes-map cleanup
complete.

TESForm SafeLoad scans the loaded executable for the unique callsite and its
surrounding warning/cleanup shape. It fails closed without patching when the
required signature is absent, ambiguous, or conflicting. At the guarded call,
it checks that the form memory is readable, that the vtable and target entry
belong to the expected game image sections, and that the returned editor-ID
string is readable and terminated within a bounded length. An invalid target
is replaced with the stable placeholder `"<invalid-form>"`, allowing the
existing warning and cleanup path to continue. In Prevent mode, an access
violation or in-page error raised inside the validated editor-ID function is
also contained and reported.

The plugin does **not** repair a save, rewrite a plugin, reconstruct a broken
form, or fix the underlying data that produced an invalid reference. It covers
only this save-load editor-ID dispatch; it is not a general invalid-form or
save-corruption fix. The reported Far Harbor `TESObjectCELL` crashes are
outside its scope.

## Install

Install with Mod Organizer 2 (or another mod manager) by adding the release
archive as a mod. The archive is rooted at the game's `Data` directory and
contains:

```text
F4SE\Plugins\SaveLoadGuardF4.dll
F4SE\SaveLoadGuardF4.ini
```

For a manual installation, extract the archive into the Fallout 4 game
directory's `Data` folder. Start the game through the matching F4SE loader.

The supplied INI enables Prevent mode. `Mode=Observe` still substitutes an
invalid/un-callable editor-ID target with the placeholder: it cannot safely
invoke the engine's invalid function pointer merely to reproduce a crash.

Logs are written to a new numbered file under
`Documents\My Games\Fallout4\F4SE\SaveLoadGuardF4.<n>.log`; older numbered
logs are not overwritten. A `working` line means the hook was reached, not that
a crash was prevented. A `prevented ...` incident line records a guard action,
but by itself does not prove that a whole load or later gameplay completed
successfully.

## Source and tests

The repository contains the plugin source and a small offline test target for
the signature scanner and memory validator. The tests exercise synthetic
images; they are not a substitute for an in-game crash reproduction.

Run the offline tests with CMake on Windows:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Building the F4SE DLL additionally requires MSVC x64, XMake, and a compatible
CommonLibF4 checkout. Set `COMMONLIBF4_ROOT` to that checkout, then run:

```powershell
xmake f -y --toolchain=msvc --toolchain_host=msvc -p windows -a x64
xmake build -y SaveLoadGuardF4
```

The DLL is emitted under `build\plugins`. The released binary is distributed
in the GitHub release asset; building from source does not by itself validate
runtime compatibility or crash prevention.

## Credits and references

- F4SE: [official site](https://f4se.silverlock.org/)
- Runtime and F4SE pair for the qualified target: Fallout 4 1.10.163 / F4SE
  0.6.23, as listed by the official F4SE site.
