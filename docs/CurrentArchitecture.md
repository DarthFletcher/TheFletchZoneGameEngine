# Current Architecture Overview

This document summarizes how the engine is structured today, based on the current editor-driven DirectX 12 codebase.

It is not a future design pitch. It describes the systems that currently exist and how they fit together.

---

## 1. High-Level Shape

The engine is currently an **editor-first DX12 engine** with an offscreen scene viewport, scene authoring tools, and a deterministic frame model.

At a high level:

- `Engine` owns application flow and frame pacing
- `Graphics` owns DX12 device state, swapchain, command submission, scene RTs, and Dear ImGui integration
- `UI` builds the dockspace shell, menus, command strip, overlays, and editor windows
- `EditorPanels` provides the editor-facing panels and authoring workflows
- `Scene` owns scene instance data, selection, parenting, prefab persistence, picking support, and scene draw submission
- `RuntimeWorld` owns the play-mode runtime entity/component clone used by gameplay systems
- `MaterialManager` and `TextureManager` own material and texture-facing resource workflows used by the scene/editor

---

## 2. Frame Model

The frame is engine-owned and deterministic.

Conceptually:

1. `Graphics::BeginFrame()` prepares recording state
2. UI builds the editor shell and panels
3. the scene viewport requests offscreen scene rendering
4. `Scene::Render(...)` records scene draw commands into the active frame command list
5. Dear ImGui records the final editor UI draw data
6. `Graphics::EndFrame()` closes and executes work
7. `Graphics::Present()` is called once by the engine

This is one of the engine's strongest architectural foundations and should remain stable.

---

## 3. Graphics Responsibilities

`Graphics` currently owns or coordinates:

- DX12 device creation and recovery
- command queue, command list, and allocator lifecycle
- per-frame synchronization and fences
- swapchain/backbuffer handling
- deferred resize application
- Dear ImGui backend integration
- scene viewport render target creation and resize
- scene viewport texture exposure back to the editor UI
- frame constant buffer allocation helpers
- shared SRV heap usage for editor/scene material workflows

The scene pass is therefore nested inside a broader engine-owned render flow rather than owning the frame itself.

---

## 4. Scene System Responsibilities

`Scene` currently does much more than just drawing.

It owns:

- the authoritative list of `SceneInstance` objects
- stable instance IDs and parent IDs
- local transform data
- primitive selection per instance
- material index assignment
- hierarchy-aware active state
- visibility state
- selection, hover, and multi-selection tracking
- parent/child transform hierarchy behavior
- prefab save/load/apply/revert logic
- scene save/load serialization
- CPU-side instance data generation for rendering
- visibility filtering and visible-instance scratch building
- picking support using viewport rays and instance bounds

This makes `Scene` a central authoring system today, but it also means `Src/Scene.cpp` is carrying multiple responsibilities that will likely be split later.

---

## 5. Scene Data Model

A `SceneInstance` currently stores:

- `instanceId`
- `parentInstanceId`
- `name`
- `prefabSourcePath`
- `position`
- `rotation`
- `scale`
- `activeSelf`
- `visible`
- `materialIndex`
- `primitive`

This is effectively the current authoring entity model.

The engine does not yet expose a broader runtime ECS-style component model here. The current model is intentionally simple and editor-friendly.

---

## 6. RuntimeWorld and Play Mode

`RuntimeWorld` is now the engine's runtime-side gameplay world foundation.

Current behavior includes:

- cloning editor `SceneInstance` data into runtime entities when entering Play mode
- excluding inactive objects and descendants of inactive parents from the runtime clone
- stable runtime entity IDs and source scene instance mapping
- runtime component storage for transforms, cameras, mesh renderers, player controller data, trigger volumes, and vault gameplay objects
- runtime diagnostics in the editor Diagnostics panel
- runtime refresh on vault reset and vault scene transition
- runtime-to-scene transform sync helpers used as a compatibility bridge while rendering still consumes `Scene` data

The current vault gameplay path now uses runtime components for discovery and gameplay state bridging:

- player and main camera acquisition prefer runtime entity IDs
- player and camera transforms update runtime components first, then sync back to `SceneInstance` for rendering
- vault node, core, exit, and ring state is mirrored through runtime components
- vault-specific logic has been split into focused helper modules:
  - `VaultRuntime`
  - `VaultDiscovery`
  - `VaultNodeSystem`
  - `VaultObjectSystem`
  - `VaultMissionSystem`
  - `VaultPresentation`
  - `VaultCampaign`

This is not a full data-oriented ECS yet. It is intentionally a simple, debuggable runtime layer that separates play-mode ownership from editor authoring while preserving compatibility with the current renderer.

---

## 7. Rendering Path Today

The current scene rendering path is centered around primitive meshes and per-instance authoring data.

Current behavior includes:

- mesh selection based on `ScenePrimitive`
- per-instance world matrix generation
- per-instance color assignment for normal, hovered, and selected states
- visible instance filtering
- upload of visible instance data to a GPU-readable buffer
- per-visible-object material binding
- opaque indexed mesh drawing
- a separate grid overlay pass

The scene render code also defensively rebinds root signature, heap, viewport, scissor, and other sticky state to avoid ImGui contamination.

---

## 8. Editor Shell and Panels

The editor uses Dear ImGui with a dockspace-centered shell.

Current panel set includes:

- Scene
- Hierarchy
- Inspector
- Assets
- Instancing
- Material Preview
- Black Flame
- Prompt Helper
- Prefab Workflow
- Debug Overlay
- Diagnostics
- Log Viewer

The main editor shell also includes menu-driven layout reset/save behavior, font sizing options, and other editor utilities.

---

## 9. Editor Interaction Systems

The editor currently supports:

- scene viewport hover/focus tracking
- camera input gating based on viewport interaction
- scene picking from viewport rays
- hover highlighting
- selection and multi-selection
- selection center / bounds queries
- gizmo-driven transform workflows
- undo/redo snapshot storage for scene authoring changes
- parent/child hierarchy editing with world-transform preservation

This means the engine is already beyond a rendering-only milestone and is firmly in editor workflow territory.

---

## 10. Prefab and Asset-Oriented Authoring

Prefab support already exists in practical form.

Current prefab workflow includes:

- saving a selected scene instance as a prefab asset
- instantiating prefab assets into the scene
- applying a selected instance back to its prefab source
- reverting a selected instance from its prefab source
- property-level apply/revert for selected prefab-backed fields
- prefab override state checks

Textures and materials are also first-class editor-facing systems through `TextureManager` and `MaterialManager`.

---

## 11. Diagnostics and Logging

Diagnostics are not an afterthought in this codebase.

Current diagnostic emphasis includes:

- DRED/device-health support
- frame-health checks
- resize diagnostics
- scene and culling logs
- instancing and CBV debug logs
- runtime world entity/component diagnostics
- editor-visible diagnostics/log panels

This is consistent with the engine's broader correctness-first philosophy.

---

## 12. Current Architectural Pressure Points

The largest current pressure points are structural, not conceptual.

The main ones are:

- `Src/Scene.cpp` has become too large and multi-purpose
- `Src/Game.cpp` has been reduced through runtime helper modules, but still orchestrates the current vault update loop
- scene/prefab serialization is still hand-parsed text rather than a more robust structured serializer
- some older phase names in logs/docs no longer match the current feature set
- parts of the render/update path still rebuild data every frame that could later become dirty-driven

These are normal pressure points for an engine that has grown from renderer foundation work into editor tooling.

---

## 13. Likely Next Structural Steps

The most natural architectural follow-ups are:

- split `Scene` responsibilities into smaller implementation units
- continue reducing `Game.cpp` into runtime systems where useful
- harden serialization/versioning
- continue moving renderer-facing data consumption toward runtime-owned state over time
- expand asset identity and import workflows
- continue improving rendering quality without violating the existing frame contract
