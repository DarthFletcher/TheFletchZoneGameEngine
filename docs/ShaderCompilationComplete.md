# Engine-Side Shader Compilation Integration Complete ?

## Summary

Your DirectX 12 engine now compiles shaders **at runtime** using DXC (DirectX Shader Compiler), bypassing Visual Studio's removed FxCompile system entirely.

---

## ? What's Been Integrated

### 1. **DXC Runtime Compilation Infrastructure**
- **`Src/ShaderCompiler.cpp/h`**: Core DXC compilation with disk cache support
- **`Src/ShaderUtils.cpp/h`**: High-level helpers that auto-route SM6 shaders to DXC
- **`tools/CopyDXCDlls.bat`**: Deploys `dxcompiler.dll` + `dxil.dll` next to the exe at build time

### 2. **MSBuild Integration**
- **`Directory.Build.props`** (NEW): Injects vcpkg DXC headers/libs into all configurations
- **`Directory.Build.targets`**: Runs post-build copy of DXC DLLs and shader sources

### 3. **Shader Model Support**
- **SM 6.x (Shader Model 6+)**: Automatically compiled via DXC
- **SM 5.x and below**: Falls back to legacy FXC (`D3DCompileFromFile`) if needed
- Detection is automatic based on profile string (e.g., `vs_6_0` ? DXC, `vs_5_0` ? FXC)

### 4. **Debug vs Release Options**
```cpp
ShaderCompileOptions opts;
#if defined(_DEBUG)
    opts.enableDebugInfo = true;          // -Zi, -Qembed_debug
    opts.disableOptimizations = true;     // -Od
#else
    opts.enableDebugInfo = false;
    opts.disableOptimizations = false;    // -O3
#endif
opts.enableCache = true;                   // Disk cache at exe/ShaderCache/*.cso
```

---

## ?? File Structure

```
TheFletchZoneGameEngine/
??? Directory.Build.props          ? NEW - Vcpkg paths for DXC
??? Directory.Build.targets        ? UPDATED - Post-build DXC DLL + shader copy
??? tools/
?   ??? CopyDXCDlls.bat           ? Deploys dxcompiler.dll + dxil.dll
?   ??? AddVcpkgIncludePath.ps1   (helper script)
??? Src/
?   ??? ShaderCompiler.cpp/h      ? DXC compilation + cache logic
?   ??? ShaderUtils.cpp/h         ? High-level helpers (auto-routing)
??? shaders/
?   ??? *.hlsl                     ? Copied to output at build (for runtime loading)
??? vcpkg_installed/x64-windows/
    ??? include/directx-dxc/       ? DXC SDK headers
    ??? lib/dxcompiler.lib         ? Import lib
    ??? bin/dxcompiler.dll         ? Runtime DLL (copied to exe dir)
```

---

## ?? How to Use in Your Engine

### **Option A: Direct DXC Call (SM6+ shaders)**
```cpp
#include "ShaderUtils.h"

ShaderCompileOptions opts;
opts.enableDebugInfo = true;
opts.enableCache = true;

auto vertexShader = CompileShaderDXCFromRelativeFile(
    L"shaders\\MyShader_VS.hlsl",
    L"main",
    L"vs_6_0",
    opts
);

// vertexShader is a ComPtr<ID3DBlob> ready for CreateGraphicsPipelineState
```

### **Option B: Auto-Routing Helper (SM5 or SM6)**
```cpp
#include "ShaderUtils.h"

// Automatically picks DXC for SM6, FXC for SM5
auto pixelShader = CompileShaderFromFile(
    L"shaders\\MyShader_PS.hlsl",
    "main",
    "ps_6_0"  // Uses DXC
);

auto legacyShader = CompileShaderFromFile(
    L"shaders\\Legacy_PS.hlsl",
    "main",
    "ps_5_0"  // Uses FXC
);
```

---

## ?? Configuration Options

### **ShaderCompileOptions Fields**
| Field | Description | Debug Default | Release Default |
|-------|-------------|---------------|-----------------|
| `enableDebugInfo` | Embed PDB info in DXIL | `true` | `false` |
| `disableOptimizations` | Skip optimizations (-Od) | `true` | `false` |
| `treatWarningsAsErrors` | -WX flag | `false` | `false` |
| `enableCache` | Disk cache at `ShaderCache/*.cso` | `true` | `true` |
| `cacheDirectory` | Cache folder (relative to exe) | `"ShaderCache"` | `"ShaderCache"` |
| `additionalIncludeDirs` | Extra `-I` paths for DXC | `{}` | `{}` |

### **Cache Behavior**
- Cached shaders are stored as `{ShaderName}_{EntryPoint}_{Profile}_{OptionsHash}.cso`
- Cache is invalidated when:
  - Source `.hlsl` file is modified (timestamp check)
  - Compile options change (hash comparison)

---

## ?? Build System Behavior

### **Post-Build Automatic Steps**
1. **`CopyDXCDlls`**: Copies `dxcompiler.dll` + `dxil.dll` to output directory
2. **`CopyRuntimeShaders`**: Copies all `shaders/**/*.hlsl` next to the exe

### **Linker Configuration**
- Links against `dxcompiler.lib` from vcpkg
- Includes `<directx-dxc/dxcapi.h>` from vcpkg

---

## ??? Troubleshooting

### **"DxcCreateInstance not found"**
? **Fixed** - `Directory.Build.props` now adds `dxcompiler.lib` automatically

### **"IDxcUtils/IDxcCompiler3 undeclared"**
? **Fixed** - vcpkg include path now injected via `Directory.Build.props`

### **"dxcompiler.dll missing at runtime"**
? **Fixed** - `Directory.Build.targets` runs `CopyDXCDlls.bat` after every build

### **Cache not invalidating after editing shader**
- The cache checks file timestamps. If still stale, manually delete `ShaderCache/*.cso`

---

## ?? Visual Studio Shader Items

All `.hlsl` files in your project are marked as:
```xml
<ItemGroup>
  <None Include="shaders\*.hlsl" />
</ItemGroup>
```

**Why `<None>`?**
- Visual Studio 17.14+ removed FxCompile from the UI
- Marking as `<None>` prevents VS from trying (and failing) to compile them
- Your engine compiles them at runtime instead

---

## ?? Next Steps (Optional Improvements)

### **1. Hot-Reload Shaders**
```cpp
// Watch shaders\ folder and recompile + reload PSOs on file change
```

### **2. Precompile Shaders for Release**
```cpp
// Add a custom build step to compile all shaders to .cso at build time
// Then load precompiled blobs in Release builds
```

### **3. Async Shader Compilation**
```cpp
// Use std::async to compile shaders on background threads during level load
```

### **4. Shader Permutations**
```cpp
// Add preprocessor defines to ShaderCompileOptions::additionalDefines
// Generate variants for different quality levels
```

---

## ?? References

- **DXC Documentation**: https://github.com/microsoft/DirectXShaderCompiler
- **Shader Model 6**: https://docs.microsoft.com/en-us/windows/win32/direct3dhlsl/hlsl-shader-model-6-0-features-for-direct3d-12
- **Your ShaderCompiler.cpp**: See inline comments for advanced options

---

## ? Integration Status

| Component | Status |
|-----------|--------|
| DXC Headers | ? Resolved via vcpkg |
| DXC Library | ? Linked via vcpkg |
| Runtime DLLs | ? Auto-copied at build |
| Shader Sources | ? Auto-copied at build |
| Cache System | ? Functional |
| SM6 Auto-Routing | ? Implemented |
| Debug/Release Flags | ? Configured |

**Your engine is now fully independent of Visual Studio's shader compilation system.** ??
