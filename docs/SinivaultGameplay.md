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
- `Corrupted`
- `Relay`
- `Hidden`

### Normal nodes

Normal nodes activate immediately when interacted with.

They then begin their active/decay lifecycle.

### Slow-stabilize nodes

Slow-stabilize nodes require the player to remain nearby while stabilization progresses.

If the player moves too far away before completion, the node returns to inactive.

### Fragile nodes

Fragile nodes decay faster than normal nodes.

They create route pressure and should usually be refreshed earlier.

### Corrupted nodes

Corrupted nodes increase the decay rate of all active and decaying nodes by 10% while the corrupted node is active or decaying.

This pressure stacks multiplicatively with Vault Lord decay pressure.

### Relay nodes

Relay nodes reduce the decay rate of all active and decaying nodes by 10% while the relay node is active or decaying.

This relief offsets part of the pressure created by corrupted nodes and Vault Lords.

### Hidden nodes

Hidden nodes are presented as weak signals rather than identified nodes until the player reaches and stabilizes them.

Scanner and interaction language uses `Weak Signal` and `Stabilize Signal` to preserve this behavior.

### Node ecology diagnostics

The runtime Diagnostics panel reports a `Vault Node Ecology` count for each node type.

The Phase 12 validation scene expects:

- Normal: `1`
- Slow: `1`
- Fragile: `1`
- Corrupted: `1`
- Relay: `1`
- Hidden: `1`

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

Inactive scene objects are excluded from the play-mode runtime clone. An inactive node, or a node below an inactive parent, therefore does not render or participate in discovery, scanner targeting, interaction, gameplay, or node ecology diagnostics.

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

## Vault Lords

Vault Lords are currently presence/anomaly systems, not combat bosses.

The first Vault Lord prototype is designed around the idea that the vault is being watched or influenced by an ancient intelligence. A Vault Lord does not attack, chase, use health, or deal damage yet. Instead, it changes how the vault feels and applies subtle pressure to the existing node/core/exit loop.

Current Vault Lord behavior:

- runtime objects are classified by names or prefab paths containing `vaultlord`
- proximity to the player drives threat
- entering the influence radius marks the Vault Lord as discovered
- `discovered` remains true after first detection
- `active` is true while the player is inside the influence radius
- `threatLevel` rises as the player gets closer
- `distanceToPlayer` is tracked for diagnostics

Runtime state is stored in `VaultLordComponent`:

- `enabled`
- `discovered`
- `active`
- `threatLevel`
- `influenceRadius`
- `distanceToPlayer`

Presence rules live in `VaultLordSystem`.

The current design goal is:

> Vault Lords should create dread before danger.

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

The scanner points toward the next relevant objective or strongest active threat signal.

The scanner is intended to become a gameplay language, not just a waypoint arrow. It should help the player interpret the vault, prioritize routes, and notice anomalies.

Vault Lord threat currently overrides normal scanner objectives while active.

Current Vault Lord scanner language:

- `Scanner: Unknown Signal` can represent an undiscovered anomaly signal
- `Scanner: Threat Signal` represents a discovered Vault Lord threat
- `ANOMALY DETECTED` appears during threat scanner interference
- scanner strength becomes `Threat XX%` while tracking a Vault Lord
- scanner visuals shift toward red/purple interference
- scanner sweep jitter and glitch lines increase under threat

Expected target priority while activating nodes:

1. active Vault Lord threat signal
2. stabilizing node
3. decaying/unstable node
4. inactive vault node

Other states:

- `CoreUnlocked` points to the vault core
- `Completed` points to the exit

Scanner direction convention:

- `0` relative angle means target is straight ahead / screen-centered
- positive angle means target is to the right
- negative angle means target is to the left

The scanner uses the active camera view to align the arrow with what the player sees. If a target is centered on screen, the scanner arrow should point up.

Current scanner node classification uses:

- `Scanner: Normal Node`
- `Scanner: Slow Node`
- `Scanner: Fragile Node`
- `Scanner: Corrupted Node`
- `Scanner: Relay Node`
- `Scanner: Weak Signal`
- `Scanner: Unstable Node` for the most urgent decaying node

While a special node is stabilizing, the scanner uses its corresponding `Scanner: Stabilizing ...` label. Hidden nodes use `Scanner: Stabilizing Weak Signal`.

---

## Presentation and tutorial feedback

Presentation helpers live in `VaultPresentation`.

Current feedback includes:

- objective text
- tutorial header
- tutorial primary/secondary hints
- context hints for special node types
- scanner target label/distance/strength
- scanner anomaly/interference visuals
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
- Vault Lord detected
- Vault Lord threat pulse

Vault mood changes lighting, skybox, material color intensity, and audio tension.

Vault Lord presence also affects presentation:

- scanner interference becomes red/purple
- audio plays a first-detection stinger
- high threat can trigger periodic pulse/interference audio
- sky tint shifts toward darker purple
- sky intensity and exposure lower under threat
- ambient lighting lowers under threat
- node/core/ring material pulses become more intense
- strongest threat slightly increases audio tension

The current goal is atmospheric pressure, not combat music.

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
- `VaultLordComponent`

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
- vault lords, when a scene contains `VaultLord` objects
- selected runtime entity mapping when a scene object is selected
- detailed selected component state
- `Vault runtime components look valid.`

For selected Vault Lord objects, diagnostics should show:

- enabled
- discovered
- active
- threat level
- influence radius
- distance to player
- node decay pressure

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
- Vault Lords are presence/anomaly systems only; they do not have combat, health, attacks, or AI behavior yet

These are acceptable for the current milestone.

---

## Future gameplay direction

Likely next gameplay additions:

- node ecology expansion
- scanner node classification labels
- corrupted nodes
- relay nodes
- hidden nodes
- richer Vault Lord archetypes that alter vault rules
- hybrid camera modes
- 3D text / world labels
- stronger scanner/route feedback
- better campaign progression UI
- runtime-driven rendering path for Play mode

Potential Vault Lord direction:

- Vault Lords should act like ancient vault intelligences or anomalies first.
- Each Vault Lord can eventually influence node behavior, scanner reliability, atmosphere, and routing pressure.
- Combat should not be added until the presence/anomaly identity is proven.

Potential node ecology direction:

- `Relay Node`: supports or slows decay of nearby nodes
- `Corrupted Node`: creates unstable or misleading benefits
- `Hidden Node`: requires scanner interpretation
- `Anchor Node`: keeps core stability from collapsing
- `Overload Node`: activates quickly but creates later pressure

Important rule:

> New gameplay features should build on `RuntimeWorld` components and focused helper systems, not by re-centralizing everything into `Game.cpp`.
