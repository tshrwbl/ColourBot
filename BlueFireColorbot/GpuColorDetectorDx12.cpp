#include "GpuColorDetectorDx12.h"

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <array>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")

namespace colourbot {

namespace {

constexpr std::uint32_t kRootConstantCount = 16;

constexpr std::uint32_t kRootParamConstants = 0;
constexpr std::uint32_t kRootParamSrvTable = 1;
constexpr std::uint32_t kRootParamUavTable = 2;

constexpr std::uint32_t kSrvCaptureTexture = 0;
constexpr std::uint32_t kSrvWaveResults = 1;
constexpr std::uint32_t kSrvStage2 = 2;
constexpr std::uint32_t kUavWaveResults = 3;
constexpr std::uint32_t kUavStage2 = 4;
constexpr std::uint32_t kUavFinalResult = 5;
constexpr std::uint32_t kDescriptorCount = 6;

constexpr char kScanShaderSource[] = R"(
cbuffer ScanConstants : register(b0) {
    uint Width;
    uint Height;
    uint MinX;
    uint MaxX;
    uint MinY;
    uint MaxY;
    uint CenterX;
    uint CenterY;
    uint GroupsX;
    uint WavesPerGroup;
    uint Padding0;
    uint Padding1;
};

Texture2D<float4> SourceTexture : register(t0);
RWStructuredBuffer<uint4> WaveResults : register(u0);

bool IsPurple(uint red, uint green, uint blue) {
    if (green >= 170u) {
        return false;
    }

    if (green >= 120u) {
        return (abs((int)red - (int)blue) <= 8) &&
               (red - green >= 50u) &&
               (blue - green >= 50u) &&
               (red >= 105u) &&
               (blue >= 105u);
    }

    return (abs((int)red - (int)blue) <= 13) &&
           (red - green >= 60u) &&
           (blue - green >= 60u) &&
           (red >= 110u) &&
           (blue >= 100u);
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
    bool found = false;

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
            found = true;
        }
    }

    const uint candidateScore = found ? score : 0xffffffffu;
    const uint waveBestScore = WaveActiveMin(candidateScore);
    const bool waveFound = WaveActiveAnyTrue(found);
    const bool isWaveBest = found && (score == waveBestScore);
    const uint waveBestX = WaveActiveMin(isWaveBest ? outX : 0xffffffffu);
    const uint waveBestY = WaveActiveMin(isWaveBest ? outY : 0xffffffffu);

    if (WaveIsFirstLane()) {
        const uint waveSize = WaveGetLaneCount();
        const uint waveIndex = groupIndex / waveSize;
        const uint groupLinear = groupId.y * GroupsX + groupId.x;
        const uint outIndex = groupLinear * WavesPerGroup + waveIndex;
        WaveResults[outIndex] = uint4(waveBestScore, waveBestX, waveBestY, waveFound ? 1u : 0u);
    }
}
)";

constexpr char kReduceShaderSource[] = R"(
cbuffer ReduceConstants : register(b0) {
    uint CandidateCount;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

StructuredBuffer<uint4> WaveResults : register(t0);
RWStructuredBuffer<uint4> Stage2Results : register(u0);

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint lane = dispatchThreadId.x;
    uint score = 0xffffffffu;
    uint outX = 0u;
    uint outY = 0u;
    bool found = false;

    for (uint i = lane; i < CandidateCount; i += 256u) {
        const uint4 item = WaveResults[i];
        const bool takeItem =
            (item.w != 0u) &&
            ((!found) || (item.x < score));

        if (takeItem) {
            score = item.x;
            outX = item.y;
            outY = item.z;
            found = true;
        }
    }

    const uint candidateScore = found ? score : 0xffffffffu;
    const uint waveBestScore = WaveActiveMin(candidateScore);
    const bool waveFound = WaveActiveAnyTrue(found);
    const bool isWaveBest = found && (score == waveBestScore);
    const uint waveBestX = WaveActiveMin(isWaveBest ? outX : 0xffffffffu);
    const uint waveBestY = WaveActiveMin(isWaveBest ? outY : 0xffffffffu);

    if (WaveIsFirstLane()) {
        const uint waveSize = WaveGetLaneCount();
        const uint waveIndex = lane / waveSize;
        Stage2Results[waveIndex] = uint4(waveBestScore, waveBestX, waveBestY, waveFound ? 1u : 0u);
    }
}
)";

constexpr char kFinalShaderSource[] = R"(
cbuffer FinalConstants : register(b0) {
    uint StageCount;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

StructuredBuffer<uint4> Stage2Results : register(t0);
RWStructuredBuffer<uint4> FinalResult : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint lane = dispatchThreadId.x;
    uint score = 0xffffffffu;
    uint outX = 0u;
    uint outY = 0u;
    bool found = false;

    if (lane < StageCount) {
        const uint4 item = Stage2Results[lane];
        if (item.w != 0u) {
            score = item.x;
            outX = item.y;
            outY = item.z;
            found = true;
        }
    }

    const uint candidateScore = found ? score : 0xffffffffu;
    const uint waveBestScore = WaveActiveMin(candidateScore);
    const bool waveFound = WaveActiveAnyTrue(found);
    const bool isWaveBest = found && (score == waveBestScore);
    const uint waveBestX = WaveActiveMin(isWaveBest ? outX : 0xffffffffu);
    const uint waveBestY = WaveActiveMin(isWaveBest ? outY : 0xffffffffu);

    if (WaveIsFirstLane()) {
        FinalResult[0] = uint4(waveBestScore, waveBestX, waveBestY, waveFound ? 1u : 0u);
    }
}
)";

using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

} // namespace

struct GpuColorDetectorDx12::CaptureContext {
	RECT desktopRect{};
	HMONITOR monitor = nullptr;

	Microsoft::WRL::ComPtr<ID3D11Device> device11;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
	Microsoft::WRL::ComPtr<ID3D11Device5> device11_5;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context11_4;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> sharedTexture11;
	Microsoft::WRL::ComPtr<ID3D11Fence> sharedFence11;
	Microsoft::WRL::ComPtr<ID3D12Fence> sharedFence12;
	HANDLE sharedTextureHandle = nullptr;
	HANDLE sharedFenceHandle = nullptr;
	std::uint64_t latestFenceValue = 0;
	bool shouldRoUninitialize = false;

	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{ nullptr };
	winrt::Windows::Graphics::SizeInt32 itemSize{};
};

bool GpuColorDetectorDx12::Initialize(int width, int height, HWND gameWindow) {
	if (width <= 0 || height <= 0 || gameWindow == nullptr) {
		return false;
	}

	width_ = width;
	height_ = height;
	gameWindow_ = gameWindow;
	groupsX_ = (width_ + 15) / 16;
	groupsY_ = (height_ + 15) / 16;
	groupsCount_ = groupsX_ * groupsY_;
	currentFrameIndex_ = 0;
	nextFenceValue_ = 1;

	if (!CreateDeviceAndQueue()) {
		return false;
	}
	if (!CheckFeatureSupport()) {
		return false;
	}
	if (!CreateCaptureInfrastructure()) {
		return false;
	}
	if (!CreateCommandObjects()) {
		return false;
	}
	if (!CreateDescriptors()) {
		return false;
	}
	if (!CreateResources()) {
		return false;
	}
	if (!CreatePipelines()) {
		return false;
	}

	return true;
}

bool GpuColorDetectorDx12::IsReady() const noexcept {
	if (device_ == nullptr ||
		queue_ == nullptr ||
		commandList_ == nullptr ||
		fence_ == nullptr ||
		rootSignature_ == nullptr ||
		scanPso_ == nullptr ||
		reducePso_ == nullptr ||
		finalPso_ == nullptr ||
		descriptorHeap_ == nullptr ||
		capture_ == nullptr ||
		capture_->device11 == nullptr ||
		capture_->context11 == nullptr ||
		capture_->device11_5 == nullptr ||
		capture_->context11_4 == nullptr ||
		capture_->sharedTexture11 == nullptr ||
		capture_->sharedFence11 == nullptr ||
		capture_->sharedFence12 == nullptr ||
		capture_->framePool == nullptr ||
		capture_->session == nullptr ||
		captureTexture_ == nullptr ||
		waveResultsBuffer_ == nullptr ||
		stage2Buffer_ == nullptr ||
		finalResultBuffer_ == nullptr ||
		gameWindow_ == nullptr ||
		wavesPerGroup_ <= 0) {
		return false;
	}

	for (const auto& frame : frames_) {
		if (frame.allocator == nullptr || frame.readbackBuffer == nullptr || frame.mappedReadback == nullptr) {
			return false;
		}
	}

	return true;
}

bool GpuColorDetectorDx12::CreateDeviceAndQueue() {
	Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		return false;
	}

	for (UINT adapterIndex = 0;; ++adapterIndex) {
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC1 desc{};
		if (FAILED(adapter->GetDesc1(&desc))) {
			continue;
		}
		if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
			continue;
		}

		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
			break;
		}
	}

	if (device_ == nullptr) {
		return false;
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.NodeMask = 0;
	if (FAILED(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)))) {
		return false;
	}

	return true;
}

bool GpuColorDetectorDx12::CreateCaptureInfrastructure() {
	const HRESULT roInitResult = RoInitialize(RO_INIT_MULTITHREADED);
	if (FAILED(roInitResult) && roInitResult != RPC_E_CHANGED_MODE) {
		return false;
	}

	if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
		return false;
	}

	delete capture_;
	capture_ = new CaptureContext();
	capture_->shouldRoUninitialize = (roInitResult == S_OK || roInitResult == S_FALSE);

	Microsoft::WRL::ComPtr<IDXGIDevice> d3d12DxgiDevice;
	if (FAILED(device_.As(&d3d12DxgiDevice))) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	if (FAILED(d3d12DxgiDevice->GetAdapter(&adapter))) {
		return false;
	}

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	if (FAILED(D3D11CreateDevice(
		adapter.Get(),
		D3D_DRIVER_TYPE_UNKNOWN,
		nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&capture_->device11,
		&featureLevel,
		&capture_->context11))) {
		return false;
	}

	if (FAILED(capture_->device11.As(&capture_->device11_5))) {
		return false;
	}
	if (FAILED(capture_->context11.As(&capture_->context11_4))) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDXGIDevice> captureDxgiDevice;
	if (FAILED(capture_->device11.As(&captureDxgiDevice))) {
		return false;
	}

	try {
		winrt::com_ptr<::IInspectable> inspectableDevice;
		if (FAILED(::CreateDirect3D11DeviceFromDXGIDevice(captureDxgiDevice.Get(), inspectableDevice.put()))) {
			return false;
		}

		capture_->winrtDevice = inspectableDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
	} catch (const winrt::hresult_error&) {
		return false;
	}

	capture_->monitor = MonitorFromWindow(gameWindow_, MONITOR_DEFAULTTONEAREST);
	if (capture_->monitor == nullptr) {
		return false;
	}

	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	if (!GetMonitorInfoW(capture_->monitor, &monitorInfo)) {
		return false;
	}
	capture_->desktopRect = monitorInfo.rcMonitor;

	try {
		auto interopFactory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
		winrt::check_hresult(interopFactory->CreateForMonitor(
			capture_->monitor,
			winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
			reinterpret_cast<void**>(winrt::put_abi(item))));

		capture_->item = item;
		capture_->itemSize = capture_->item.Size();
		if (capture_->itemSize.Width <= 0 || capture_->itemSize.Height <= 0) {
			return false;
		}

		capture_->framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
			capture_->winrtDevice,
			winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
			2,
			capture_->itemSize);
		capture_->session = capture_->framePool.CreateCaptureSession(capture_->item);
		capture_->session.StartCapture();
	} catch (const winrt::hresult_error&) {
		return false;
	}

	return true;
}

bool GpuColorDetectorDx12::CheckFeatureSupport() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
	if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)))) {
		return false;
	}
	if (!options1.WaveOps || options1.WaveLaneCountMin == 0) {
		return false;
	}

	const std::uint32_t waveLaneCount = options1.WaveLaneCountMin;
	if ((kThreadsPerGroup % waveLaneCount) != 0) {
		return false;
	}

	wavesPerGroup_ = static_cast<int>(kThreadsPerGroup / waveLaneCount);
	if (wavesPerGroup_ <= 0 || wavesPerGroup_ > kMaxWavesPerGroup) {
		return false;
	}

	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
	shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_0;
	if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))) {
		return false;
	}
	if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0) {
		return false;
	}

	return true;
}
bool GpuColorDetectorDx12::CreateCommandObjects() {
	for (auto& frame : frames_) {
		if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)))) {
			return false;
		}
	}

	if (FAILED(device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		frames_[0].allocator.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList_)))) {
		return false;
	}

	if (FAILED(commandList_->Close())) {
		return false;
	}

	if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
		return false;
	}

	fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	return fenceEvent_ != nullptr;
}

bool GpuColorDetectorDx12::CreateDescriptors() {
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = kDescriptorCount;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	if (FAILED(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap_)))) {
		return false;
	}

	descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	return descriptorSize_ != 0;
}

bool GpuColorDetectorDx12::CreateResources() {
	if (capture_ == nullptr ||
		capture_->device11 == nullptr ||
		capture_->device11_5 == nullptr) {
		return false;
	}

	D3D12_HEAP_PROPERTIES defaultHeap{};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_HEAP_PROPERTIES readbackHeap{};
	readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

	D3D11_TEXTURE2D_DESC sharedCaptureDesc{};
	sharedCaptureDesc.Width = static_cast<UINT>(width_);
	sharedCaptureDesc.Height = static_cast<UINT>(height_);
	sharedCaptureDesc.MipLevels = 1;
	sharedCaptureDesc.ArraySize = 1;
	sharedCaptureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sharedCaptureDesc.SampleDesc.Count = 1;
	sharedCaptureDesc.Usage = D3D11_USAGE_DEFAULT;
	sharedCaptureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	sharedCaptureDesc.CPUAccessFlags = 0;
	sharedCaptureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	if (FAILED(capture_->device11->CreateTexture2D(&sharedCaptureDesc, nullptr, &capture_->sharedTexture11))) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDXGIResource1> sharedCaptureResource;
	if (FAILED(capture_->sharedTexture11.As(&sharedCaptureResource))) {
		return false;
	}
	if (FAILED(sharedCaptureResource->CreateSharedHandle(
		nullptr,
		DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
		nullptr,
		&capture_->sharedTextureHandle))) {
		return false;
	}

	if (FAILED(device_->OpenSharedHandle(capture_->sharedTextureHandle, IID_PPV_ARGS(&captureTexture_)))) {
		return false;
	}

	if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&capture_->sharedFence12)))) {
		return false;
	}
	if (FAILED(device_->CreateSharedHandle(
		capture_->sharedFence12.Get(),
		nullptr,
		GENERIC_ALL,
		nullptr,
		&capture_->sharedFenceHandle))) {
		return false;
	}
	if (FAILED(capture_->device11_5->OpenSharedFence(capture_->sharedFenceHandle, IID_PPV_ARGS(&capture_->sharedFence11)))) {
		return false;
	}
	capture_->latestFenceValue = 0;

	const std::size_t maxWaveResultBytes =
		static_cast<std::size_t>(groupsCount_) *
		static_cast<std::size_t>(kMaxWavesPerGroup) *
		sizeof(GroupResult);

	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = static_cast<UINT64>(maxWaveResultBytes);
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (FAILED(device_->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&waveResultsBuffer_)))) {
		return false;
	}

	D3D12_RESOURCE_DESC stage2Desc = bufferDesc;
	stage2Desc.Width = static_cast<UINT64>(kMaxWavesPerGroup * sizeof(GroupResult));
	if (FAILED(device_->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&stage2Desc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&stage2Buffer_)))) {
		return false;
	}

	D3D12_RESOURCE_DESC finalDesc = bufferDesc;
	finalDesc.Width = static_cast<UINT64>(sizeof(GroupResult));
	if (FAILED(device_->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&finalDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&finalResultBuffer_)))) {
		return false;
	}

	for (auto& frame : frames_) {
		if (FAILED(device_->CreateCommittedResource(
			&readbackHeap,
			D3D12_HEAP_FLAG_NONE,
			&finalDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&frame.readbackBuffer)))) {
			return false;
		}

		void* mapped = nullptr;
		if (FAILED(frame.readbackBuffer->Map(0, nullptr, &mapped))) {
			return false;
		}
		frame.mappedReadback = static_cast<GroupResult*>(mapped);
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC captureSrv{};
	captureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	captureSrv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	captureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	captureSrv.Texture2D.MostDetailedMip = 0;
	captureSrv.Texture2D.MipLevels = 1;
	captureSrv.Texture2D.PlaneSlice = 0;
	captureSrv.Texture2D.ResourceMinLODClamp = 0.0f;
	device_->CreateShaderResourceView(captureTexture_.Get(), &captureSrv, CpuHandle(kSrvCaptureTexture));

	D3D12_SHADER_RESOURCE_VIEW_DESC waveSrv{};
	waveSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	waveSrv.Format = DXGI_FORMAT_UNKNOWN;
	waveSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	waveSrv.Buffer.FirstElement = 0;
	waveSrv.Buffer.NumElements = static_cast<UINT>(maxWaveResultBytes / sizeof(GroupResult));
	waveSrv.Buffer.StructureByteStride = sizeof(GroupResult);
	waveSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	device_->CreateShaderResourceView(waveResultsBuffer_.Get(), &waveSrv, CpuHandle(kSrvWaveResults));

	D3D12_SHADER_RESOURCE_VIEW_DESC stage2Srv = waveSrv;
	stage2Srv.Buffer.NumElements = kMaxWavesPerGroup;
	device_->CreateShaderResourceView(stage2Buffer_.Get(), &stage2Srv, CpuHandle(kSrvStage2));

	D3D12_UNORDERED_ACCESS_VIEW_DESC waveUav{};
	waveUav.Format = DXGI_FORMAT_UNKNOWN;
	waveUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	waveUav.Buffer.FirstElement = 0;
	waveUav.Buffer.NumElements = static_cast<UINT>(maxWaveResultBytes / sizeof(GroupResult));
	waveUav.Buffer.StructureByteStride = sizeof(GroupResult);
	waveUav.Buffer.CounterOffsetInBytes = 0;
	waveUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	device_->CreateUnorderedAccessView(waveResultsBuffer_.Get(), nullptr, &waveUav, CpuHandle(kUavWaveResults));

	D3D12_UNORDERED_ACCESS_VIEW_DESC stage2Uav = waveUav;
	stage2Uav.Buffer.NumElements = kMaxWavesPerGroup;
	device_->CreateUnorderedAccessView(stage2Buffer_.Get(), nullptr, &stage2Uav, CpuHandle(kUavStage2));

	D3D12_UNORDERED_ACCESS_VIEW_DESC finalUav = waveUav;
	finalUav.Buffer.NumElements = 1;
	device_->CreateUnorderedAccessView(finalResultBuffer_.Get(), nullptr, &finalUav, CpuHandle(kUavFinalResult));

	return true;
}
bool GpuColorDetectorDx12::CompileComputeShader(
	const char* source,
	const wchar_t* targetProfile,
	Microsoft::WRL::ComPtr<IDxcBlob>& blobOut) {
	if (source == nullptr || targetProfile == nullptr) {
		return false;
	}

	static HMODULE dxcModule = LoadLibraryW(L"dxcompiler.dll");
	if (dxcModule == nullptr) {
		return false;
	}

	const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxcModule, "DxcCreateInstance"));
	if (createInstance == nullptr) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDxcLibrary> library;
	Microsoft::WRL::ComPtr<IDxcCompiler> compiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
	if (FAILED(createInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)))) {
		return false;
	}
	if (FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
		return false;
	}
	if (FAILED(library->CreateIncludeHandler(&includeHandler))) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
	const std::size_t sourceLength = std::strlen(source);
	if (FAILED(library->CreateBlobWithEncodingOnHeapCopy(source, static_cast<UINT32>(sourceLength), CP_UTF8, &sourceBlob))) {
		return false;
	}

	static const wchar_t* kArguments[] = {
		L"-HV", L"2021",
		L"-O3",
		L"-Qstrip_debug",
		L"-Qstrip_reflect"
	};

	Microsoft::WRL::ComPtr<IDxcOperationResult> operationResult;
	if (FAILED(compiler->Compile(
		sourceBlob.Get(),
		L"GpuColorDetectorDx12.hlsl",
		L"main",
		targetProfile,
		kArguments,
		_countof(kArguments),
		nullptr,
		0,
		includeHandler.Get(),
		&operationResult))) {
		return false;
	}

	HRESULT compileStatus = E_FAIL;
	if (FAILED(operationResult->GetStatus(&compileStatus)) || FAILED(compileStatus)) {
		return false;
	}

	return SUCCEEDED(operationResult->GetResult(&blobOut));
}

bool GpuColorDetectorDx12::CreatePipelines() {
	Microsoft::WRL::ComPtr<IDxcBlob> scanBlob;
	Microsoft::WRL::ComPtr<IDxcBlob> reduceBlob;
	Microsoft::WRL::ComPtr<IDxcBlob> finalBlob;
	if (!CompileComputeShader(kScanShaderSource, L"cs_6_0", scanBlob)) {
		return false;
	}
	if (!CompileComputeShader(kReduceShaderSource, L"cs_6_0", reduceBlob)) {
		return false;
	}
	if (!CompileComputeShader(kFinalShaderSource, L"cs_6_0", finalBlob)) {
		return false;
	}

	D3D12_DESCRIPTOR_RANGE srvRange{};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_DESCRIPTOR_RANGE uavRange{};
	uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavRange.NumDescriptors = 1;
	uavRange.BaseShaderRegister = 0;
	uavRange.RegisterSpace = 0;
	uavRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER rootParameters[3]{};
	rootParameters[kRootParamConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootParamConstants].Constants.Num32BitValues = kRootConstantCount;
	rootParameters[kRootParamConstants].Constants.RegisterSpace = 0;
	rootParameters[kRootParamConstants].Constants.ShaderRegister = 0;

	rootParameters[kRootParamSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootParamSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootParamSrvTable].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[kRootParamSrvTable].DescriptorTable.pDescriptorRanges = &srvRange;

	rootParameters[kRootParamUavTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootParamUavTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootParamUavTable].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[kRootParamUavTable].DescriptorTable.pDescriptorRanges = &uavRange;

	D3D12_ROOT_SIGNATURE_DESC rootDesc{};
	rootDesc.NumParameters = _countof(rootParameters);
	rootDesc.pParameters = rootParameters;
	rootDesc.NumStaticSamplers = 0;
	rootDesc.pStaticSamplers = nullptr;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> rootSigBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootSigBlob, &errorBlob))) {
		return false;
	}
	if (FAILED(device_->CreateRootSignature(
		0,
		rootSigBlob->GetBufferPointer(),
		rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)))) {
		return false;
	}

	auto createPso = [this](IDxcBlob* shaderBlob, ID3D12PipelineState** psoOut) -> bool {
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSignature_.Get();
		psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
		psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();
		psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		return SUCCEEDED(device_->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(psoOut)));
	};

	return createPso(scanBlob.Get(), scanPso_.GetAddressOf()) &&
		createPso(reduceBlob.Get(), reducePso_.GetAddressOf()) &&
		createPso(finalBlob.Get(), finalPso_.GetAddressOf());
}

void GpuColorDetectorDx12::Transition(
	ID3D12GraphicsCommandList* list,
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES beforeState,
	D3D12_RESOURCE_STATES afterState) const {
	if (beforeState == afterState) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	list->ResourceBarrier(1, &barrier);
}

bool GpuColorDetectorDx12::SubmitAndSignal(FrameContext& frame) {
	if (FAILED(commandList_->Close())) {
		return false;
	}

	ID3D12CommandList* lists[] = { commandList_.Get() };
	queue_->ExecuteCommandLists(1, lists);

	frame.submittedFenceValue = nextFenceValue_;
	if (FAILED(queue_->Signal(fence_.Get(), frame.submittedFenceValue))) {
		return false;
	}

	++nextFenceValue_;
	return true;
}

bool GpuColorDetectorDx12::TryConsumeResult(
	const FrameContext& frame,
	const GpuDetectionParams& params,
	Vector2& targetOut) const {
	if (frame.submittedFenceValue == 0 || frame.mappedReadback == nullptr) {
		return false;
	}
	if (fence_->GetCompletedValue() < frame.submittedFenceValue) {
		return false;
	}

	const GroupResult& result = *frame.mappedReadback;
	if (result.found == 0u) {
		return false;
	}

	targetOut = Vector2(static_cast<int>(result.x) - params.centerX, static_cast<int>(result.y) - params.centerY);
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE GpuColorDetectorDx12::CpuHandle(std::uint32_t descriptorIndex) const noexcept {
	D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<SIZE_T>(descriptorIndex) * static_cast<SIZE_T>(descriptorSize_);
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuColorDetectorDx12::GpuHandle(std::uint32_t descriptorIndex) const noexcept {
	D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<UINT64>(descriptorIndex) * static_cast<UINT64>(descriptorSize_);
	return handle;
}

bool GpuColorDetectorDx12::WaitForCapturedFrame() {
	if (capture_ == nullptr || capture_->sharedFence12 == nullptr) {
		return false;
	}
	if (capture_->latestFenceValue == 0) {
		return true;
	}
	return SUCCEEDED(queue_->Wait(capture_->sharedFence12.Get(), capture_->latestFenceValue));
}

bool GpuColorDetectorDx12::CaptureFrameToTexture() {
	if (capture_ == nullptr ||
		capture_->framePool == nullptr ||
		capture_->context11 == nullptr ||
		capture_->context11_4 == nullptr ||
		capture_->sharedTexture11 == nullptr ||
		capture_->sharedFence11 == nullptr) {
		return false;
	}

	try {
		auto frame = capture_->framePool.TryGetNextFrame();
		if (!frame) {
			return false;
		}

		for (;;) {
			auto newerFrame = capture_->framePool.TryGetNextFrame();
			if (!newerFrame) {
				break;
			}
			frame = std::move(newerFrame);
		}

		const auto contentSize = frame.ContentSize();
		if (contentSize.Width <= 0 || contentSize.Height <= 0) {
			return false;
		}
		if (contentSize.Width != capture_->itemSize.Width || contentSize.Height != capture_->itemSize.Height) {
			capture_->framePool.Recreate(
				capture_->winrtDevice,
				winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
				2,
				contentSize);
			capture_->itemSize = contentSize;
			return false;
		}

		winrt::com_ptr<::IInspectable> surfaceInspectable = frame.Surface().as<::IInspectable>();
		Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> dxgiAccess;
		if (FAILED(surfaceInspectable->QueryInterface(IID_PPV_ARGS(&dxgiAccess)))) {
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> frameTexture;
		if (FAILED(dxgiAccess->GetInterface(IID_PPV_ARGS(&frameTexture)))) {
			return false;
		}

		POINT clientOrigin{ 0, 0 };
		if (!ClientToScreen(gameWindow_, &clientOrigin)) {
			return false;
		}

		const int sourceLeft = clientOrigin.x - capture_->desktopRect.left;
		const int sourceTop = clientOrigin.y - capture_->desktopRect.top;
		const int sourceRight = sourceLeft + width_;
		const int sourceBottom = sourceTop + height_;
		if (sourceLeft < 0 || sourceTop < 0) {
			return false;
		}

		D3D11_TEXTURE2D_DESC frameDesc{};
		frameTexture->GetDesc(&frameDesc);
		if (sourceRight > static_cast<int>(frameDesc.Width) || sourceBottom > static_cast<int>(frameDesc.Height)) {
			return false;
		}

		D3D11_BOX sourceBox{};
		sourceBox.left = static_cast<UINT>(sourceLeft);
		sourceBox.top = static_cast<UINT>(sourceTop);
		sourceBox.front = 0;
		sourceBox.right = static_cast<UINT>(sourceRight);
		sourceBox.bottom = static_cast<UINT>(sourceBottom);
		sourceBox.back = 1;

		capture_->context11->CopySubresourceRegion(capture_->sharedTexture11.Get(), 0, 0, 0, 0, frameTexture.Get(), 0, &sourceBox);
		++capture_->latestFenceValue;
		if (FAILED(capture_->context11_4->Signal(capture_->sharedFence11.Get(), capture_->latestFenceValue))) {
			return false;
		}
		return true;
	} catch (const winrt::hresult_error&) {
		return false;
	}

	return false;
}

bool GpuColorDetectorDx12::Detect(const GpuDetectionParams& params, Vector2& targetOut) {
	if (!IsReady()) {
		return false;
	}

	const ScanBounds clamped{
		(std::max)(0, params.bounds.minX),
		(std::min)(width_, params.bounds.maxX),
		(std::max)(0, params.bounds.minY),
		(std::min)(height_, params.bounds.maxY)
	};

	const std::uint32_t writeIndex = currentFrameIndex_;
	const std::uint32_t readIndex = (currentFrameIndex_ + 1u) % kFramesInFlight;
	FrameContext& writeFrame = frames_[writeIndex];

	if (writeFrame.submittedFenceValue != 0 && fence_->GetCompletedValue() < writeFrame.submittedFenceValue) {
		return TryConsumeResult(frames_[readIndex], params, targetOut);
	}

	if (!CaptureFrameToTexture()) {
		return TryConsumeResult(frames_[readIndex], params, targetOut);
	}
	if (!WaitForCapturedFrame()) {
		return false;
	}

	if (FAILED(writeFrame.allocator->Reset())) {
		return false;
	}
	if (FAILED(commandList_->Reset(writeFrame.allocator.Get(), nullptr))) {
		return false;
	}

	ID3D12DescriptorHeap* heaps[] = { descriptorHeap_.Get() };
	commandList_->SetDescriptorHeaps(1, heaps);
	commandList_->SetComputeRootSignature(rootSignature_.Get());

	const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
	commandList_->ClearUnorderedAccessViewUint(
		GpuHandle(kUavWaveResults),
		CpuHandle(kUavWaveResults),
		waveResultsBuffer_.Get(),
		clearValues,
		0,
		nullptr);
	commandList_->ClearUnorderedAccessViewUint(
		GpuHandle(kUavStage2),
		CpuHandle(kUavStage2),
		stage2Buffer_.Get(),
		clearValues,
		0,
		nullptr);

	ScanConstants scan{};
	scan.width = static_cast<std::uint32_t>(width_);
	scan.height = static_cast<std::uint32_t>(height_);
	scan.minX = static_cast<std::uint32_t>(clamped.minX);
	scan.maxX = static_cast<std::uint32_t>(clamped.maxX);
	scan.minY = static_cast<std::uint32_t>(clamped.minY);
	scan.maxY = static_cast<std::uint32_t>(clamped.maxY);
	scan.centerX = static_cast<std::uint32_t>(params.centerX);
	scan.centerY = static_cast<std::uint32_t>(params.centerY);
	scan.groupsX = static_cast<std::uint32_t>(groupsX_);
	scan.wavesPerGroup = static_cast<std::uint32_t>(wavesPerGroup_);

	commandList_->SetPipelineState(scanPso_.Get());
	commandList_->SetComputeRoot32BitConstants(kRootParamConstants, 12, &scan, 0);
	commandList_->SetComputeRootDescriptorTable(kRootParamSrvTable, GpuHandle(kSrvCaptureTexture));
	commandList_->SetComputeRootDescriptorTable(kRootParamUavTable, GpuHandle(kUavWaveResults));
	commandList_->Dispatch(static_cast<UINT>(groupsX_), static_cast<UINT>(groupsY_), 1);

	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = waveResultsBuffer_.Get();
	commandList_->ResourceBarrier(1, &uavBarrier);

	Transition(commandList_.Get(), waveResultsBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	ReduceConstants reduce{};
	reduce.candidateCount = static_cast<std::uint32_t>(groupsCount_ * wavesPerGroup_);
	commandList_->SetPipelineState(reducePso_.Get());
	commandList_->SetComputeRoot32BitConstants(kRootParamConstants, 4, &reduce, 0);
	commandList_->SetComputeRootDescriptorTable(kRootParamSrvTable, GpuHandle(kSrvWaveResults));
	commandList_->SetComputeRootDescriptorTable(kRootParamUavTable, GpuHandle(kUavStage2));
	commandList_->Dispatch(1, 1, 1);

	uavBarrier.UAV.pResource = stage2Buffer_.Get();
	commandList_->ResourceBarrier(1, &uavBarrier);

	Transition(commandList_.Get(), stage2Buffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	FinalConstants finalConstants{};
	finalConstants.stageCount = static_cast<std::uint32_t>(wavesPerGroup_);
	commandList_->SetPipelineState(finalPso_.Get());
	commandList_->SetComputeRoot32BitConstants(kRootParamConstants, 4, &finalConstants, 0);
	commandList_->SetComputeRootDescriptorTable(kRootParamSrvTable, GpuHandle(kSrvStage2));
	commandList_->SetComputeRootDescriptorTable(kRootParamUavTable, GpuHandle(kUavFinalResult));
	commandList_->Dispatch(1, 1, 1);

	uavBarrier.UAV.pResource = finalResultBuffer_.Get();
	commandList_->ResourceBarrier(1, &uavBarrier);

	Transition(commandList_.Get(), finalResultBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commandList_->CopyBufferRegion(writeFrame.readbackBuffer.Get(), 0, finalResultBuffer_.Get(), 0, sizeof(GroupResult));
	Transition(commandList_.Get(), finalResultBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	Transition(commandList_.Get(), waveResultsBuffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	Transition(commandList_.Get(), stage2Buffer_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	if (!SubmitAndSignal(writeFrame)) {
		return false;
	}

	currentFrameIndex_ = (currentFrameIndex_ + 1u) % kFramesInFlight;
	return TryConsumeResult(frames_[currentFrameIndex_], params, targetOut);
}

void GpuColorDetectorDx12::CloseFenceEvent() noexcept {
	if (fenceEvent_ != nullptr) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
}

void GpuColorDetectorDx12::CloseCaptureSharedHandles() noexcept {
	if (capture_ == nullptr) {
		return;
	}

	if (capture_->sharedTextureHandle != nullptr) {
		CloseHandle(capture_->sharedTextureHandle);
		capture_->sharedTextureHandle = nullptr;
	}
	if (capture_->sharedFenceHandle != nullptr) {
		CloseHandle(capture_->sharedFenceHandle);
		capture_->sharedFenceHandle = nullptr;
	}
}

GpuColorDetectorDx12::~GpuColorDetectorDx12() {
	if (capture_ != nullptr) {
		capture_->session = nullptr;
		capture_->framePool = nullptr;
		capture_->item = nullptr;
		capture_->winrtDevice = nullptr;
	}

	if (fence_ != nullptr && queue_ != nullptr && nextFenceValue_ > 0) {
		const std::uint64_t signalValue = nextFenceValue_;
		if (SUCCEEDED(queue_->Signal(fence_.Get(), signalValue))) {
			if (fence_->GetCompletedValue() < signalValue && fenceEvent_ != nullptr) {
				if (SUCCEEDED(fence_->SetEventOnCompletion(signalValue, fenceEvent_))) {
					WaitForSingleObject(fenceEvent_, INFINITE);
				}
			}
		}
	}

	for (auto& frame : frames_) {
		if (frame.readbackBuffer != nullptr) {
			frame.readbackBuffer->Unmap(0, nullptr);
			frame.mappedReadback = nullptr;
		}
	}

	CloseCaptureSharedHandles();
	if (capture_ != nullptr && capture_->shouldRoUninitialize) {
		RoUninitialize();
	}
	delete capture_;
	capture_ = nullptr;
	CloseFenceEvent();
}

} // namespace colourbot
