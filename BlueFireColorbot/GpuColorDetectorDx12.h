#pragma once

#include "DetectionCommon.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>

struct IDxcBlob;

namespace colourbot {

class GpuColorDetectorDx12 {
public:
	~GpuColorDetectorDx12();

	bool Initialize(int width, int height, HWND gameWindow);
	[[nodiscard]] bool IsReady() const noexcept;
	bool Detect(const GpuDetectionParams& params, Vector2& targetOut);

private:
	struct GroupResult {
		std::uint32_t score{};
		std::uint32_t x{};
		std::uint32_t y{};
		std::uint32_t found{};
	};

	struct alignas(16) ScanConstants {
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t minX{};
		std::uint32_t maxX{};
		std::uint32_t minY{};
		std::uint32_t maxY{};
		std::uint32_t centerX{};
		std::uint32_t centerY{};
		std::uint32_t groupsX{};
		std::uint32_t wavesPerGroup{};
		std::uint32_t padding[2]{};
	};

	struct alignas(16) ReduceConstants {
		std::uint32_t candidateCount{};
		std::uint32_t padding0{};
		std::uint32_t padding1{};
		std::uint32_t padding2{};
	};

	struct alignas(16) FinalConstants {
		std::uint32_t stageCount{};
		std::uint32_t padding0{};
		std::uint32_t padding1{};
		std::uint32_t padding2{};
	};

	static constexpr int kFramesInFlight = 2;
	static constexpr int kThreadsPerGroup = 16 * 16;
	static constexpr int kMaxWavesPerGroup = 8;

	struct FrameContext {
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
		Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
		GroupResult* mappedReadback = nullptr;
		std::uint64_t submittedFenceValue = 0;
	};

	struct CaptureContext;

	bool CreateDeviceAndQueue();
	bool CreateCaptureInfrastructure();
	bool CheckFeatureSupport();
	bool CreateCommandObjects();
	bool CreateDescriptors();
	bool CreateResources();
	bool CreatePipelines();
	bool CaptureFrameToTexture();
	bool WaitForCapturedFrame();
	bool CompileComputeShader(const char* source, const wchar_t* targetProfile, Microsoft::WRL::ComPtr<IDxcBlob>& blobOut);
	void CloseFenceEvent() noexcept;
	void CloseCaptureSharedHandles() noexcept;
	bool SubmitAndSignal(FrameContext& frame);
	bool TryConsumeResult(const FrameContext& frame, const GpuDetectionParams& params, Vector2& targetOut) const;
	[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(std::uint32_t descriptorIndex) const noexcept;
	[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(std::uint32_t descriptorIndex) const noexcept;
	void Transition(
		ID3D12GraphicsCommandList* list,
		ID3D12Resource* resource,
		D3D12_RESOURCE_STATES beforeState,
		D3D12_RESOURCE_STATES afterState) const;

	int width_{ 0 };
	int height_{ 0 };
	int groupsX_{ 0 };
	int groupsY_{ 0 };
	int groupsCount_{ 0 };
	int wavesPerGroup_{ 0 };
	HWND gameWindow_{ nullptr };

	std::uint32_t currentFrameIndex_{ 0 };
	std::uint64_t nextFenceValue_{ 1 };

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	HANDLE fenceEvent_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> scanPso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> reducePso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> finalPso_;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
	std::uint32_t descriptorSize_{ 0 };

	Microsoft::WRL::ComPtr<ID3D12Resource> captureTexture_;
	CaptureContext* capture_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> waveResultsBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> stage2Buffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> finalResultBuffer_;

	std::array<FrameContext, kFramesInFlight> frames_{};

	D3D12_RESOURCE_STATES captureTextureState_ = D3D12_RESOURCE_STATE_COMMON;
	D3D12_RESOURCE_STATES waveResultsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	D3D12_RESOURCE_STATES stage2State_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	D3D12_RESOURCE_STATES finalResultState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

} // namespace colourbot
