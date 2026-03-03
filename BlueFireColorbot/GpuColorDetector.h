#pragma once

#include "DetectionCommon.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace colourbot {

struct GpuDetectionParams {
	ScanBounds bounds{};
	int centerX{};
	int centerY{};
};

class GpuColorDetector {
public:
	bool Initialize(ID3D11Device* device, ID3D11Texture2D* sourceTexture, int width, int height);
	[[nodiscard]] bool IsReady() const noexcept;
	bool Detect(ID3D11DeviceContext* context, const GpuDetectionParams& params, Vector2& targetOut);

private:
	struct alignas(16) DetectionConstants {
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t minX{};
		std::uint32_t maxX{};
		std::uint32_t minY{};
		std::uint32_t maxY{};
		std::uint32_t centerX{};
		std::uint32_t centerY{};
		std::uint32_t groupsX{};
		std::uint32_t padding[3]{};
	};

	struct GroupResult {
		std::uint32_t score{};
		std::uint32_t x{};
		std::uint32_t y{};
		std::uint32_t found{};
	};

	static constexpr int kReadbackBufferCount = 2;

	bool CreateShaders(ID3D11Device* device);
	bool CreateBuffers(ID3D11Device* device);
	bool CreateSourceView(ID3D11Device* device, ID3D11Texture2D* sourceTexture);

	int width_{ 0 };
	int height_{ 0 };
	int groupsX_{ 0 };
	int groupsY_{ 0 };
	int readbackWriteIndex_{ 0 };
	std::array<bool, kReadbackBufferCount> readbackReady_{};

	Microsoft::WRL::ComPtr<ID3D11ComputeShader> scanComputeShader_;
	Microsoft::WRL::ComPtr<ID3D11ComputeShader> reduceComputeShader_;
	Microsoft::WRL::ComPtr<ID3D11Buffer> constantsBuffer_;
	Microsoft::WRL::ComPtr<ID3D11Buffer> groupResultBuffer_;
	Microsoft::WRL::ComPtr<ID3D11Buffer> finalResultBuffer_;
	std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, kReadbackBufferCount> finalReadbackBuffers_{};
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceTextureSrv_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> groupResultSrv_;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> groupResultUav_;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> finalResultUav_;
};

} // namespace colourbot

