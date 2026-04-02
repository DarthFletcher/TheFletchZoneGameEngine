# Editor Workflow Status

This document describes the editor-facing workflow that currently exists in the engine.

It is meant as a practical reference for how scene authoring behaves today.

---

## 1. Editor Shell

The editor uses a Dear ImGui dockspace shell.

Current shell behavior includes:

- dockspace root window
- menu bar
- command strip region
- dockable editor panels
- optional diagnostics and overlay windows
- layout save/reset support

The scene viewport is presented inside the editor as an ImGui texture sourced from the engine's offscreen scene render target.

---

## 2. Main Panels

The current panel set includes:

- `Scene`
- `Hierarchy`
- `Inspector`
- `Assets`
- `Instancing`
- `Material Preview`
- `Black Flame`
- `Prompt Helper`
- `Prefab Workflow`
- `Debug Overlay`
- `Diagnostics`
- `Log Viewer`

Together these panels already form a real editor workflow rather than a single debug UI.

---

## 3. Scene Viewport Behavior

The scene viewport is an offscreen-rendered view of the current scene.

Current behavior includes:

- scene render target displayed inside the editor UI
- viewport hover/focus tracking
- camera input gating based on viewport interaction state
- picking support using the most recent rendered camera data
- hover and selection feedback

The viewport is therefore both a render output and an interaction surface.

---

## 4. Scene Authoring

The current scene authoring workflow supports primitive-based content creation.

Supported primitive types include:

- cube
- sphere
- plane
- cylinder

Authoring operations available in the current code include:

- create primitive
- select instance
- multi-select instances
- duplicate selected instance
- delete selected instance
- change parent / unparent
- preserve world transform while re-parenting
- inspect and edit transform data
- edit visibility and material assignment

---

## 5. Selection Model

The scene currently maintains:

- active selected instance ID
- multi-selection list
- hovered instance ID
- selection center and selection bounds queries

Selection supports several common editor flows:

- single selection
- additive selection
- toggle selection
- clearing selection when a miss occurs in non-additive mode

Selection state also feeds visual feedback in scene rendering.

---

## 6. Gizmo and Transform Editing

The editor has a gizmo-oriented transform workflow.

Current editor state already tracks:

- hot axis
- active axis
- dragging state
- drag start hit and position
- pivot mode
- camera navigation mode

This provides the basis for in-viewport transform editing rather than relying only on numeric inspector edits.

---

## 7. Hierarchy Workflow

Hierarchy editing is already part of the scene model.

Current behavior includes:

- parent-child relationships via `parentInstanceId`
- rejection of invalid parenting such as self-parenting or cyclic ancestry
- world-transform preservation when changing parent
- child unparenting when selected parents are deleted

This is a meaningful authoring feature and should be treated as a core editor system.

---

## 8. Prefab Workflow

Prefab support is already integrated into the editor workflow.

Current supported flows:

- save selected instance as prefab asset
- instantiate prefab asset into the scene
- apply selected instance back to its prefab source asset
- revert selected instance from prefab source asset
- apply or revert individual prefab-backed properties
- query prefab override state

Current tracked prefab-backed property categories include:

- name
- rotation
- scale
- visible
- material

---

## 9. Scene Save/Load Workflow

The scene system already supports persistence.

Current flows include:

- serialize the current scene to text
- load a scene from text
- save a scene file to disk
- load a scene file from disk

The current format is intentionally simple and easy to inspect, but it should be considered an area for future hardening.

---

## 10. Undo/Redo Foundation

The editor state already stores scene history snapshots.

Current history entries capture:

- serialized scene snapshot
- active selected instance ID
- selected instance ID list

This gives the editor an undo/redo foundation for scene authoring changes, even if coverage and granularity still have room to expand.

---

## 11. Diagnostics-Centered Workflow

The editor includes dedicated visibility into engine state.

Current tooling includes:

- diagnostics panel
- debug overlay
- log viewer
- frame diagnostics window
- engine logging with categories and throttling

This is important because the engine is still evolving and architectural visibility is part of daily workflow.

---

## 12. Known Workflow Gaps

The current editor workflow is useful, but still not finished.

The main gaps are:

- scene/prefab parsing is still hand-rolled
- some systems remain coupled through large implementation files
- full runtime/play-mode separation is not yet the dominant architecture
- undo/redo coverage can expand further
- asset import and GUID-style asset identity are still future-facing improvements

Even with those gaps, the current engine already supports meaningful editor-side authoring and iteration.