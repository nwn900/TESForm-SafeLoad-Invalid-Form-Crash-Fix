# Compatibility and qualification

## Declared target

| Game | F4SE | Status |
| --- | --- | --- |
| Fallout 4 Windows PC 1.10.163.0 | 0.6.23.0 | Only qualified target for release 0.1.0 |

The exact callsite byte contract and cleanup discriminator were statically
checked against Fallout 4 1.10.163.0. A local session on that game/F4SE pair
reported `contract=exact-scan`, one unique candidate, and an installed hook.
Two numbered logs dated 2026-09-22 each recorded the hook reaching an invalid
editor-ID dispatch and reporting `prevented` with reason `UNREADABLE_VTABLE`.
The latest checked session log (2026-09-24 19:32 local time) had no guarded
call. The two earlier entries demonstrate that the guard path ran on this
runtime and substituted its placeholder; they do not show that the entire
save load completed or prove the unguarded counterfactual. The local
installation was Steam; no separate GOG qualification was performed.

## Not qualified

This release makes no compatibility claim for any other game runtime. Examples
include Fallout 4 1.10.980/1.10.984, 1.11.x, Fallout 4 VR 1.2.72, and Windows
Store versions. The scanner can look for an exact or conservative relaxed
signature on another PC runtime, but finding one is not the same as validating
that runtime's executable, ABI, behavior, or crash-prevention result. VR
installation is deliberately rejected by the hook installer.

“Not qualified” is a statement about available evidence—not a claim that the
same crash occurs only on 1.10.163, or that it cannot occur on another build.
The supplied crash path is from the 1.10.163 investigation; there is no
verified cross-version incidence matrix.

## Verification boundary

- F4SE loader and exact-signature hook installation were observed once on
  Fallout 4 1.10.163.0 / F4SE 0.6.23.0.
- The guard's synthetic scanner/validator tests are offline tests, not game
  tests.
- Two live prevented-dispatch incident records are part of the evidence, but
  no complete successful load/reload A-B result is available.
- Do not interpret a plugin load, a signature match, or an incident log alone
  as proof that the entire save load or subsequent gameplay is stable.
