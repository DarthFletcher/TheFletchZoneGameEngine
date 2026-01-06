#include "ShaderUtils.h"
#include <stdexcept>

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* source, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to compile shader");
    }
    return shaderBlob;
}
