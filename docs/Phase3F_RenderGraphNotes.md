# Phase 3F: Render Graph & Pass Ordering — Rationale & Constraints

**Status:** Documentation-only (no runtime changes)  
**Phase:** 3F  
**Related:** Phase 3C (camera), Phase 3D (resize), Phase 3E (diagnostics)

---

## Purpose

This document explains **why explicit pass ordering exists**, **why ImGui is a consumer** (not an owner), and **why `Scene::Render` must not create global GPU state**. Phase 3F is diagnostics-only; no code changes to rendering paths.

---

## 1. Why Explicit Pass Ordering Exists

### 1.1 Single Command List, Single Queue

The engine uses a **single graphics command list** per frame and a **single command queue**. All GPU work (scene draw, UI draw, transitions) must serialize into this list in a deterministic order.

**Consequences:**
- **Pass order matters**: If ImGui draws *before* scene transitions to `PIXEL_SHADER_RESOURCE`, the editor will see garbage or crash.
- **State is sticky**: PSO, root signature, descriptor heaps, viewports, scissors, and render targets persist between draws unless explicitly changed.
- **No implicit resets**: D3D12 does not auto-clear state between `SetPipelineState()` calls. Bleed from one pass to another is a common source of rendering bugs.

### 1.2 Frame Lifecycle Contract

```
BeginFrame():
  1. Reset command allocator + command list
  2. Bind SRV heap
  3. ImGui::NewFrame()
  4. UI draws (dockspace, panels, scene viewport) ? records ImGui draw data

EndFrame():
  5. Close command list
  6. ExecuteCommandLists()
  7. Signal fence

Present():
  8. IDXGISwapChain::Present()
```

**Scene rendering happens during step 4**, when the UI code calls `Graphics::RenderSceneToTarget()`. This function:
- Transitions scene RT to `RENDER_TARGET`
- Calls `Scene::Render(...)` (opaque geometry)
- Transitions scene RT to `PIXEL_SHADER_RESOURCE`
- Returns control to UI, which uses `sceneImGuiTextureID` to display the result

**ImGui rendering happens after UI build completes** (still in step 4), when `ImGui::Render()` + `ImGui_ImplDX12_RenderDrawData()` consume the recorded draw data.

---

## 2. Why ImGui Is a Consumer, Not an Owner

### 2.1 ImGui Does Not Own the Command List

- **Owner:** `Graphics` class (via `BeginFrame`/`EndFrame`)
- **Consumer:** ImGui backend (`ImGui_ImplDX12_RenderDrawData`)

ImGui **reads** the command list (via `commandList->DrawIndexedInstanced()`, etc.) but does **not**:
- Reset allocators
- Signal fences
- Transition backbuffer states
- Call `Present()`

**Why:** ImGui is middleware. It expects the host engine to manage frame lifecycle, synchronization, and presentation. Allowing ImGui to own the command list would create conflicting fence stamps and allocator resets.

### 2.2 ImGui State Bleed

ImGui sets:
- Root signature (`ImGui_ImplDX12` internal)
- PSO (blend enabled, no depth, triangle list)
- Descriptor heaps (SRV for fonts + textures)
- Viewport / scissor (per ImGui window)

After `ImGui_ImplDX12_RenderDrawData()` finishes, **all of these remain bound**. If another draw happens without re-binding, it will use ImGui's PSO, which is almost never what you want for 3D geometry.

**Mitigation:** `Scene::Render()` explicitly re-binds its own root signature, PSO, heaps, viewport, and scissor **every frame**, even if we "know" ImGui hasn't drawn yet. This guards against future refactors.

---

## 3. Why `Scene::Render` Must Not Create Global GPU State

### 3.1 Scene Is a Draw Function, Not a Subsystem

`Scene::Render(ctx)` is:
- **Stateless** (modulo internal resource cache)
- **Non-owning** (does not manage the command list lifecycle)
- **Deterministic** (same inputs ? same GPU commands)

It **must not**:
- Open or close the command list
- Transition the **main window backbuffer** (that's `Graphics::RenderSceneToTarget`'s job)
- Signal fences or flush the GPU
- Bind descriptor heaps that ImGui or other passes expect

**Why:** If `Scene::Render` transitions the backbuffer, it violates the frame lifecycle contract. The engine would crash when ImGui tries to draw, because the backbuffer is in the wrong state.

### 3.2 Resource State Responsibility

| Resource                  | Owner                          | Notes                                      |
|---------------------------|---------------------------------|--------------------------------------------|
| Main window backbuffer    | `Graphics::RenderSceneToTarget` | Transitioned by caller, not `Scene::Render` |
| Scene render target       | `Graphics::RenderSceneToTarget` | `RENDER_TARGET` on entry, `PIXEL_SHADER_RESOURCE` on exit |
| Scene depth buffer        | `Graphics::RenderSceneToTarget` | Managed by owning function                  |
| ImGui font texture        | ImGui backend                   | Persistent `PIXEL_SHADER_RESOURCE`          |

**Rule:** `Scene::Render` may only transition resources it **exclusively owns** (e.g., intermediate G-buffer textures in a future deferred renderer). The scene RT is **borrowed**, not owned.

### 3.3 Descriptor Heap Contract

`Scene::Render` binds `g_SRVHeap` (the global engine SRV heap) if it needs textures. It **must not** create a private heap and bind it, because:
1. ImGui expects `g_SRVHeap` to remain bound (for font texture lookups)
2. Switching heaps mid-frame is legal but adds complexity
3. The picking system also expects `g_SRVHeap`

**Current behavior (Phase 3F):**  
Scene binds `g_SRVHeap`, draws, then returns. ImGui re-binds it implicitly when `ImGui_ImplDX12_RenderDrawData()` starts. This works because both use the same heap.

---

## 4. Pass Boundaries & Ownership Assumptions

### 4.1 Conceptual Pass Structure

```
Pass 1: Scene Geometry ? Scene RT
  - Owner: Graphics::RenderSceneToTarget()
  - Consumer: Scene::Render(ctx)
  - Transitions: Scene RT (COMMON ? RENDER_TARGET ? PIXEL_SHADER_RESOURCE)

Pass 2: ImGui UI ? Main Window Backbuffer
  - Owner: Graphics::RenderSceneToTarget() (backbuffer transitions)
  - Consumer: ImGui_ImplDX12_RenderDrawData()
  - Transitions: Backbuffer (PRESENT ? RENDER_TARGET ? PRESENT)
```

**Note:** Pass 1 is **nested inside** Pass 2's transition block. The backbuffer is in `RENDER_TARGET` state *before* we draw the scene, but the scene draws to a **different RT** (the scene RT). This is legal because D3D12 allows multiple RTs to coexist in different states, as long as each is in the correct state when accessed.

### 4.2 Transition Correctness

**Golden rule:** A resource must be in the correct state **at the moment it's used**, not before or after.

Example (from `Graphics::RenderSceneToTarget`):
```cpp
// Transition scene RT to RENDER_TARGET (we're about to draw to it)
Transition(sceneRenderTarget, RENDER_TARGET);

// Clear + bind
commandList->ClearRenderTargetView(sceneRtvHandle, clearColor, 0, nullptr);
commandList->OMSetRenderTargets(1, &sceneRtvHandle, FALSE, nullptr);

// Draw (Scene::Render consumes the command list, adds draws)
Scene::Render(ctx);

// Transition scene RT to PIXEL_SHADER_RESOURCE (ImGui will sample it)
Transition(sceneRenderTarget, PIXEL_SHADER_RESOURCE);
```

If we moved the `PIXEL_SHADER_RESOURCE` transition **before** `Scene::Render`, the draws would write to a texture in the wrong state ? GPU page fault ? device removal.

---

## 5. Future: Render Graph System

Phase 3F documents the **current implicit graph**. A future phase may introduce an explicit render graph system that:
- Declares passes + dependencies
- Auto-inserts transitions
- Validates state correctness at build time

**Why not now:** We need stable camera + resize + diagnostics first (Phases 3C–3E). Render graphs add complexity; defer until the baseline is bulletproof.

---

## 6. Summary

| Concept                          | Rationale                                                                 |
|----------------------------------|---------------------------------------------------------------------------|
| **Explicit pass ordering**       | Single command list, single queue ? deterministic serialization           |
| **ImGui is a consumer**          | ImGui does not own frame lifecycle (no fence signals, no Present calls)   |
| **Scene does not create global state** | Scene is stateless; transitions are owned by caller (`RenderSceneToTarget`) |
| **State bleed is real**          | PSO, heaps, viewports persist ? explicit re-binding required              |
| **Transition timing is critical**| Resource must be in correct state **when accessed**, not before/after     |

---

## Change History

- **2025-01-XX (Phase 3F):** Initial documentation (no code changes)

