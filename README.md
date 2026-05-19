# TheFletchZoneGameEngine

A custom DirectX 12 engine written in C++20, focused on **explicit ownership**, **deterministic frame flow**, and a steadily growing **editor-first authoring workflow**.

This project intentionally avoids hidden engine magic. GPU resources, render passes, scene viewport flow, and editor/runtime responsibilities are expected to be explicit, documented, and engine-owned.

---

## Core Features

- **DirectX 12 rendering pipeline**
- **Deterministic engine-owned frame lifecycle**  
  `BeginFrame → Render → EndFrame → Present` with single-present ownership enforced by the engine
- **Custom Dear ImGui integration (Win32 + DX12)**
  - Engine-owned SRV heap
  - Deferred font upload
  - Lazy PSO / root signature creation
  - No ImGui-owned GPU lifecycle
- **Dockspace-based editor shell**
  - Menu bar + command strip workflow
  - Dockable editor panels
  - Layout save/reset support
  - Frame diagnostics tooling
- **Offscreen scene viewport render target**
  - Explicit RT ↔ SRV transitions
  - Resize-safe, fence-aware lifetime management
  - Stable descriptor reuse for editor presentation
  - Viewport hover/focus-aware input handling
- **Scene rendering path**
  - Engine-owned root signature and PSOs
  - Primitive mesh rendering
  - Material binding per scene instance
  - Grid overlay pass
  - Defensive state rebinding to prevent ImGui bleed
- **Scene editing workflow**
  - Hierarchy, inspector, assets, diagnostics, and log panels
  - Selection, hover, multi-selection, duplication, deletion
  - Parenting / re-parenting with world-transform preservation
  - Editor gizmo-driven transform editing
- **Scene picking and placement helpers**
  - Viewport mouse-to-world ray construction
  - Grid plane hit testing for editor placement
  - Scene-object selection and hover picking
- **Prefab workflow**
  - Save selected instance as prefab
  - Instantiate prefab assets into the scene
  - Apply / revert full prefab data or individual prefab-backed properties
  - Prefab override detection
- **Scene serialization**
  - Scene save/load to JSON-like text assets
  - Prefab asset persistence
  - Stable instance IDs and parent relationships
  - Editor file menu integration for scene save/load
- **Camera and editor interaction systems**
  - Engine-owned camera update once per frame
  - Scene consumes camera via `SceneRenderContext`
  - Viewport picking and hover ray tests
  - Optional editor scene culling toggle
- **Materials and lighting controls**
  - Per-instance material assignment
  - Directional light parameters consumed by the scene pass
  - Texture-backed material channels
  - Normal green-channel flip support
- **Robust resize authority**
  - Deferred resize requests for swapchain and scene RT
  - GPU-idle enforcement before `ResizeBuffers`
  - Client-rect–authoritative sizing
- **Diagnostics and recovery**
  - DRED-enabled device recovery support
  - Frame health validation
  - DX12 / DXGI logging
  - Throttled engine logging and invariant checks
  - GPU timestamp-based frame timing
  - PIX / GPU event marker support
- **Safe GPU resource lifetime handling**
  - Fence-stamped deferred resource release
  - Safer destruction of GPU-referenced objects across frames
- **Experimental Black Flame assistant tooling**
  - Editor-integrated assistant panels
  - Scene-event-aware feedback hooks
  - Reactive audio/visual support

---

## Architecture Principles

- **The engine owns the frame** — not ImGui, not the scene
- **Render passes are explicit and documented**
- **Scene rendering is a consumer, not a frame owner**
- **Resize follows request → apply, never immediate mutation**
- **No GPU resource is created, resized, or destroyed arbitrarily mid-frame**
- **Editor-facing workflows should remain debuggable and deterministic**
- **Correctness over convenience**

Detailed design notes are available in:
- `ENGINE_CONTRACT.md`
- `docs/Phase3F_RenderGraphNotes.md`
- `docs/CurrentArchitecture.md`
- `docs/EditorWorkflow.md`
- `docs/RenderingStatus.md`
- `docs/Roadmap.md`
- `docs/CHANGELOG_0.3.0.md`

---

## Documentation Guide

- `README.md` - project overview and current status
- `ENGINE_CONTRACT.md` - frame ownership and rendering contract rules
- `docs/CurrentArchitecture.md` - current subsystem layout and responsibilities
- `docs/EditorWorkflow.md` - editor and scene authoring workflow status
- `docs/RenderingStatus.md` - current rendering path and limitations
- `docs/Roadmap.md` - near-term and mid-term priorities
- `docs/CHANGELOG_0.3.0.md` - documented project changes

---

## Project Structure

- `Include/` — public engine interfaces
- `Src/` — engine implementations
- `docs/` — design notes, status docs, changelog, and architecture references
- `shaders/` — HLSL shader sources
- `tools/` — build helpers and support scripts
- `Assets/` — textures, prefabs, and content used by the editor/scene

---

## Build Requirements

- Windows 10 / 11
- Visual Studio 2022
- DirectX 12 compatible GPU
- C++20 or later

---

## Current Status

🚧 **Active Development**

The engine has moved well beyond its original early-renderer milestone and now operates as a real **editor-first DirectX 12 engine foundation**.

### Progress so far

The current codebase already includes:

- a deterministic engine-owned DX12 frame model
- a working offscreen scene viewport rendered into the editor UI
- explicit scene pass ownership and render-state discipline
- primitive-based scene rendering with materials and texture binding
- hierarchy, inspector, assets, diagnostics, and log panels
- scene picking, hover, selection, and multi-selection
- gizmo-based transform editing
- scene parenting / re-parenting with world-transform preservation
- prefab save, instantiate, apply, revert, and override tracking flows
- scene save/load support with stable instance and parent data
- RuntimeWorld play-mode foundation with runtime entities/components and editor diagnostics
- vault gameplay runtime-state bridge for player/camera transforms, nodes, core, exit, rings, mission state, and campaign flow helpers
- camera ownership integrated into scene rendering and viewport interaction
- resize safety, device diagnostics, and frame-health validation

### What this means

This project is no longer just proving that a DX12 renderer can draw geometry. It already has a meaningful editor workflow, scene authoring loop, and a documented ownership model that supports continued engine growth.

Some log messages and older implementation notes still reference earlier historical phase names. Those are still useful as development history, but they no longer describe the full maturity of the current codebase.

### Current emphasis

The next stage is less about basic rendering bring-up and more about:

- scaling the architecture safely
- continuing to split large systems into cleaner units
- hardening serialization and asset workflows
- improving dirty-driven updates and editor polish
- strengthening runtime/play-mode boundaries beyond the initial RuntimeWorld foundation

---

## License

MIT License © 2026 TheFletchZone
