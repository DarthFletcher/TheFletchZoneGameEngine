# RuntimeWorld Architecture

`RuntimeWorld` is the engine's play-mode runtime world foundation.

It exists to separate editor authoring state from runtime simulation state while keeping the current renderer/editor compatible with the existing `Scene` system.

---

## Purpose

`Scene` is the editor-facing authoring model.

`RuntimeWorld` is the play-mode simulation model.

The important ownership rule is:

- `Scene` owns authored objects, selection, hierarchy, serialization, prefab data, and editor state.
- `RuntimeWorld` owns runtime entities, runtime components, and play-mode gameplay state.
- During Play mode, gameplay should prefer `RuntimeWorld` data.
- For now, runtime transforms are synced back to `SceneInstance` only because the renderer still consumes `Scene` data.

This makes `RuntimeWorld` a compatibility bridge today and the foundation for stronger runtime ownership later.

---

## Lifecycle

### Entering Play mode

When Play mode begins:

1. `Engine` captures an editor scene snapshot.
2. `RuntimeWorld::CloneFromScene()` clones current `SceneInstance` data.
3. Runtime entities are created with source scene instance IDs.
4. Runtime components are created from scene data and gameplay classification.
5. Engine state changes to `Playing`.

### Pausing Play mode

Pause does not clear or clone `RuntimeWorld`.

It only changes the engine state between `Playing` and `Paused`.

### Stopping Play mode

When Play mode stops:

1. The editor scene snapshot is restored.
2. Editor selection/material focus is restored.
3. `RuntimeWorld` is cleared.
4. Engine state returns to `Editing`.

### Editor scene loads and new scenes

When the engine is in `Editing` state, editor scene loads and new-scene actions clear `RuntimeWorld` if it contains stale runtime entities.

These clears are logged through the runtime lifecycle logging path.

### Runtime vault reset / transition

During Play mode, vault reset and vault scene transition do not simply clear `RuntimeWorld` as an editor action.

Instead, the loaded runtime scene is refreshed with `RuntimeWorld::CloneFromScene()` so gameplay continues with a fresh runtime clone.

---

## Entity model

A runtime entity is represented by `RuntimeEntity`.

Each runtime entity stores:

- `RuntimeEntityId id`
- `uint32_t sourceSceneInstanceId`
- `RuntimeEntityId parent`
- `std::string name`

`sourceSceneInstanceId` is the bridge back to the editor `SceneInstance` that produced the runtime entity.

Important helpers include:

- `FindBySourceSceneInstanceId(...)`
- `GetEntity(...)`
- `FindMainCameraEntity()`
- `FindFirstPlayerControllerEntity()`

---

## Runtime components

`RuntimeWorld` currently stores components in per-type maps keyed by `RuntimeEntityId`.

Current component types include:

- `RuntimeTransformComponent`
- `RuntimeCameraComponent`
- `RuntimeMeshRendererComponent`
- `PlayerControllerComponent`
- `TriggerVolumeComponent`
- `VaultNodeComponent`
- `VaultCoreComponent`
- `VaultRingComponent`
- `VaultExitComponent`

This is intentionally simple. It is not a full ECS yet.

The goal is clear ownership and debuggability before introducing more complex runtime architecture.

---

## Scene cloning

`RuntimeWorld::CloneFromScene()` creates runtime entities from the current scene.

For each `SceneInstance`, it clones:

- transform data
- mesh renderer data when the instance has a renderable primitive
- camera data when the instance has an enabled camera
- parent mapping after all runtime entities have been created

It also classifies gameplay objects based on current scene data and naming conventions.

Current vault classification recognizes common scene names such as:

- `VaultRunner`
- `VaultCore`
- `VaultNode_*`
- `VaultRing_*`
- `VaultExitGate`

This gives gameplay a component-based view of authored scene objects.

---

## Runtime-to-scene sync bridge

The renderer still draws from `SceneInstance` data.

Because of that, Play mode currently uses a sync bridge:

1. gameplay updates runtime components first
2. runtime transforms are synced back to matching `SceneInstance` objects
3. the existing renderer draws the synced scene data

Relevant helpers:

- `RuntimeWorld::SyncTransformToScene(...)`
- `RuntimeWorld::SyncAllTransformsToScene()`

This bridge should remain explicit. Runtime code should not silently mutate editor scene state without going through a clear sync boundary.

Future rendering work can reduce this dependency by allowing Play mode rendering to consume runtime component data directly.

---

## Sinivault runtime usage

The current Sinivault gameplay loop uses `RuntimeWorld` for:

- player entity acquisition
- main camera entity acquisition
- player/camera runtime transform updates
- vault object discovery
- vault node state
- vault core state
- vault ring state
- vault exit state

The vault systems are split into focused helper modules:

- `VaultRuntime`
- `VaultDiscovery`
- `VaultNodeSystem`
- `VaultObjectSystem`
- `VaultMissionSystem`
- `VaultPresentation`
- `VaultCampaign`

`Game.cpp` still orchestrates the main update flow, but runtime/gameplay support logic is no longer all embedded directly in one file.

---

## Diagnostics

The Diagnostics panel includes a `RuntimeWorld` section.

It shows:

- runtime entity/component counts
- main camera runtime entity
- player runtime entity
- selected scene instance to runtime entity mapping
- selected runtime transform
- selected runtime rendering components
- selected runtime gameplay components
- selected vault component state
- vault scene validation warnings
- stale runtime warning while editing

Diagnostics are part of the runtime architecture. They are not optional polish.

Use them to verify play-mode clone correctness, runtime component classification, and runtime/editor sync behavior.

---

## Current limitations

`RuntimeWorld` is a foundation, not a finished runtime engine.

Current limitations:

- renderer still consumes `Scene` data
- runtime-to-scene sync is required for visible Play mode transforms
- gameplay still has some orchestration in `Game.cpp`
- scene serialization remains editor-scene oriented
- component storage is simple map-based storage, not a data-oriented ECS

These are acceptable for the current milestone.

---

## Future direction

Likely future improvements:

- move Play mode rendering toward runtime-owned component data
- continue extracting gameplay orchestration from `Game.cpp` when it improves clarity
- add more explicit runtime validation helpers
- strengthen runtime scene transition tests/checklists
- add runtime component authoring support only after the current bridge remains stable

The guiding rule is:

> Keep `RuntimeWorld` simple, explicit, and debuggable until the engine has enough runtime pressure to justify more complex systems.
