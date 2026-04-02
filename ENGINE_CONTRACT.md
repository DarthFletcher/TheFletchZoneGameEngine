# TheFletchZone Engine - DX12 Frame Contract

This document defines the non-negotiable rules for the engine's DirectX 12 frame lifecycle, synchronization model, resize handling, scene viewport rendering, and Dear ImGui integration.

The goal is to keep the engine deterministic, prevent regressions such as double-present or allocator misuse, and preserve clear ownership boundaries as the editor and scene systems grow.

---

## 1) Frame Ownership

- The `Engine` owns frame pacing.
- Exactly **one** `Graphics::Present()` call is allowed **per frame**.
- `Present()` is called **only** by `Engine`.
- `Graphics` must never call `Present()` implicitly from helper functions.

Canonical order:

1. `Graphics::BeginFrame()`
2. `Graphics::Render()`
3. `Graphics::EndFrame()`
4. `Graphics::Present()`

`Scene::Render(...)` and Dear ImGui are both **consumers** inside the frame, not lifecycle owners.

---

## 2) Command Lists and Allocators

- Frame rendering uses per-backbuffer allocator ownership.
- The main frame command list is reset from the allocator for the current backbuffer.
- Upload work uses dedicated upload-side command recording resources.
- Upload paths and frame rendering paths must remain isolated.

Rules:

- Never use frame allocators for upload work.
- Never use upload allocators or upload command lists as the main frame list.
- Never reset an allocator without fence validation proving GPU completion for the work recorded with it.

---

## 3) Fences and Synchronization

- Fence values tied to the current backbuffer are the source of truth for allocator reuse.
- `ExecuteCommandLists()` must be followed by a fence signal for that submitted work.
- Future frames must wait on the required fence value before reusing allocator-owned GPU recording resources.

Contractually, the engine should preserve:

- one clear fence authority
- predictable allocator reuse
- no silent fence desynchronization between subsystems

---

## 4) Resize Authority

- Window messages may request resize, but must not immediately mutate the swapchain.
- Resize is handled as a **request ? apply** model.
- Swapchain resize happens only through the engine's controlled frame path.
- Scene render target resize follows the same deferred model.

Rules:

- `WM_SIZE` and similar messages request resize only.
- `IDXGISwapChain::ResizeBuffers()` must not be called from arbitrary code paths.
- GPU work referencing old buffers must be complete before swapchain or scene RT resources are replaced.

---

## 5) Scene Viewport Ownership

The editor scene view is rendered into an offscreen scene render target, then sampled by the UI.

Ownership rules:

- `Graphics::RenderSceneToTarget()` owns the scene render target transitions.
- `Scene::Render(...)` consumes the command list and records scene draw work.
- Dear ImGui samples the finished scene texture after the scene RT has been transitioned back to shader-resource usage.

`Scene::Render(...)` must not:

- open or close the frame command list
- present the swapchain
- signal fences
- resize the scene render target
- transition the main window backbuffer

The scene pass is a draw pass, not a frame owner.

---

## 6) Scene Render Contract

`Scene::Render(const SceneRenderContext&)` is expected to be a deterministic consumer of already-prepared frame state.

On entry, the caller is responsible for:

- providing a valid device and open command list
- providing a valid camera via `SceneRenderContext`
- ensuring the target scene render target is already in `RENDER_TARGET` state

Within the pass, scene code is responsible for:

- binding its own root signature and PSOs
- rebinding descriptor heaps it depends on
- setting viewport and scissor state explicitly
- binding scene constant buffers, instance data, mesh buffers, and material resources explicitly

The scene pass must defensively rebind state even when current call order appears stable, because ImGui and future passes can leave sticky state behind.

---

## 7) Dear ImGui Contract

Dear ImGui remains middleware and a frame consumer.

Rules:

- Dear ImGui does not own frame lifetime.
- Dear ImGui must not introduce hidden presents.
- Dear ImGui font and texture uploads use dedicated upload paths.
- ImGui and scene rendering should continue sharing the engine SRV heap model unless a broader heap ownership design replaces it intentionally.

Important consequence:

- ImGui state can bleed into later draw work unless later passes explicitly restore their own PSO, root signature, viewport, scissor, and descriptor heap state.

---

## 8) Editor and Scene Interaction Contract

The current editor is tightly integrated with the scene authoring flow. That requires clear rules.

- The editor may inspect and mutate scene data.
- The scene remains the authoritative owner of scene instance data used for rendering, selection, prefab application, and serialization.
- Editor UI should call scene-facing APIs rather than mutating rendering buffers directly.
- Picking and hover should be driven from camera data captured during scene rendering, not a hidden global camera outside engine ownership.

Current editor-facing capabilities include:

- hierarchy and inspector flows
- scene selection and hover
- multi-selection
- duplication and deletion
- parent / child hierarchy edits
- gizmo-driven transform updates
- prefab save / apply / revert flows

---

## 9) Scene Data and Serialization Expectations

The current scene system stores authoring data such as:

- stable `instanceId`
- optional `parentInstanceId`
- local transform
- visibility
- material assignment
- primitive type
- optional prefab source path

Serialization and prefab persistence are part of engine behavior now, not side experiments.

Rules going forward:

- scene save/load must preserve stable instance relationships
- prefab application must preserve intentional runtime/editor invariants
- serialization changes should be versioned
- parsing behavior should trend toward more robust structured serialization over time

---

## 10) Safety Guarantees and Runtime Guards

The codebase should continue enforcing these invariants where practical:

- no double-present in one frame
- no allocator reset without fence validation
- no resource destruction while GPU work may still reference it
- no hidden backbuffer transitions from scene-only code
- no render-time creation of global GPU state from subsystems that are supposed to be pure consumers

Recommended guard patterns include:

- `presentedThisFrame` style validation
- command-list-open assertions
- render-phase resource creation assertions
- diagnostic logging when contract violations are detected

---

## 11) Healthy Frame Expectations

A healthy frame should continue to resemble this flow:

1. `BeginFrame` resets per-frame recording state
2. editor UI builds dockspace and panels
3. scene viewport requests scene rendering through `Graphics::RenderSceneToTarget()`
4. scene RT transitions to render target state
5. `Scene::Render(...)` records scene geometry and overlay draws
6. scene RT transitions back to shader-resource state
7. ImGui consumes the scene texture and records UI draw data
8. `EndFrame()` closes and executes command work, then signals fences
9. `Present()` is issued once by the engine

---

## 12) Scope of This Contract

This document is intentionally stricter than a generic rendering guide.

It exists to protect:

- deterministic DX12 frame ownership
- stable editor scene rendering
- future refactors of large files such as `Src/Scene.cpp` and `Src/Graphics.cpp`
- AI-assisted and manual edits that might otherwise violate implicit engine assumptions
