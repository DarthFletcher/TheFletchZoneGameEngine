#pragma once

#include <DirectXMath.h>

struct VertexPC
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT4 Color;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 UV;
};
