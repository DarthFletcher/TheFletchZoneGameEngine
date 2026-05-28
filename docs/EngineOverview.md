# Engine Overview

TheFletchZone Game Engine is an editor-first DirectX 12 game engine foundation with a playable runtime loop, scene authoring tools, prefab workflow, and the Sinivault gameplay prototype.

This document is the high-level orientation guide for the engine. It explains what the major systems own, how editor state differs from runtime state, and what architectural rules should stay intact as the engine grows.

---

## Engine identity

The engine is currently built around four major pillars:

1. **Explicit DirectX 12 ownership**
   - deterministic frame flow
   - explicit device/swapchain/command ownership
   - controlled resize and present behavior

2. **Editor-first scene authoring**
   - docked editor UI
   - scene viewport
   - hierarchy/inspector/assets workflows
   - gizmo editing
   - prefab and scene persistence

3. **Runtime/play-mode separation**
   - editor `Scene` state remains the authoring model
   - `RuntimeWorld` is cloned from the scene for Play mode
   - gameplay uses runtime entities/components where possible

4. **Sinivault as the first game vertical slice**
   - player/camera runtime flow
   - vault node/core/exit/ring gameplay
   - campaign progression
   - scanner, tutorial, presentation, and diagnostics

---

## Core subsystem map

### `Engine`

`Engine` owns application-level flow.

Primary responsibilities:

- initialize and shut down engine systems
- own Play/Pause/Stop state
- own the high-level frame contract
- snapshot editor scene state when entering Play mode
- restore editor scene state when leaving Play mode
- coordinate `RuntimeWorld` clone/clear lifecycle

Important rule:

> `Engine` owns frame flow and Play mode state transitions. Do not let individual subsystems present frames or independently own play/edit transitions.

---

### `Graphics`

`Graphics` owns DirectX 12 rendering infrastructure.

Primary responsibilities:

- DX12 device and swapchain
- command queues/lists/allocators
- frame fences and synchronization
- render targets and descriptor heaps
- scene render targets
- ImGui backend integration
- frame health and device-loss diagnostics

Important rule:

> Keep DirectX 12 ownership explicit. Rendering work should happen inside the engine-owned frame model.

---

### `Scene`

`Scene` is the editor-facing authoring world.

Primary responsibilities:

- list of `SceneInstance` objects
- stable editor instance IDs
- transform/visibility/material authoring data
- camera authoring data
- selection and hover state
- hierarchy and parenting
- scene serialization
- prefab save/load/apply/revert flows
- scene render submission data
- picking support

Important rule:

> `Scene` is authoring state. Runtime gameplay should not treat editor scene data as the long-term simulation source of truth.

---

### `RuntimeWorld`

`RuntimeWorld` is the Play mode runtime world.

Primary responsibilities:

- clone editor scene data into runtime entities
- map runtime entities back to source `SceneInstance` IDs
- store runtime components
- expose runtime diagnostics
- provide runtime-to-scene transform sync while rendering still consumes `Scene`

Current runtime components include:

- transform
- camera
- mesh renderer
- player controller
- trigger volume
- vault node
- vault core
- vault ring
- vault exit

Important rule:

> Runtime gameplay should prefer `RuntimeWorld`. Syncing back to `SceneInstance` is currently a renderer compatibility bridge, not the desired final ownership model.

See also: `docs/RuntimeWorld.md`.

---

### `UI`

`UI` owns the editor shell.

Primary responsibilities:

- dockspace and menu bar
- command strip
- project browser
- scene save/load UI paths
- project creation/opening flows
- editor-level popups and menu actions

Important rule:

> UI should trigger engine/editor commands, not become the owner of runtime simulation rules.

---

### `EditorPanels`

`EditorPanels` owns individual editor panels.

Current major panels include:

- Scene
- Game
- Hierarchy
- Inspector
- Assets
- Diagnostics
- Log Viewer
- Instancing
- Material Preview
- Debug Overlay
- Prefab Workflow
- Prompt Helper
- Black Flame panels

The Diagnostics panel is especially important for runtime development because it exposes `RuntimeWorld` entity/component state.

---

### `Game`

`Game` currently orchestrates Sinivault gameplay.

Primary responsibilities:

- high-level game update flow
- player input handling
- camera/player runtime transform updates
- calling vault helper systems
- public gameplay state getters used by UI/Game View overlays

`Game.cpp` is no longer the only place where vault logic lives, but it still coordinates the main gameplay loop.

Important rule:

> Keep shrinking `Game.cpp` only when there is a safe, clear subsystem boundary. Do not split code just for the sake of splitting.

---

## Vault gameplay helper modules

Sinivault gameplay is split into focused helpers:

- `VaultRuntime`
  - shared vault runtime state structs and node type/state mappings

- `VaultDiscovery`
  - builds vault gameplay bindings from `RuntimeWorld` or scene fallback

- `VaultNodeSystem`
  - node binding lookup and runtime node sync

- `VaultObjectSystem`
  - core, exit, and ring runtime sync helpers

- `VaultMissionSystem`
  - mission state queries and mission counter updates

- `VaultPresentation`
  - tutorial, objective, overlay, context, and banner text helpers

- `VaultCampaign`
  - campaign scene routing and vault progression helpers

---

## Editor vs runtime model

The most important architecture split is:

```text
Editor authoring: Scene / SceneInstance
Runtime gameplay: RuntimeWorld / RuntimeEntity / RuntimeComponents
```

Current flow:

```text
Scene authoring data
    -> RuntimeWorld clone on Play
    -> gameplay updates runtime components
    -> runtime transforms sync back to SceneInstance
    -> current renderer draws Scene data
    -> Stop Play restores editor scene snapshot
```

The sync-back step exists because rendering still consumes scene data. It should stay explicit and easy to remove later.

---

## Frame flow

The engine frame is intentionally deterministic.

Typical frame shape:

1. process input/window messages
2. begin graphics frame
3. update engine/game/editor state
4. build editor dockspace and panels
5. render scene/game viewport targets
6. render ImGui draw data
7. end frame and submit GPU work
8. present once through `Engine`/`Graphics`

Important rule:

> Present must happen exactly once per frame and remain engine-owned.

---

## Diagnostics philosophy

Diagnostics are part of the engine architecture.

Current diagnostics cover:

- DX12/device health
- frame health
- resize behavior
- scene/render stats
- runtime world entity/component counts
- selected runtime entity details
- vault runtime validation warnings
- runtime lifecycle clear logs

Guideline:

> If a runtime/editor state bug would be confusing, add diagnostics or logs near the ownership boundary.

---

## Current game: Sinivault

Sinivault is the first full gameplay slice proving the engine.

It currently includes:

- player movement and camera follow
- runtime player/camera transform ownership
- vault nodes with decay/stabilization behavior
- vault core unlock/stabilize objective
- vault exit opening and escape trigger
- vault rings as progress feedback
- scanner guidance
- tutorial hints
- mission success/failure states
- multi-scene campaign progression

See also: `docs/SinivaultGameplay.md`.

---

## Current strengths

The strongest current architecture wins are:

- explicit DX12 frame ownership
- working editor workflow
- stable scene IDs
- prefab support
- runtime world clone and component layer
- runtime diagnostics
- playable Sinivault campaign loop
- focused vault helper modules

---

## Current pressure points

Known pressure points:

- `Scene.cpp` still owns many responsibilities
- renderer still consumes `Scene` data during Play mode
- runtime-to-scene sync is still required for visible runtime transforms
- `Game.cpp` still orchestrates the main gameplay loop
- scene/prefab serialization can be hardened further

These are acceptable current limitations, but they should guide future phases.

---

## Development rules

Use these rules when adding new systems:

1. Keep `Engine` in charge of frame flow and Play mode transitions.
2. Keep DirectX 12 ownership explicit and debuggable.
3. Treat `Scene` as editor authoring state.
4. Treat `RuntimeWorld` as Play mode simulation state.
5. Use explicit sync bridges where editor/runtime compatibility is needed.
6. Add diagnostics when crossing ownership boundaries.
7. Prefer small checkpoints with clear tags.
8. Avoid large rewrites unless a subsystem boundary is already obvious.

---

## Near-term direction

The best near-term engine work is:

- keep hardening runtime/editor separation
- keep improving diagnostics and validation checklists
- document gameplay systems before adding larger mechanics
- only move rendering toward runtime data after the current bridge stays stable
- add new gameplay features, like Vault Lords, on top of the runtime component model rather than by expanding `Game.cpp` indiscriminately
