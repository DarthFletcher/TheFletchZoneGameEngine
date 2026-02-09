# TheFletchZoneGameEngine

A custom DirectX 12 game engine written in C++, focused on **explicit ownership**, **deterministic frame flow**, and **long-term engine correctness**.

This project is intentionally built *without relying on engine magic* — every GPU resource, render pass, and lifecycle transition is owned, documented, and enforced by the engine itself.

---

## Core Features

- **DirectX 12 rendering pipeline**
- **Deterministic engine-owned frame lifecycle**  
  `BeginFrame → Render → EndFrame → Present` (single-present enforced)
- **Custom Dear ImGui integration (Win32 + DX12)**
  - Engine-owned SRV heap
  - Deferred font upload
  - Lazy PSO / root signature creation
  - No ImGui-owned GPU lifecycle
- **Offscreen Scene Render Target**
  - Explicit state transitions (RT ↔ SRV)
  - Resize-safe, fence-aware lifetime management
  - Stable SRV descriptor reuse (no heap churn)
- **Camera ownership system (Phase 3C)**
  - Engine-owned camera update once per frame
  - Scene consumes camera via `SceneRenderContext`
  - No global “current camera” access
- **Scene rendering path**
  - Explicit PSO/root signature ownership
  - Solid pass + grid overlay pass
  - No hidden state bleed from ImGui
- **Robust resize authority**
  - Deferred resize requests (swapchain + scene RT)
  - GPU-idle enforcement before `ResizeBuffers`
  - Client-rect–authoritative sizing
- **Device lost & diagnostics**
  - DRED-enabled device recovery
  - Frame health validation
  - DX12 + DXGI diagnostic logging
- **Custom logging system**
  - Throttled, categorized logs
  - Invariant and contract violation detection

---

## Architecture Principles

- **The engine owns the frame** — not ImGui, not the scene
- **No GPU resource is created, resized, or destroyed mid-frame**
- **Render passes are explicit and documented**
- **Scene rendering is a consumer, not an owner**
- **Resize is a request → apply model, never immediate**
- **Correctness over convenience**

Detailed design notes are available in:
- `ENGINE_CONTRACT.md`
- `docs/Phase3F_RenderGraphNotes.md`

---

## Project Structure

├─ Include/ // Public engine interfaces

├─ Src/ // Engine implementations

├─ docs/ // Design + phase documentation

├─ tools/ // Build helpers

├─ shaders/ // HLSL shaders


---

## Build Requirements

- Windows 10 / 11
- Visual Studio 2022
- DirectX 12 compatible GPU
- C++20 (or later)

---

## Current Status

🚧 **Active Development**

✅ **Phase 3 complete** — Core engine correctness & ownership stabilized  
- Frame lifecycle enforced  
- Resize authority hardened  
- Scene + camera ownership implemented  
- Render pass contracts documented and enforced  

🔒 Phase 3 is **closed and locked**.

---

## Next Phase

**Phase 4A** will focus on:
- Real mesh rendering (cube / indexed geometry)
- Proper vertex/index buffer ownership
- Transform system groundwork
- Preparing for entity-based rendering

---

## License

MIT License © 2026 TheFletchZone
