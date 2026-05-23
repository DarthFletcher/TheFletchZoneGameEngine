# Phase 10 Play-Mode Validation Checklist

# Phase 10 Play-Mode Validation Checklist

Use this checklist after gameplay-system, runtime-sync, scene-load, or diagnostics changes.

## RuntimeWorld diagnostics

- Open the `Diagnostics` panel.
- Enter Play mode.
- Confirm `RuntimeWorld` entity/component counts are non-zero.
- Open `RuntimeWorld Summary`.
- Confirm vault scenes show `Vault runtime components look valid.`
- Confirm no missing-component warnings appear for valid campaign scenes:
  - `PlayerController` runtime entity
  - main camera runtime entity
  - `VaultNode` components
  - `VaultCore` component
  - `VaultRing` components
  - `VaultExit` component
- Select scene objects and confirm `Selected Runtime Entity` resolves to the expected runtime entity.
- Confirm selected runtime transform values update while moving during Play mode.
- Confirm selected runtime component labels match the selected object type:
  - player: `Transform`, `MeshRenderer`, `PlayerController`
  - main camera: `Transform`, `Camera`
  - vault nodes: `Transform`, `MeshRenderer`, `VaultNode`
  - vault core: `Transform`, `MeshRenderer`, `VaultCore`
  - vault rings: `Transform`, `MeshRenderer`, `VaultRing`
  - vault exit: `Transform`, `MeshRenderer`, `TriggerVolume`, `VaultExit`
- Confirm detailed component sections show expected values:
  - camera: enabled, main camera, FOV, near/far clip
  - mesh renderer: visible, material index, primitive type
  - player controller: move speed, look sensitivity, camera height
  - trigger volume: enabled, half extents
  - vault node: type, state, decay timer/duration, stabilize progress/duration, warning flag
  - vault core: unlocked, stabilized
  - vault ring: active, completed
  - vault exit: unlocked, opened, open offset Y
- Stop Play and confirm `RuntimeWorld Summary` does not show `RuntimeWorld contains entities while editing.` during normal editing.

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
- Confirm `RuntimeWorld` refresh logs appear after vault reset.
- Confirm `RuntimeWorld` refresh logs appear after vault scene transition.
- Stop Play and verify editor state is restored.
- Confirm runtime diagnostics clear when returning to Edit mode.
- Confirm no editor-authored transforms remain corrupted after Stop Play.
- Load a different scene while editing and confirm stale runtime entities do not remain.

## Regression watch list

- selected runtime entity ID is `0` during Play for valid scene objects
- player/camera transform mismatch between runtime and scene
- vault nodes fail to activate or decay incorrectly
- core unlock does not match active node count
- exit gate material or position does not update
- scene transition loads but runtime diagnostics show stale entities
- `RuntimeWorld contains entities while editing` appears after normal Stop Play or editor scene load
- vault validation warnings appear in valid campaign scenes
- selected camera lacks camera details
- selected mesh object lacks mesh renderer details
