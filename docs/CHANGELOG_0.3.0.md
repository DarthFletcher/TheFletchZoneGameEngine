## [Unreleased]

### Added
- `docs/CurrentArchitecture.md` to capture the current engine/editor structure
- `docs/EditorWorkflow.md` to document scene authoring, selection, hierarchy, gizmo, and prefab flows
- `docs/RenderingStatus.md` to describe the current scene viewport and rendering path
- `docs/Roadmap.md` to summarize current priorities and likely next milestones

### Changed
- Refreshed `README.md` so it reflects the current editor-heavy DX12 engine state instead of the older Phase 3 / early Phase 4 snapshot
- Expanded `ENGINE_CONTRACT.md` with scene viewport, scene render, editor interaction, and serialization expectations

### Notes
- This changelog entry is documentation-focused and does not introduce runtime behavior changes

## [0.3.0] - Camera Ownership

### Added
- Engine-owned camera system with explicit view/projection ownership
- Deterministic per-frame camera update
- `SceneRenderContext` camera contract

### Changed
- Scene rendering now consumes camera data exclusively from context
- Removed implicit matrix construction from `Scene`

### Notes
- Matrix convention locked: LH, CPU-transposed for HLSL `mul(float4, gViewProj)`
