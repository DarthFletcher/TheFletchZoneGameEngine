# TheFletchZone Engine – DX12 Frame Contract

This document defines non-negotiable rules for the engine’s DirectX 12 frame lifecycle, synchronization, resizing model, and ImGui integration.

The goal is to keep the frame model deterministic, prevent regressions (double-present, allocator misuse, fence desync), and ensure future changes (including AI-assisted edits) follow the same contract.

## 1) Frame Ownership

- The `Engine` owns frame pacing.
- Exactly **one** `Graphics::Present()` call is allowed **per frame**.
- `Present()` is called **only** by `Engine`.
- `Graphics` must never call `Present()` internally (no implicit presents).

Canonical order:

1. `Graphics::BeginFrame()`
2. `Graphics::Render()` (records main draw work)
3. `Graphics::EndFrame()` (executes + fences; **no present**)
4. `Graphics::Present()` (owned by `Engine`)

## 2) Command Lists & Allocators

- Frame rendering uses **per-backbuffer** command allocators: `commandAllocators[backBufferIndex]`.
- The main frame command list is reset from the allocator for the current backbuffer.
- Upload work uses a **dedicated** upload allocator + upload command list:
  - `uploadAllocator`
  - `uploadCommandList`
- Upload command lists are isolated from frame rendering:
  - Never use frame allocators for uploads.
  - Never use upload allocator/command list for the main frame.

## 3) Fences & Synchronization

- Single fence source of truth.
- `fenceValues[backBufferIndex]` is the authoritative per-backbuffer completion value.
- No duplicate/secondary fence arrays.
- The contract is:
  - `ExecuteCommandLists()` ? `SignalFence()`
  - next frame waits on `fenceValues[currentBackBufferIndex]` before resetting that allocator.

## 4) Resizing

- Window messages may request resize, but do not resize the swapchain immediately.
- `WM_SIZE` ? `Graphics::RequestResize(width, height)`
- The actual swapchain resize occurs only in `Graphics::BeginFrame()` via `ApplyPendingResize()`.
- `IDXGISwapChain::ResizeBuffers()` must not be called anywhere else.

## 5) ImGui

- Phase 0: multi-viewport is disabled (single swapchain / single present path).
  - `io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;`
- Font/texture uploads must use the dedicated upload command list.
- ImGui rendering must not introduce hidden presents.

## 6) Safety Guarantees (Guards)

The code should enforce these invariants:

- No allocator reset without fence validation.
- No GPU resource destruction without ensuring GPU completion.
- No double-present.

Recommended runtime guard:

- Track `presentedThisFrame` and block/log/assert on a second `Present()` in the same frame.

## 7) Logging Expectations

A healthy frame repeatedly logs (in order):

- `BeginFrame` entry
- main viewport render recorded
- command list executed and fenced
- `Present` completed
