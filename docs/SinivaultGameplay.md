# Sinivault Gameplay Overview

Sinivault is the engine's first playable game vertical slice.

It exists to prove the editor, scene system, play-mode runtime world, runtime components, diagnostics, and gameplay architecture under real gameplay pressure.

---

## Core loop

The primary gameplay loop is:

```text
Activate vault nodes
    -> keep nodes active before too many decay
    -> unlock the vault core
    -> stabilize the core
    -> open the exit
    -> escape
    -> advance to the next vault
```

The player wins a vault by stabilizing the core and reaching the opened exit.

The player fails when too many nodes decay.

---

## Campaign flow

The current campaign sequence is:

1. `Main.scene` / `Vault_Intro.scene`
2. `Vault_Traversal.scene`
3. `Vault_Priority.scene`

Current campaign behavior:

- early vaults teach the loop
- later vaults increase traversal and routing pressure
- after escaping a vault, the player can advance manually or by auto-advance where supported
- the final vault can restart the campaign

Campaign routing is handled by `VaultCampaign`.

---

## Player and camera

The player/camera path now uses runtime ownership first.

Runtime flow:

1. `RuntimeWorld` finds the player controller entity.
2. `RuntimeWorld` finds the main camera entity.
3. movement updates the player runtime transform
4. camera follow/look updates the camera runtime transform
5. runtime transforms sync back to `SceneInstance` for current renderer compatibility

Player supports:

- keyboard movement
- sprint
- mouse look
- gamepad look/move where enabled
- frozen input during failure/escape presentation states

---

## Vault nodes

Vault nodes are the main routing pressure mechanic.

Node states:

- `Inactive`
- `Stabilizing`
- `Active`
- `Decaying`

Node types:

- `Normal`
- `SlowStabilize`
- `Fragile`

### Normal nodes

Normal nodes activate immediately when interacted with.

They then begin their active/decay lifecycle.

### Slow-stabilize nodes

Slow-stabilize nodes require the player to remain nearby while stabilization progresses.

If the player moves too far away before completion, the node returns to inactive.

### Fragile nodes

Fragile nodes decay faster than normal nodes.

They create route pressure and should usually be refreshed earlier.

### Runtime state

Node runtime state is stored in `VaultNodeComponent` and bridged through `VaultNodeSystem`.

Important runtime fields:

- type
- state
- decay timer
- decay duration
- stabilize progress
- stabilize duration
- warning played

---

## Vault core

The vault core is the second major objective.

Behavior:

- locked while not all nodes are active
- unlocks when the active node count reaches the total node count
- becomes the interaction target in `CoreUnlocked` mission state
- stabilizing the core completes the vault objective and opens the exit

Runtime state is stored in `VaultCoreComponent`:

- `unlocked`
- `stabilized`

Core state sync is handled by `VaultObjectSystem`.

---

## Vault exit

The vault exit is the final objective of each vault.

Behavior:

- locked before core stabilization
- opens after mission completion
- raises upward visually using an open offset
- triggers escape when the player reaches the exit area

Runtime state is stored in `VaultExitComponent`:

- `unlocked`
- `opened`
- `openOffsetY`

Exit state sync is handled by `VaultObjectSystem`.

---

## Vault rings

Vault rings provide visual progression feedback.

Behavior:

- rings activate as node progress increases
- rings complete when the vault mission is completed
- ring rotation is updated through runtime transform data first, then mirrored to the scene for rendering

Runtime state is stored in `VaultRingComponent`:

- `active`
- `completed`

Ring state sync is handled by `VaultObjectSystem`.

---

## Mission states

Current mission states include:

- `Inactive`
- `ActivatingNodes`
- `CoreUnlocked`
- `Completed`
- `Escaped`
- `Failed`

Mission helpers live in `VaultMissionSystem`.

Responsibilities include:

- reset mission counters after vault discovery
- track total and active nodes
- detect decay failure condition
- determine completion/escape/restart eligibility
- update core availability state

---

## Scanner behavior

The scanner points toward the next relevant objective.

Expected target priority while activating nodes:

1. stabilizing node
2. decaying/unstable node
3. inactive vault node

Other states:

- `CoreUnlocked` points to the vault core
- `Completed` points to the exit

Scanner direction convention:

- `0` relative angle means target is straight ahead
- positive angle means target is to the right
- negative angle means target is to the left

The scanner UI arrow uses that convention directly so the arrow points up when the target is straight ahead.

---

## Presentation and tutorial feedback

Presentation helpers live in `VaultPresentation`.

Current feedback includes:

- objective text
- tutorial header
- tutorial primary/secondary hints
- context hints for special node types
- scanner target label/distance/strength
- start and escape banners
- fail pulse
- end overlay title/subtitle

---

## Audio and mood

Sinivault uses gameplay audio events for player feedback.

Important events include:

- node activated
- node warning
- node decayed
- slow node started
- slow node completed
- core unlocked
- core stabilized
- exit opened
- escape triggered
- vault failure
- campaign completed

Vault mood changes lighting, skybox, material color intensity, and audio tension.

Current broad mood progression:

- tutorial/intro: calm
- traversal vault: tension
- priority vault: critical

---

## Runtime components involved

Sinivault currently uses these runtime components:

- `RuntimeTransformComponent`
- `RuntimeCameraComponent`
- `RuntimeMeshRendererComponent`
- `PlayerControllerComponent`
- `TriggerVolumeComponent`
- `VaultNodeComponent`
- `VaultCoreComponent`
- `VaultRingComponent`
- `VaultExitComponent`

These are cloned/classified from scene data by `RuntimeWorld` and `VaultDiscovery`.

---

## Diagnostics expectations

During Play mode in a valid vault scene, the Diagnostics panel should show:

- non-zero runtime entity/component counts
- player runtime entity
- main camera runtime entity
- vault nodes
- vault core
- vault rings
- vault exit
- selected runtime entity mapping when a scene object is selected
- detailed selected component state
- `Vault runtime components look valid.`

Warnings in this section usually indicate a scene classification or runtime clone issue.

---

## Restart and scene transition behavior

Vault reset and scene transition behavior should preserve the runtime/editor boundary.

Expected behavior:

- pressing `R` after failure or escape resets the run
- advancing to the next vault loads the next scene and refreshes `RuntimeWorld`
- stopping Play restores the editor scene snapshot
- runtime entities clear when returning to Editing state
- editor-authored transforms should not remain corrupted after Stop Play

---

## Current limitations

Known limitations:

- `Game.cpp` still orchestrates the main gameplay update loop
- Play mode rendering still depends on syncing runtime transforms back to `SceneInstance`
- vault object discovery still has scene fallback logic for compatibility
- scanner and UI feedback are tuned for the current vault campaign, not a generalized quest system

These are acceptable for the current milestone.

---

## Future gameplay direction

Likely next gameplay additions:

- Vault Lords
- richer runtime enemy or hazard components
- more node variants
- stronger scanner/route feedback
- better campaign progression UI
- runtime-driven rendering path for Play mode

Important rule:

> New gameplay features should build on `RuntimeWorld` components and focused helper systems, not by re-centralizing everything into `Game.cpp`.
