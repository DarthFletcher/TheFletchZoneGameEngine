## [0.3.0] – Camera Ownership

### Added
- Engine-owned camera system with explicit view/projection ownership
- Deterministic per-frame camera update
- SceneRenderContext camera contract

### Changed
- Scene rendering now consumes camera data exclusively from context
- Removed implicit matrix construction from Scene

### Notes
- Matrix convention locked: LH, CPU-transposed for HLSL mul(float4, gViewProj)
