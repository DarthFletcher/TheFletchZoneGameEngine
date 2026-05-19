# Phase 10 Play-Mode Validation Checklist

Use this checklist after gameplay-system or runtime-sync changes.

## RuntimeWorld diagnostics

- Open the `Diagnostics` panel.
- Enter Play mode.
- Confirm `RuntimeWorld` entity/component counts are non-zero.
- Select scene objects and confirm `Selected Runtime Entity` resolves to the expected runtime entity.
- Confirm selected runtime component labels match the selected object type:
  - player: `Transform`, `MeshRenderer`, `PlayerController`
  - main camera: `Transform`, `Camera`
  - vault nodes: `Transform`, `MeshRenderer`, `VaultNode`
  - vault core: `Transform`, `MeshRenderer`, `VaultCore`
  - vault rings: `Transform`, `MeshRenderer`, `VaultRing`
  - vault exit: `Transform`, `MeshRenderer`, `TriggerVolume`, `VaultExit`

## Vault gameplay loop

- Start from `Main.scene` or `Vault_Intro.scene`.
- Press Play.
- Verify player movement and camera follow.
- Activate all vault nodes.
- Verify node decay, warning, refresh, slow-stabilize, and fragile behavior where applicable.
- Stabilize the vault core.
- Verify the exit gate opens and the escape trigger works.
- Advance through `Vault_Traversal.scene` and `Vault_Priority.scene`.
- Verify the final vault can restart the campaign.

## Reset and transition behavior

- Press `R` after failure or escape and verify the run resets.
- Stop Play and verify editor state is restored.
- Confirm runtime diagnostics clear when returning to Edit mode.
- Confirm no editor-authored transforms remain corrupted after Stop Play.

## Regression watch list

- selected runtime entity ID is `0` during Play for valid scene objects
- player/camera transform mismatch between runtime and scene
- vault nodes fail to activate or decay incorrectly
- core unlock does not match active node count
- exit gate material or position does not update
- scene transition loads but runtime diagnostics show stale entities
