#pragma once
#include <wrl.h>
#include <d3dcompiler.h>

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* source, const char* target);
Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(const wchar_t* filename, const char* entryPoint, const char* target);

// Compiles from a relative path (e.g. L"shaders\\scene_grid_vs.hlsl") resolved against the executable directory
Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromRelativeFile(const wchar_t* relativePath, const char* entryPoint, const char* target);
