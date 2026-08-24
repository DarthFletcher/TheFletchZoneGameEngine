# Roadmap

This roadmap reflects the engine as it exists today: a DirectX 12 engine with a working editor shell, offscreen scene viewport, scene authoring workflow, prefab support, and a strong explicit-ownership rendering model.

It is intentionally short and practical.

---

## Current Position

The project is no longer just an early rendering foundation.

The current codebase already includes:

- deterministic DX12 frame ownership
- offscreen scene viewport rendering
- editor dockspace and panel workflow
- hierarchy, inspector, assets, diagnostics, and log panels
- selection, hover, multi-selection, duplication, deletion, and parenting
- gizmo-driven transform editing
- prefab save / instantiate / apply / revert flows
- material and texture integration
- scene save/load support
- a `RuntimeWorld` play-mode foundation with runtime entities/components, diagnostics, and scene sync compatibility
- vault gameplay split into runtime-focused helper modules for discovery, node/object state, mission state, presentation, and campaign routing
- hierarchy-aware object activation that excludes inactive objects from rendering and play-mode runtime systems

That means the next work should focus less on proving the renderer exists and more on making the engine easier to scale, maintain, and use.

---

## Practical Checklist

### Today

- `Assets` panel folder support
  - create folder
  - rename folder
  - basic folder navigation
- `Empty Object`
  - transform-only scene object
  - selectable in hierarchy
  - usable as a parent/group root
- `Scene` foundation
  - `New Scene`
  - clear current scene safely
  - define scene file storage under `Assets/Scenes/`

### This Week

- scene persistence
  - `Save Scene`
  - `Load Scene`
  - `Save As`
  - simple scene file format
- asset browser improvements
  - drag/drop assets into folders
  - move folders/assets on disk safely
- hierarchy polish
  - empty objects as grouping roots
  - cleaner rename flow
  - parent/child workflow cleanup

### Later

- `Play / Pause / Stop` behavior
- `Game View`
- runtime scene copy / isolation
- scene camera vs game camera separation
- settings panel
- project open/select flow
- build/export stub

### Best Immediate Order

1. `Create Folder`
2. `Rename Folder`
3. `Empty Object`
4. `New Scene`
5. `Save/Load Scene`
6. `Drag/Drop asset moves`

---

## Near-Term Priorities

### 1. Scene System Refactor

The highest-value structural improvement is to split `Src/Scene.cpp` into smaller implementation units.

Suggested separation:

- scene rendering
- selection and picking
- transform hierarchy
- prefab workflow
- serialization and file IO

Goal:

- reduce coupling
- improve readability
- make future feature work safer

### 2. Serialization Hardening

The current scene and prefab persistence paths work, but they still rely on hand-parsed text.

Focus:

- make parsing more robust
- add clearer schema/version handling
- reduce brittleness around formatting and escaping

### 3. Dirty-Driven Scene Updates

The engine currently favors correctness and clarity over aggressive optimization.

Next step:

- rebuild render data only when authoring state actually changes
- separate transform, visibility, selection, and material dirty paths where practical

### 4. Editor Workflow Polish

The editor already does real work. The next value is consistency.

Focus:

- improve undo/redo coverage
- tighten panel consistency
- improve empty/error states
- surface shortcuts and workflow guidance more clearly

---

## Mid-Term Priorities

### 5. Runtime / Play-Mode Separation

The first major `RuntimeWorld` milestone is now in place. The engine has a runtime entity/component clone of the editor scene during Play mode, with gameplay systems beginning to use runtime-owned state.

Remaining goals:

- continue reducing remaining `Game.cpp` orchestration into focused runtime systems only where it improves clarity
- move more renderer-facing data consumption toward runtime-owned state over time
- preserve editor state while running game logic
- keep the runtime layer simple, debuggable, and compatible with the existing explicit frame contract

### 6. Better Asset Pipeline

The engine already has materials, textures, and prefabs, but asset workflow can become much stronger.

Focus:

- stronger asset identity
- import workflow improvements
- better asset metadata
- future hot-reload friendliness

### 7. Rendering Quality Upgrades

Once the current architecture remains stable, rendering quality can move forward.

Likely next upgrades:

- shadows
- improved lighting polish
- post-processing groundwork
- richer debug visualization

---

## Longer-Term Direction

The likely long-term direction for the engine is:

- cleaner scene/runtime separation
- stronger asset pipeline
- more scalable scene data model
- richer rendering features
- continued explicit ownership and diagnostic discipline

Any future upgrades should preserve the engine's strongest existing qualities:

- deterministic frame flow
- explicit DX12 ownership
- debuggable editor integration
- clear subsystem responsibilities

---

## Guiding Rule

When choosing between new features and architectural cleanup, prefer the work that makes the next five features easier to build safely.
