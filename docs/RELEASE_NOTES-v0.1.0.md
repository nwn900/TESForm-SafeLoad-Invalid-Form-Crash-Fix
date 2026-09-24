# TESForm SafeLoad – Invalid Form Crash Fix 0.1.0

First public experimental prerelease.

- Adds an F4SE hook around the Fallout 4 1.10.163.0 save-load
  `TESForm::GetFormEditorID()` dispatch used by a form-type-mismatch warning.
- Validates the form, vtable slot, function entry point, and bounded editor-ID
  string before allowing the virtual call.
- Uses a placeholder and incident logging for an invalid target; fails closed
  when the callsite scan is missing, ambiguous, or conflicting.
- Includes the default INI and MO2-ready archive layout.

The hook was observed installing on Fallout 4 1.10.163.0 / F4SE 0.6.23.0. Two
logs from 2026-09-22 recorded intercepted invalid editor-ID dispatches with
reason `UNREADABLE_VTABLE`. The latest checked session (2026-09-24 19:32 local
time) installed the hook but did not reach it. The intercepted events do not
establish successful completion of the full load or the unguarded
counterfactual, so end-to-end crash prevention remains unverified. Only that
game/F4SE pair is qualified for this release; other runtimes are unverified
rather than known affected or known unaffected.
