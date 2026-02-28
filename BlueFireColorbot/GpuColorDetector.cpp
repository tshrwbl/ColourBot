#include "GpuColorDetector.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <limits>

#pragma comment(lib, "d3dcompiler.lib")

namespace colourbot {

namespace {

constexpr char kComputeShaderSource[] = R"(
cbuffer DetectionConstants : register(b0) {
    uint Width;
    uint Height;
    uint MinX;
    uint MaxX;
    uint MinY;
    uint MaxY;
    uint CenterX;
    uint CenterY;
    uint GroupsX;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

Texture2D<float4> SourceTexture : register(t0);
RWStructuredBuffer<uint4> GroupResults : register(u0);

groupshared uint SharedScore[256];
groupshared uint SharedX[256];
groupshared uint SharedY[256];
groupshared uint SharedFound[256];

bool IsPurple(uint red, uint green, uint blue) {
    if (green >= 170) {
        return false;
    }

    if (green >= 120) {
        return (abs((int)red - (int)blue) <= 8) &&
               (red - green >= 50) &&
               (blue - green >= 50) &&
               (red >= 105) &&
               (blue >= 105);
    }

    return (abs((int)red - (int)blue) <= 13) &&
           (red - green >= 60) &&
           (blue - green >= 60) &&
           (red >= 110) &&
           (blue >= 100);
}

[numthreads(16, 16, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint groupIndex : SV_GroupIndex,
    uint3 groupId : SV_GroupID)
{
    uint score = 0xffffffffu;
    uint outX = 0u;
    uint outY = 0u;
    uint found = 0u;

    if (dispatchThreadId.x < Width &&
        dispatchThreadId.y < Height &&
        dispatchThreadId.x >= MinX &&
        dispatchThreadId.x < MaxX &&
        dispatchThreadId.y >= MinY &&
        dispatchThreadId.y < MaxY)
    {
        float4 color = SourceTexture.Load(int3(dispatchThreadId.xy, 0));
        uint blue = (uint)round(saturate(color.x) * 255.0f);
        uint green = (uint)round(saturate(color.y) * 255.0f);
        uint red = (uint)round(saturate(color.z) * 255.0f);

        if (IsPurple(red, green, blue)) {
            int dx = (int)dispatchThreadId.x - (int)CenterX;
            int dy = (int)dispatchThreadId.y - (int)CenterY;
            score = (uint)(dx * dx + dy * dy);
            outX = dispatchThreadId.x;
            outY = dispatchThreadId.y;
            found = 1u;
        }
    }

    SharedScore[groupIndex] = score;
    SharedX[groupIndex] = outX;
    SharedY[groupIndex] = outY;
    SharedFound[groupIndex] = found;

    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128u; stride > 0u; stride >>= 1u) {
        if (groupIndex < stride) {
            const uint other = groupIndex + stride;
            const bool takeOther =
                (SharedFound[other] != 0u) &&
                ((SharedFound[groupIndex] == 0u) || (SharedScore[other] < SharedScore[groupIndex]));

            if (takeOther) {
                SharedScore[groupIndex] = SharedScore[other];
                SharedX[groupIndex] = SharedX[other];
                SharedY[groupIndex] = SharedY[other];
                SharedFound[groupIndex] = SharedFound[other];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0u) {
        const uint groupLinear = groupId.y * GroupsX + groupId.x;
        GroupResults[groupLinear] = uint4(SharedScore[0], SharedX[0], SharedY[0], SharedFound[0]);
    }
}
)";

} // namespace

bool GpuColorDetector::Initialize(ID3D11Device* device, ID3D11Texture2D* sourceTexture, int width, int height) {
	if (device == nullptr || sourceTexture == nullptr || width <= 0 || height <= 0) {
		return false;
	}

	width_ = width;
	height_ = height;
	groupsX_ = (width_ + 15) / 16;
	groupsY_ = (height_ + 15) / 16;

	if (!CreateShader(device)) {
		return false;
	}
	if (!CreateSourceView(device, sourceTexture)) {
		return false;
	}
	if (!CreateBuffers(device)) {
		return false;
	}
	return true;
}

bool GpuColorDetector::IsReady() const noexcept {
	return computeShader_ != nullptr &&
		constantsBuffer_ != nullptr &&
		groupResultBuffer_ != nullptr &&
		groupReadbackBuffer_ != nullptr &&
		sourceTextureSrv_ != nullptr &&
		groupResultUav_ != nullptr;
}

bool GpuColorDetector::Detect(ID3D11DeviceContext* context, const GpuDetectionParams& params, Vector2& targetOut) {
	if (!IsReady() || context == nullptr) {
		return false;
	}

	const ScanBounds clamped{
		(std::max)(0, params.bounds.minX),
		(std::min)(width_, params.bounds.maxX),
		(std::max)(0, params.bounds.minY),
		(std::min)(height_, params.bounds.maxY)
	};

	DetectionConstants constants{};
	constants.width = static_cast<std::uint32_t>(width_);
	constants.height = static_cast<std::uint32_t>(height_);
	constants.minX = static_cast<std::uint32_t>(clamped.minX);
	constants.maxX = static_cast<std::uint32_t>(clamped.maxX);
	constants.minY = static_cast<std::uint32_t>(clamped.minY);
	constants.maxY = static_cast<std::uint32_t>(clamped.maxY);
	constants.centerX = static_cast<std::uint32_t>(params.centerX);
	constants.centerY = static_cast<std::uint32_t>(params.centerY);
	constants.groupsX = static_cast<std::uint32_t>(groupsX_);

	context->UpdateSubresource(constantsBuffer_.Get(), 0, nullptr, &constants, 0, 0);

	ID3D11ShaderResourceView* srvs[] = { sourceTextureSrv_.Get() };
	ID3D11UnorderedAccessView* uavs[] = { groupResultUav_.Get() };
	ID3D11Buffer* cbs[] = { constantsBuffer_.Get() };

	context->CSSetShader(computeShader_.Get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, cbs);
	context->CSSetShaderResources(0, 1, srvs);
	context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
	context->Dispatch(static_cast<UINT>(groupsX_), static_cast<UINT>(groupsY_), 1);

	ID3D11ShaderResourceView* nullSrv[] = { nullptr };
	ID3D11UnorderedAccessView* nullUav[] = { nullptr };
	context->CSSetShaderResources(0, 1, nullSrv);
	context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);

	context->CopyResource(groupReadbackBuffer_.Get(), groupResultBuffer_.Get());

	D3D11_MAPPED_SUBRESOURCE mapped{};
	const HRESULT mapResult = context->Map(groupReadbackBuffer_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	if (FAILED(mapResult)) {
		return false;
	}

	const auto* results = static_cast<const GroupResult*>(mapped.pData);
	const int groupCount = groupsX_ * groupsY_;
	bool foundAny = false;
	std::uint32_t bestScore = (std::numeric_limits<std::uint32_t>::max)();
	int bestX = 0;
	int bestY = 0;
	for (int i = 0; i < groupCount; ++i) {
		const auto& item = results[i];
		if (item.found == 0) {
			continue;
		}
		if (!foundAny || item.score < bestScore) {
			foundAny = true;
			bestScore = item.score;
			bestX = static_cast<int>(item.x);
			bestY = static_cast<int>(item.y);
		}
	}

	context->Unmap(groupReadbackBuffer_.Get(), 0);

	if (!foundAny) {
		return false;
	}

	targetOut = Vector2(bestX - params.centerX, bestY - params.centerY);
	return true;
}

bool GpuColorDetector::CreateShader(ID3D11Device* device) {
	Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	const HRESULT compileResult = D3DCompile(
		kComputeShaderSource,
		sizeof(kComputeShaderSource) - 1,
		nullptr,
		nullptr,
		nullptr,
		"main",
		"cs_5_0",
		0,
		0,
		&shaderBlob,
		&errorBlob);
	if (FAILED(compileResult) || shaderBlob == nullptr) {
		return false;
	}

	const HRESULT createResult = device->CreateComputeShader(
		shaderBlob->GetBufferPointer(),
		shaderBlob->GetBufferSize(),
		nullptr,
		&computeShader_);
	return SUCCEEDED(createResult);
}

bool GpuColorDetector::CreateSourceView(ID3D11Device* device, ID3D11Texture2D* sourceTexture) {
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	const HRESULT result = device->CreateShaderResourceView(sourceTexture, &srvDesc, &sourceTextureSrv_);
	return SUCCEEDED(result);
}

bool GpuColorDetector::CreateBuffers(ID3D11Device* device) {
	D3D11_BUFFER_DESC constantsDesc{};
	constantsDesc.ByteWidth = sizeof(DetectionConstants);
	constantsDesc.Usage = D3D11_USAGE_DEFAULT;
	constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(device->CreateBuffer(&constantsDesc, nullptr, &constantsBuffer_))) {
		return false;
	}

	const int groupCount = groupsX_ * groupsY_;
	D3D11_BUFFER_DESC groupResultDesc{};
	groupResultDesc.ByteWidth = static_cast<UINT>(groupCount * sizeof(GroupResult));
	groupResultDesc.Usage = D3D11_USAGE_DEFAULT;
	groupResultDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	groupResultDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	groupResultDesc.StructureByteStride = sizeof(GroupResult);
	if (FAILED(device->CreateBuffer(&groupResultDesc, nullptr, &groupResultBuffer_))) {
		return false;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = static_cast<UINT>(groupCount);
	if (FAILED(device->CreateUnorderedAccessView(groupResultBuffer_.Get(), &uavDesc, &groupResultUav_))) {
		return false;
	}

	D3D11_BUFFER_DESC readbackDesc = groupResultDesc;
	readbackDesc.Usage = D3D11_USAGE_STAGING;
	readbackDesc.BindFlags = 0;
	readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	readbackDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	if (FAILED(device->CreateBuffer(&readbackDesc, nullptr, &groupReadbackBuffer_))) {
		return false;
	}

	return true;
}

} // namespace colourbot
