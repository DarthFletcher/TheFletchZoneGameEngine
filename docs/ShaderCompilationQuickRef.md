# Quick Reference: Engine Shader Compilation

## ?? Compile a Shader (C++ Code)

### For Shader Model 6+ (Recommended)
```cpp
#include "ShaderUtils.h"

ShaderCompileOptions opts;
#ifdef _DEBUG
    opts.enableDebugInfo = true;
    opts.disableOptimizations = true;
#endif

auto blob = CompileShaderDXCFromRelativeFile(
    L"shaders\\MyShader.hlsl",
    L"VSMain",         // Entry point
    L"vs_6_0",        // Target profile
    opts
);

// Use blob->GetBufferPointer() and blob->GetBufferSize() in pipeline creation
```

### Auto-Detect Compiler (SM5 or SM6)
```cpp
// Uses DXC for vs_6_0, FXC for vs_5_0
auto blob = CompileShaderFromFile(
    L"shaders\\MyShader.hlsl",
    "VSMain",
    "vs_6_0"  // or "ps_6_5", "cs_6_0", etc.
);
```

---

## ?? Add a New Shader to Project

1. Create `shaders/MyShader.hlsl`
2. In Visual Studio, add to project as **"None"** (not "HLSL Compiler")
3. That's it! MSBuild will copy it to output automatically

**Don't:**
- ? Set Item Type to "HLSL Compiler" (broken in VS 2022 17.14+)
- ? Manually configure shader properties
- ? Add FxCompile entries to vcxproj

**The engine compiles shaders at runtime.**

---

## ??? Shader Cache Location

```
x64/Debug/ShaderCache/
??? MyShader_VSMain_vs_6_0_ABC123.cso
??? MyShader_PSMain_ps_6_0_DEF456.cso
??? ...
```

Cache is **automatic**. To force recompile, delete these `.cso` files.

---

## ?? Common Issues

### Build Error: "Cannot open include file dxcapi.h"
**Fix:** `Directory.Build.props` should inject vcpkg paths. If missing, re-add:
```xml
<AdditionalIncludeDirectories>$(MSBuildProjectDirectory)\vcpkg_installed\x64-windows\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
```

### Runtime Error: "dxcompiler.dll not found"
**Fix:** `Directory.Build.targets` should run `CopyDXCDlls.bat` after build. Verify:
```bash
dir x64\Debug\dxcompiler.dll
dir x64\Debug\dxil.dll
```

### Shader Compilation Failed
**Check:**
1. Output window in Visual Studio for DXC errors
2. Shader syntax (must be valid for target profile)
3. Entry point name matches
4. File path is correct (relative to exe)

---

## ?? Configuration

### Enable/Disable Cache
```cpp
opts.enableCache = false;  // Compile every time (slower, but ensures fresh build)
```

### Add Include Directories
```cpp
opts.additionalIncludeDirs.push_back(L"shaders/common");
opts.additionalIncludeDirs.push_back(L"shaders/includes");
```

### Treat Warnings as Errors
```cpp
opts.treatWarningsAsErrors = true;  // Good for CI/CD
```

---

## ?? Supported Shader Profiles

### Shader Model 6.x (DXC)
- `vs_6_0` ? `vs_6_8`
- `ps_6_0` ? `ps_6_8`
- `cs_6_0` ? `cs_6_8`
- `lib_6_3` ? `lib_6_8` (ray tracing)
- `ms_6_5` ? `ms_6_8` (mesh shaders)
- `as_6_5` ? `as_6_8` (amplification shaders)

### Shader Model 5.x and below (FXC)
- `vs_5_0`, `vs_5_1`
- `ps_5_0`, `ps_5_1`
- `cs_5_0`, `cs_5_1`

---

## ?? Example: Full Pipeline Creation

```cpp
#include "ShaderUtils.h"

void CreateMyPipeline() {
    // 1. Compile shaders
    ShaderCompileOptions opts;
#ifdef _DEBUG
    opts.enableDebugInfo = true;
    opts.disableOptimizations = true;
#endif

    auto vs = CompileShaderDXCFromRelativeFile(L"shaders\\Forward.hlsl", L"VSMain", L"vs_6_0", opts);
    auto ps = CompileShaderDXCFromRelativeFile(L"shaders\\Forward.hlsl", L"PSMain", L"ps_6_0", opts);

    // 2. Create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = myRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    // ... fill out rest of psoDesc ...

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&myPSO));
}
```

---

## ? That's It!

Your engine now owns shader compilation end-to-end. Visual Studio just stores the `.hlsl` source files.
