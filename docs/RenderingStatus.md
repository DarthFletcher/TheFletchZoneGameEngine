# Rendering Status

This document describes the current rendering path and render-related architecture in the engine.

It focuses on what exists today, not an idealized future renderer.

---

## 1. Rendering Philosophy

The renderer is built around explicit DX12 ownership.

Core principles currently visible in the codebase include:

- engine-owned frame lifecycle
- explicit pass sequencing
- explicit resource state transitions
- no subsystem-owned `Present()` behavior
- defensive re-binding of sticky GPU state
- correctness-first diagnostics

This philosophy is one of the project's strongest foundations and should be preserved during future rendering upgrades.

---

## 2. Current Scene Rendering Path

The engine renders the scene into an offscreen scene render target that is later shown in the editor UI.

At a high level:

1. `Graphics::RenderSceneToTarget()` prepares the scene render target
2. the scene RT is transitioned into render-target usage
3. `Scene::Render(const SceneRenderContext&)` records scene draw commands
4. the scene RT is transitioned back to shader-resource usage
5. Dear ImGui samples the scene texture in the editor viewport

This keeps scene rendering explicit and compatible with the editor shell.

---

## 3. Current Passes

The current scene draw path includes at least these passes:

- **opaque geometry pass**
  - primitive meshes
  - indexed drawing
  - material binding per visible scene instance
  - depth-enabled rendering
- **grid overlay pass**
  - line rendering
  - editor-facing ground/grid visualization

The renderer is still relatively compact, but it is already beyond a trivial triangle test stage.

---

## 4. Camera Consumption

The scene pass consumes camera state through `SceneRenderContext`.

That means:

- camera data is prepared outside the scene pass
- `Scene::Render(...)` treats camera input as read-only
- picking and viewport interaction can reuse the last rendered camera data

This is important because it avoids hidden global-camera ownership inside the renderer.

---

## 5. Primitive and Mesh Rendering

The current scene path renders engine-owned primitive meshes.

Current supported primitives include:

- cube
- sphere
- plane
- cylinder

Each scene instance selects its mesh by primitive type, and drawing uses the engine mesh containers exposed through `Graphics`.

---

## 6. Material Binding

Materials are already part of the scene draw path.

Current behavior includes:

- per-instance material index lookup
- material fallback handling
- material constant buffer binding
- texture SRV binding for material channels
- shared engine SRV heap usage

This gives the engine a practical material pipeline even before more advanced rendering features arrive.

---

## 7. Instance Data Path

The scene currently builds CPU-side instance data from authoring state.

Current flow includes:

- rebuild render instances from `SceneInstance` authoring data
- compute world matrices
- compute per-instance color state
- build bounds for culling and interaction
- generate visible instance index lists
- build contiguous visible-instance scratch data
- upload visible instance data into a GPU-readable instance buffer

The current path favors clarity and authoring correctness over aggressive optimization.

---

## 8. Culling and Visibility

The current scene path already performs visibility filtering.

Current behavior includes:

- authoring-mode option to disable aggressive culling in the editor scene view
- frustum-based visible instance filtering when scene-view culling is enabled
- guaranteed visibility for selected objects so they do not disappear while editing
- hidden objects excluded from visible draw submission

This is a sensible editor-first compromise between correctness and stability.

---

## 9. Picking and Hover Support

Viewport interaction is tightly related to rendering.

Current support includes:

- conversion from viewport mouse coordinates to world-space rays
- ray vs instance bounds tests
- closest-hit selection logic
- hover tracking
- use of the most recently rendered camera state

This makes the rendering path part of a broader authoring workflow rather than an isolated draw-only system.

---

## 10. State Management

The renderer explicitly restores the state it needs before drawing.

The scene path currently rebinds or defines:

- descriptor heaps
- viewport and scissor
- root signature
- pipeline state
- primitive topology
- scene constant buffer bindings
- mesh VB/IB bindings
- per-material resource bindings

This is necessary because DX12 state is sticky and Dear ImGui can leave incompatible state behind.

---

## 11. Current Strengths

The current rendering path is strongest in these areas:

- explicit ownership and pass boundaries
- editor-compatible offscreen rendering
- defensive state setup
- practical material/texture integration
- integrated picking and culling support
- strong diagnostics mindset

These are excellent foundations for later rendering upgrades.

---

## 12. Current Limitations

The main current rendering limitations are:

- scene rendering logic and non-render scene logic still live together in `Src/Scene.cpp`
- visible-instance data is rebuilt/uploaded in a straightforward but not yet heavily optimized way
- serialization and authoring systems are tightly coupled to current scene structures
- advanced features such as shadows, post-processing, and a more explicit render graph are future work

These limitations are normal for the engine's current stage.

---

## 13. Recommended Next Rendering-Focused Steps

The most natural next rendering upgrades are:

- split `Scene` render code from scene authoring and serialization code
- make more of the scene update path dirty-driven
- add richer lighting and shadow support
- expand diagnostics with more GPU/pass-level timing visibility
- continue moving toward a more explicit pass graph only after the current baseline remains stable
