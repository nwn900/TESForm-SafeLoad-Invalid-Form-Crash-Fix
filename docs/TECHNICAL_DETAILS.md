# Technical scope

On Fallout 4 1.10.163.0, the investigated non-Far-Harbor load failure reaches
`BGSSaveLoadGame::LoadGame` at RVA `0xCDF18A` (runtime address
`Fallout4.exe+0x0CDF18A`). The engine performs a virtual call through slot
`0x1D0` to obtain `TESForm::GetFormEditorID()`, then passes the returned string
to the form-type-mismatch warning before continuing changes-map cleanup.

The plugin scans the loaded `.text` section for the exact 38-byte context
around that call plus a discriminator for the expected warning/cleanup shape.
It accepts one unique exact match; if there is no exact match, it can accept a
single conservative relaxed match that preserves the stable call anchor and
cleanup shape. Missing, ambiguous, unreadable, conflicting, or otherwise
unqualified hook sites fail closed without patching. This cross-build scanner
behavior is not a cross-build compatibility claim.

The installer replaces only the six-byte indirect call with a trampoline call
to the guard. Before invoking the editor-ID function, the guard checks that:

- the form is non-null and readable through the form-type byte;
- the vtable is readable and its `0x1D0` slot lies in the game's `.rdata`;
- the slot target is executable and lies in the game's `.text`;
- the returned C string is readable and NUL-terminated within 512 bytes.

The guard does not call a virtual accessor while validating an object. Invalid
targets are reported and replaced with `"<invalid-form>"`; the existing
warning/cleanup branch remains in control. In Prevent mode, a narrowly filtered
access violation or in-page error during the validated editor-ID function is
also reported and replaced with the placeholder. Other exceptions are not
claimed to be recoverable.

Observe mode does not call an invalid function pointer: when a target cannot be
validated, it still uses the placeholder and labels the event `observed`.
Therefore Observe is not a byte-for-byte replay of the engine's original fault
behavior.

This guard neither edits save files nor repairs records. It does not cover the
separate Far Harbor `TESObjectCELL` crash reports or unrelated save-load
failures.

## Runtime observations for 0.1.0

Two logs dated 2026-09-22 on Fallout 4 1.10.163.0 recorded
`prevented invalid TESForm editor-ID dispatch` with reason
`UNREADABLE_VTABLE`. This is evidence that the hook executed and returned its
placeholder on the guarded path. The logs do not establish whether the full
load completed, whether a later failure occurred, or whether the unguarded
game would have crashed at that exact point. The latest inspected session on
2026-09-24 installed the hook but did not reach it.
