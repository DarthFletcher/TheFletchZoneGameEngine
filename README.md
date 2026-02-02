# TheFletchZoneGameEngine

A custom DirectX 12 game engine written in C++.

## Features
- DirectX 12 rendering pipeline
- Deterministic engine-owned frame lifecycle (BeginFrame → Render → EndFrame → Present)
- Dear ImGui (Win32 + DX12) with docking and multi-viewport support
- Engine-owned ImGui resources (SRV heap, font upload, pipeline state)
- Robust device lost recovery (DRED-enabled)
- Resize-safe swapchain handling
- Custom logging and diagnostics system
- Docking editor UI foundation

## Build Requirements
- Windows 10/11
- Visual Studio 2022
- DirectX 12 compatible GPU

## Status
🚧 **Active Development**

✅ Engine core stabilized (frame lifecycle, ImGui, logging)  
🧱 Scene + shader system scaffolding in progress (Phase 3A)

**Current Focus:** Core engine ownership and stability  
The engine has recently completed a major internal restructuring to ensure:
- Explicit ownership of GPU resources and frame flow
- Deferred and safe ImGui initialization (fonts, PSO, root signature)
- Elimination of implicit, undefined, or backend-driven rendering behavior

This checkpoint establishes a stable foundation for reintroducing
scene rendering, shaders, and gameplay systems in upcoming phases.
