//Shared at https://www.unknowncheats.me/forum/valorant/444573-perfect-valorant-colorbot.html

#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include "resource.h"
#include <WinUser.h>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <stdlib.h>
#include <fstream>
#include <chrono> // for high_resolution_clock
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <atomic>
#include <filesystem>
#include <wrl/client.h>
#include <thread>
#include "interception.h"
#include <string>
#include "DetectionCommon.h"
#include "CpuDetector.h"
#include "GpuColorDetectorDx12.h"
#include "ConfigIO.h"

#pragma comment(lib,"d3d11.lib")
using namespace std;

//#define PROCESS_NAME L"VALORANT  " 
#define PROCESS_NAME L"Untitled - Paint" 

#define DEBUGDIR L"Test"

using Microsoft::WRL::ComPtr;

ComPtr<ID3D11Device> lDevice;
ComPtr<ID3D11DeviceContext> lImmediateContext;
D3D11_TEXTURE2D_DESC desc;

D3D_DRIVER_TYPE gDriverTypes[] = {
	D3D_DRIVER_TYPE_HARDWARE
};
UINT gNumDriverTypes = ARRAYSIZE(gDriverTypes);

// Feature levels supported
D3D_FEATURE_LEVEL gFeatureLevels[] = {
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
	D3D_FEATURE_LEVEL_9_1
};
UINT gNumFeatureLevels = ARRAYSIZE(gFeatureLevels);


using colourbot::kPixelStride;
using colourbot::MakeBounds;
using colourbot::ScanBounds;
using colourbot::Vector2;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kDegToRad = kPi / 180.0;

bool flickAim = false;
bool recoilControl = false;
bool overloadManualInputs = false;
bool isDebugging = false;
int flickAimTime = 20;
int checkingRangeSingleTarget = 20;
float speed = 0.2f;
int maxX = 600;
int maxY = 300;
//inline float recoilms = 0.2;
bool recoil = false;

int trueX = 0;
int trueY = 0;

int onTargetLockX = 50;
int onTargetLockY = 50;

int full360 = 0;//6429;	
int holdKeyIndex = 0;
int holdKey = VK_MENU;
bool isHold = false;
bool invertHold = false;
bool testFull360 = false;
bool modeSwitchingEnable = false;
int offset[2] = {
	0,5
};

int recoilOffset = 0;
bool recoilControlStart = false;

bool isZoomed = false;
constexpr int kHoldKeyCount = 15;
static const char* holdKeys[kHoldKeyCount]{
   "Left mouse button",
   "Right mouse button",
   "Middle mouse button",
   "TAB key",
   "SHIFT key",
   "CTRL key",
   "ALT key",
   "DEL key",
   "INS key",
   "Numeric keypad 0 key",
   "NUM LOCK key",
   "Left SHIFT key",
   "Right SHIFT key",
   "Left CONTROL key",
   "Right CONTROL key",
};

static const int holdKeysCodes[kHoldKeyCount]{
   VK_LBUTTON,
   VK_RBUTTON,
   VK_MBUTTON,
   VK_TAB,
   VK_SHIFT,
   VK_CONTROL,
   VK_MENU,
   VK_DELETE,
   VK_INSERT,
   VK_NUMPAD0,
   VK_NUMLOCK,
   VK_LSHIFT,
   VK_RSHIFT,
   VK_LCONTROL,
   VK_RCONTROL,
};

// Data
static LPDIRECT3D9              g_pD3D = NULL;
static LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

uint32_t width;
uint32_t height;
ComPtr<IDXGISurface1> gdiSurface;
ComPtr<ID3D11Texture2D> texture;
ComPtr<ID3D11Texture2D> frameCopyTexture;
ComPtr<IDXGIOutputDuplication> desktopDuplication;
HWND game_window;
std::atomic<bool> requestDebugCapture{ false };
std::atomic<unsigned long long> captureCounter{ 0ULL };
RECT duplicationDesktopRect{ 0, 0, 0, 0 };

colourbot::CpuDetector cpuDetector;
colourbot::GpuColorDetectorDx12 gpuDetectorDx12;
bool gpuDetectorDx12Available = false;
bool desktopDuplicationAvailable = false;

InterceptionContext context;
InterceptionDevice device;
InterceptionStroke stroke;

static void NormalMouse() {
	while (interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
		if (interception_is_mouse(device))
		{
			InterceptionMouseStroke& mstroke = *(InterceptionMouseStroke*)&stroke;
			interception_send(context, device, &stroke, 1);
		}
	}
}

static void InitMoveMouse() {
	cout << "Loading Interception..." << endl;

	context = interception_create_context();
	interception_set_filter(context, interception_is_mouse, INTERCEPTION_FILTER_MOUSE_MOVE);
	device = interception_wait(context);

	while (interception_receive(context, device = interception_wait(context), &stroke, 1) > 0) {
		if (interception_is_mouse(device))
		{
			InterceptionMouseStroke& mstroke = *(InterceptionMouseStroke*)&stroke;
			interception_send(context, device, &stroke, 1);
			break;
		}
	}
	cout << "Interception Loaded" << endl;
	thread normal(NormalMouse);
	normal.detach();
}

static void MoveMouse(int dx, int dy) {
	InterceptionMouseStroke& mstroke = *(InterceptionMouseStroke*)&stroke;
	mstroke.flags = 0;
	mstroke.information = 0;
	mstroke.x = dx + offset[0];
	mstroke.y = dy + offset[1] + recoilOffset;
	if (isDebugging)
		//logging::INFO("Cords: " + dx + ',' + dy);
		cout << "Cords: x-" << dx << "y-" << dy << endl;
	interception_send(context, device, &stroke, 1);
}

using PixelBuffer = const std::uint8_t*;
enum class SortingMethod {
	CustomPriority = 0,
	FirstColor = 1,
	SingleTargetPriority = 2,
	GpuFast = 3
};
SortingMethod currentSortingMethod = SortingMethod::CustomPriority;

static void SetIsZoomed() { // CALL THIS EVERY FRAME
	isZoomed = GetAsyncKeyState(VK_RBUTTON);
}

static int Full360() {
	return isZoomed ? full360 : (full360 * 8 / 10);
}

static int GetCoordsX(int delta, int total) {

	double lookAt = delta * 2.0 / total;
	double degrees = atan(lookAt * tan((isZoomed ? 41.5 : 52.0) * kDegToRad)) * kRadToDeg;
	return static_cast<int>((Full360() * degrees) / 360);
}

static int GetCoordsY(int delta, int total) {

	double lookAt = delta * 2.0 / total;
	double degrees = atan(lookAt * tan((isZoomed ? 26.5 : 36) * kDegToRad)) * kRadToDeg;
	return static_cast<int>((Full360() * degrees) / 360);
}

std::chrono::high_resolution_clock::time_point timerStart;
long long timeDiff;

static void MoveMouseFromScreenPosition(Vector2 front, int height, int width) {
	SetIsZoomed();

	if (overloadManualInputs && GetAsyncKeyState(VK_LBUTTON))
	{
		return;
	}

	int moveX = GetCoordsX(front.x, width);
	int moveY = GetCoordsY(front.y, height);

	if (recoilControl && GetAsyncKeyState(VK_LBUTTON))
	{
		if (!recoilControlStart)
		{
			recoilControlStart = true;
			timerStart = std::chrono::high_resolution_clock::now();
			timeDiff = 0;
			recoilOffset = 0;
		}
		else if (recoilOffset < 7)
		{
			auto end = std::chrono::high_resolution_clock::now();
			timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(end - timerStart).count();
			if (timeDiff > 150)
			{
				recoilOffset = static_cast<int>((timeDiff - 150) / 50);

				if (recoilOffset > 6)
				{
					return;
				}

				if (abs(moveX) < 2)
					moveX = 0;

				if (abs(moveY) < 2)
					moveY = 0;
			}
			if (isDebugging)
				//logging::INFO( "timeDiff: " + timeDiff + ' ' + recoilOffset);
				cout << "Time: " << timeDiff << "ms " << recoilOffset << endl;
			//timerStart = std::chrono::high_resolution_clock::now();
		}
		else
		{
			return;
		}
	}
	else
	{
		recoilControlStart = false;
		recoilOffset = 0;
	}


	if (flickAim) {
		MoveMouse(moveX, moveY);
		Sleep(flickAimTime);

		trueX = onTargetLockX;
		trueY = onTargetLockY;
	}
	else {
		MoveMouse(static_cast<int>(std::lround(moveX * speed)), static_cast<int>(std::lround(moveY * speed)));
	}
}

static bool RunCpuSorting(PixelBuffer data, int height, int width, int rowPitch) {
	Vector2 target;
	bool found = false;
	switch (currentSortingMethod) {
	case SortingMethod::CustomPriority:
		found = cpuDetector.CustomPriority(data, height, width, rowPitch, trueX, trueY, target);
		break;
	case SortingMethod::FirstColor:
		found = cpuDetector.FirstColor(data, height, width, rowPitch, trueX, trueY, target);
		break;
	case SortingMethod::SingleTargetPriority:
		found = cpuDetector.SingleTargetPriority(data, height, width, rowPitch, trueX, trueY, checkingRangeSingleTarget, speed, target);
		break;
	case SortingMethod::GpuFast:
	default:
		found = false;
		break;
	}

	if (found) {
		MoveMouseFromScreenPosition(target, height, width);
	}
	return found;
}

static const char* SortingMethodToString(SortingMethod method) noexcept {
	switch (method) {
	case SortingMethod::CustomPriority:
		return "Priority";
	case SortingMethod::FirstColor:
		return "FirstColor";
	case SortingMethod::SingleTargetPriority:
		return "SingleTarget";
	case SortingMethod::GpuFast:
		return "GpuFast";
	default:
		return "Unknown";
	}
}

// Debug capture utilities write a binary PPM in Test/ for fast, dependency-free output.
static std::string BuildCapturePath() {
	namespace fs = std::filesystem;
	fs::create_directories(DEBUGDIR);

	const auto now = std::chrono::system_clock::now();
	const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
	std::tm localTime{};
	localtime_s(&localTime, &timestamp);

	char timeBuffer[32]{};
	std::strftime(timeBuffer, sizeof(timeBuffer), "%Y%m%d_%H%M%S", &localTime);
	const auto id = captureCounter.fetch_add(1, std::memory_order_relaxed);
	fs::path outPath = fs::path(DEBUGDIR) / ("screengrab_" + std::string(timeBuffer) + "_" + std::to_string(id) + ".ppm");
	return outPath.string();
}

static bool WriteCaptureToPpm(PixelBuffer data, int height, int width, int rowPitch, const std::string& filePath) {
	std::ofstream output(filePath, std::ios::binary);
	if (!output.is_open()) {
		return false;
	}

	output << "P6\n" << width << " " << height << "\n255\n";
	std::vector<std::uint8_t> rgbRow(static_cast<size_t>(width) * 3U);
	for (int y = 0; y < height; ++y) {
		const auto* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(rowPitch));
		for (int x = 0; x < width; ++x) {
			const size_t outBase = static_cast<size_t>(x) * 3U;
			rgbRow[outBase + 0] = pixel[2];
			rgbRow[outBase + 1] = pixel[1];
			rgbRow[outBase + 2] = pixel[0];
			pixel += kPixelStride;
		}
		output.write(reinterpret_cast<const char*>(rgbRow.data()), static_cast<std::streamsize>(rgbRow.size()));
	}
	return static_cast<bool>(output);
}

static void TrySaveCapture(PixelBuffer data, int height, int width, int rowPitch) {
	if (!requestDebugCapture.exchange(false, std::memory_order_relaxed)) {
		return;
	}

	const std::string capturePath = BuildCapturePath();
	if (WriteCaptureToPpm(data, height, width, rowPitch, capturePath)) {
		cout << "Saved capture to: " << capturePath << endl;
	}
	else {
		cout << "Failed to save capture: " << capturePath << endl;
	}
}

static bool InitDesktopDuplication() {
	desktopDuplication.Reset();
	desktopDuplicationAvailable = false;

	ComPtr<IDXGIDevice> dxgiDevice;
	if (FAILED(lDevice.As(&dxgiDevice))) {
		return false;
	}

	ComPtr<IDXGIAdapter> adapter;
	if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
		return false;
	}

	ComPtr<IDXGIOutput> output;
	if (FAILED(adapter->EnumOutputs(0, &output))) {
		return false;
	}

	DXGI_OUTPUT_DESC outputDesc{};
	if (FAILED(output->GetDesc(&outputDesc))) {
		return false;
	}
	duplicationDesktopRect = outputDesc.DesktopCoordinates;

	ComPtr<IDXGIOutput1> output1;
	if (FAILED(output.As(&output1))) {
		return false;
	}

	HRESULT duplicateResult = output1->DuplicateOutput(lDevice.Get(), &desktopDuplication);
	if (FAILED(duplicateResult)) {
		return false;
	}

	desktopDuplicationAvailable = true;
	return true;
}

static bool CaptureFrameWithDesktopDuplication() {
	if (!desktopDuplicationAvailable || desktopDuplication == nullptr) {
		return false;
	}

	DXGI_OUTDUPL_FRAME_INFO frameInfo{};
	ComPtr<IDXGIResource> frameResource;
	const HRESULT acquireResult = desktopDuplication->AcquireNextFrame(0, &frameInfo, &frameResource);
	if (acquireResult == DXGI_ERROR_WAIT_TIMEOUT) {
		return false;
	}
	if (FAILED(acquireResult)) {
		desktopDuplicationAvailable = false;
		desktopDuplication.Reset();
		return false;
	}

	ComPtr<ID3D11Texture2D> desktopFrame;
	if (FAILED(frameResource.As(&desktopFrame))) {
		desktopDuplication->ReleaseFrame();
		return false;
	}

	POINT clientOrigin{ 0, 0 };
	if (!ClientToScreen(game_window, &clientOrigin)) {
		desktopDuplication->ReleaseFrame();
		return false;
	}

	const int sourceLeft = clientOrigin.x - duplicationDesktopRect.left;
	const int sourceTop = clientOrigin.y - duplicationDesktopRect.top;
	const int sourceRight = sourceLeft + static_cast<int>(width);
	const int sourceBottom = sourceTop + static_cast<int>(height);
	if (sourceLeft < 0 || sourceTop < 0) {
		desktopDuplication->ReleaseFrame();
		return false;
	}

	D3D11_TEXTURE2D_DESC desktopDesc{};
	desktopFrame->GetDesc(&desktopDesc);
	if (sourceRight > static_cast<int>(desktopDesc.Width) || sourceBottom > static_cast<int>(desktopDesc.Height)) {
		desktopDuplication->ReleaseFrame();
		return false;
	}

	D3D11_BOX sourceBox{};
	sourceBox.left = static_cast<UINT>(sourceLeft);
	sourceBox.top = static_cast<UINT>(sourceTop);
	sourceBox.front = 0;
	sourceBox.right = static_cast<UINT>(sourceRight);
	sourceBox.bottom = static_cast<UINT>(sourceBottom);
	sourceBox.back = 1;

	lImmediateContext->CopySubresourceRegion(texture.Get(), 0, 0, 0, 0, desktopFrame.Get(), 0, &sourceBox);
	desktopDuplication->ReleaseFrame();
	return true;
}

static bool InitColor() {
	// ==== FIND WINDOW ==== 
	RECT rect;
	game_window = FindWindowW(NULL, PROCESS_NAME);
	GetClientRect(game_window, &rect);

	// ==== SCALING FACTOR ====
	HDC monitor = GetDC(game_window); // GetDC(NULL);

	int current = GetDeviceCaps(monitor, VERTRES);
	int total = GetDeviceCaps(monitor, DESKTOPVERTRES);

	width = (rect.right - rect.left) * total / current;
	height = (rect.bottom - rect.top) * total / current;

	// ==== CREATE DEVICE ==== 

	HRESULT hr(E_FAIL);
	D3D_FEATURE_LEVEL lFeatureLevel;

	for (UINT DriverTypeIndex = 0; DriverTypeIndex < gNumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(
			nullptr,
			gDriverTypes[DriverTypeIndex],
			nullptr,
			0,
			gFeatureLevels,
			gNumFeatureLevels,
			D3D11_SDK_VERSION,
			&lDevice,
			&lFeatureLevel,
			&lImmediateContext);

		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}

		lDevice.Reset();

		lImmediateContext.Reset();
	}

	// ==== CREATE TEXTURE ====

	desc.Width = width;
	desc.Height = height;
	desc.ArraySize = 1;
	desc.MipLevels = 1;

	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	desc.Usage = D3D11_USAGE_DEFAULT;

	desc.BindFlags = 40;
	desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
	desc.CPUAccessFlags = 0;

	hr = lDevice->CreateTexture2D(&desc, NULL, &texture);

	if (FAILED(hr)) {
		cout << "Failed to create texture" << endl;
		return false;
	}

	hr = texture->QueryInterface(__uuidof(IDXGISurface1), (void**)&gdiSurface);

	if (FAILED(hr)) {
		cout << "Failed to create GDI surface" << endl;
		return false;
	}

	gpuDetectorDx12Available = gpuDetectorDx12.Initialize(static_cast<int>(width), static_cast<int>(height), game_window);
	if (!gpuDetectorDx12Available) {
		cout << "GPU DX12 detector unavailable, using CPU modes only" << endl;
	}
	if (!InitDesktopDuplication()) {
		cout << "Desktop Duplication unavailable, using GDI capture fallback" << endl;
	}

	// REUSE desc FOR FRAMECOPY
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.Usage = D3D11_USAGE_STAGING;

	hr = lDevice->CreateTexture2D(&desc, nullptr, &frameCopyTexture);
	if (FAILED(hr)) {
		cout << "Failed to create frame copy texture" << endl;
		return false;
	}

	InitMoveMouse();
	cout << "Starting at " << width << "x" << height << endl;

	return true;
}
HDC hdc_target;

static bool ScreenGrab(bool runTargeting) {
	struct DetectionTimingState {
		SortingMethod method = SortingMethod::CustomPriority;
		bool usedGpuPath = false;
		int sampleCount = 0;
		double totalMs = 0.0;
		double minMs = 0.0;
		double maxMs = 0.0;
	};
	static DetectionTimingState timing{};
	const int frameWidth = static_cast<int>(desc.Width);
	const int frameHeight = static_cast<int>(desc.Height);
	const int halfWidth = frameWidth / 2;
	const int halfHeight = frameHeight / 2;
	const bool wantsGpuDetection =
		runTargeting &&
		currentSortingMethod == SortingMethod::GpuFast &&
		gpuDetectorDx12Available;
	const bool debugCaptureRequested = requestDebugCapture.load(std::memory_order_relaxed);
	const bool needsDx11FrameCapture =
		(!wantsGpuDetection) ||
		(!runTargeting) ||
		isDebugging ||
		debugCaptureRequested;

	// ==== SCEENGRAB ==== 

	if (needsDx11FrameCapture) {
		if (!CaptureFrameWithDesktopDuplication()) {
			HDC hDC = nullptr;
			if (FAILED(gdiSurface->GetDC(true, &hDC))) {
				return false;
			}
			hdc_target = GetDC(game_window);
			if (hdc_target == nullptr) {
				gdiSurface->ReleaseDC(nullptr);
				return false;
			}

			// === FALLBACK COPY VIA GDI ===
			while (!BitBlt(hDC, 0, 0, width, height, hdc_target, 0, 0, SRCCOPY)) {
				cout << "FAILED" << endl;
				Sleep(1000);
			}

			// VERY IMPORTANT TO RELEASE BEFORE COPY
			ReleaseDC(game_window, hdc_target);
			gdiSurface->ReleaseDC(nullptr);
		}
	}

	bool usedGpuDetection = false;
	bool targetFound = false;
	auto detectionStart = std::chrono::high_resolution_clock::now();

	if (wantsGpuDetection) {
		const auto bounds = MakeBounds(halfWidth, halfHeight, trueX, trueY, frameWidth, frameHeight);
		colourbot::GpuDetectionParams params{};
		params.bounds = bounds;
		params.centerX = halfWidth;
		params.centerY = halfHeight;

		Vector2 target{};
		if (gpuDetectorDx12.Detect(params, target)) {
			MoveMouseFromScreenPosition(target, frameHeight, frameWidth);
			targetFound = true;
		}
		usedGpuDetection = true;
	}

	const bool needsCpuMap = (runTargeting && !usedGpuDetection) ||
		isDebugging ||
		debugCaptureRequested;

	if (needsCpuMap) {
		D3D11_MAPPED_SUBRESOURCE mapped{};
		lImmediateContext->CopyResource(frameCopyTexture.Get(), texture.Get());
		const HRESULT mapResult = lImmediateContext->Map(frameCopyTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(mapResult)) {
			return false;
		}

		const auto* data = static_cast<const std::uint8_t*>(mapped.pData);
		const int rowPitch = static_cast<int>(mapped.RowPitch);

		if (runTargeting && !usedGpuDetection) {
			if (currentSortingMethod == SortingMethod::GpuFast) {
				Vector2 fallbackTarget{};
				targetFound = cpuDetector.CustomPriority(data, frameHeight, frameWidth, rowPitch, trueX, trueY, fallbackTarget);
				if (targetFound) {
					MoveMouseFromScreenPosition(fallbackTarget, frameHeight, frameWidth);
				}
			}
			else {
				targetFound = RunCpuSorting(data, frameHeight, frameWidth, rowPitch);
			}
		}

		TrySaveCapture(data, frameHeight, frameWidth, rowPitch);
		lImmediateContext->Unmap(frameCopyTexture.Get(), 0);
	}

	if (runTargeting && isDebugging) {
		const auto detectionEnd = std::chrono::high_resolution_clock::now();
		const double detectionMs = std::chrono::duration<double, std::milli>(detectionEnd - detectionStart).count();
		const bool usingGpuPath = usedGpuDetection;

		if (timing.sampleCount == 0 || timing.method != currentSortingMethod || timing.usedGpuPath != usingGpuPath) {
			timing.method = currentSortingMethod;
			timing.usedGpuPath = usingGpuPath;
			timing.sampleCount = 0;
			timing.totalMs = 0.0;
			timing.minMs = detectionMs;
			timing.maxMs = detectionMs;
		}

		++timing.sampleCount;
		timing.totalMs += detectionMs;
		timing.minMs = (std::min)(timing.minMs, detectionMs);
		timing.maxMs = (std::max)(timing.maxMs, detectionMs);

		constexpr int kPrintEverySamples = 30;
		if ((timing.sampleCount % kPrintEverySamples) == 0) {
			const double averageMs = timing.totalMs / timing.sampleCount;
			cout << "[DetectTiming] mode=" << SortingMethodToString(currentSortingMethod)
				<< " path=" << (usingGpuPath ? "GPU" : "CPU")
				<< " current_ms=" << detectionMs
				<< " avg_ms=" << averageMs
				<< " min_ms=" << timing.minMs
				<< " max_ms=" << timing.maxMs
				<< " found=" << (targetFound ? 1 : 0)
				<< " samples=" << timing.sampleCount
				<< endl;
		}
	}

	if (runTargeting && !targetFound && flickAim)
	{
		trueX = maxX;
		trueY = maxY;
	}

	return true;
}

bool isRunning = false;
bool isReallyRunning = false;

static void ScreenGrabMain() {
	while (true) {
		bool shouldRun = false;
		if (isRunning) {
			if (isHold) {
				shouldRun = (GetKeyState(holdKey) & 0x8000);
				if (invertHold) shouldRun = !shouldRun;
			}
			else {
				shouldRun = (GetKeyState(holdKey) == 1);
			}

			if (shouldRun) {
				if (testFull360) {
					MoveMouse(full360, 0);
					Sleep(1000);
				}
				else {
					ScreenGrab(true);
				}
			}
			else {
				Sleep(10);
				//if (flickAim)
				//{
				trueX = maxX;
				trueY = maxY;
				//}
				/*if (isDebugging)
					system("cls");*/
				cpuDetector.ResetSingleTarget();
			}
		}
		else {
			Sleep(100);
		}
		isReallyRunning = shouldRun;
	}
}

int sortingCounter = 0;
const char* currentSortingMethodName;
const char* currentSortingMethodDescript;
void UpdateSortingMethod(int id) {
	cpuDetector.ResetSingleTarget();
	switch (id % 4)
	{
	case 0:
		currentSortingMethod = SortingMethod::CustomPriority;
		currentSortingMethodName = "Priority Sorter";
		currentSortingMethodDescript = "A bit slower, but priorities heads";
		break;
	case 1:
		currentSortingMethod = SortingMethod::FirstColor;
		currentSortingMethodName = "First Color Sorter";
		currentSortingMethodDescript = "Fast, but no sorting";
		break;
	case 2:
		currentSortingMethod = SortingMethod::SingleTargetPriority;
		currentSortingMethodName = "Target Priority Sorter";
		currentSortingMethodDescript = "Locks on one";
		break;
	case 3:
		currentSortingMethod = SortingMethod::GpuFast;
		currentSortingMethodName = "GPU DX12 Wave";
		currentSortingMethodDescript = gpuDetectorDx12Available ? "SM6 wave-intrinsic compute path" : "DX12/SM6 unavailable, falls back to CPU";
		break;
	default:
		break;
	}
}

static bool ReadConfig() {
	colourbot::ConfigData config{};
	config.speed = speed;
	config.maxX = maxX;
	config.maxY = maxY;
	config.offset = { offset[0], offset[1] };
	config.flickAim = flickAim;
	config.flickAimTime = flickAimTime;
	config.full360 = full360;
	config.sortingCounter = sortingCounter;
	config.holdKey = holdKey;
	config.isHold = isHold;
	config.invertHold = invertHold;
	config.recoilControl = recoilControl;
	config.overloadManualInputs = overloadManualInputs;

	if (!colourbot::LoadConfigFile("config.txt", config)) {
		return false;
	}

	speed = config.speed;
	maxX = config.maxX;
	maxY = config.maxY;
	offset[0] = config.offset[0];
	offset[1] = config.offset[1];
	flickAim = config.flickAim;
	flickAimTime = config.flickAimTime;
	full360 = config.full360;
	sortingCounter = config.sortingCounter;
	holdKey = config.holdKey;
	isHold = config.isHold;
	invertHold = config.invertHold;
	recoilControl = config.recoilControl;
	overloadManualInputs = config.overloadManualInputs;
	return true;
}

static void SaveConfig() {
	colourbot::ConfigData config{};
	config.speed = speed;
	config.maxX = maxX;
	config.maxY = maxY;
	config.offset = { offset[0], offset[1] };
	config.flickAim = flickAim;
	config.flickAimTime = flickAimTime;
	config.full360 = full360;
	config.sortingCounter = sortingCounter;
	config.holdKey = holdKey;
	config.isHold = isHold;
	config.invertHold = invertHold;
	config.recoilControl = recoilControl;
	config.overloadManualInputs = overloadManualInputs;

	if (!colourbot::SaveConfigFile("config.txt", config, holdKeyIndex, holdKeysCodes, kHoldKeyCount)) {
		cout << "Failed to save config" << endl;
		return;
	}
	cout << "Saved config" << endl;
}

// Main code
int main(int, char**)
{
	//logging::INFO("Start of application");
	cout << "Fetching Config..." << endl;
	if (!ReadConfig()) {
		cout << "Failed to read config" << endl;
	}
	else {
		cout << "Loaded Config" << endl;
	}

	if (!InitColor()) {
		cin.get();
		return -1;
	}
	thread t1(ScreenGrabMain);
	t1.detach();

	//create Test directory if not exists
	CreateDirectory(DEBUGDIR, NULL);

	UpdateSortingMethod(sortingCounter);

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("ImGui Example"), NULL };
	::RegisterClassEx(&wc);

	HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("LABADABA dub dub's"), WS_OVERLAPPEDWINDOW, 0, 0, 400, 500, NULL, NULL, wc.hInstance, NULL);


	HICON hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(MAINICON));

	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX9_Init(g_pd3dDevice);

	ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.10f, 1.00f);

	holdKeyIndex = -1;
	for (int i = 0; i < kHoldKeyCount; ++i)
	{
		if (holdKeysCodes[i] == holdKey) {
			holdKeyIndex = i;
			break;
		}
	}

	// Main loop
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

		{

			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(400, 580), 0);
			ImGui::Begin("Settings", 0, flags);

			ImGui::Text("Setting");

			if (ImGui::Button(testFull360 ? "Stop Full360" : "Start Full360")) {
				testFull360 = !testFull360;
			}
			if (testFull360) {
				ImGui::InputInt("Full360", &full360);

				if (holdKeyIndex > 0) {
					ImGui::Text("Testing Full360, you most scope with vandal\nand configure so it turns 360 degrees\nwhen pressing [%s]", holdKeys[holdKeyIndex]);
				}
				else {
					ImGui::Text("Testing Full360, you most scope with vandal\nand configure so it turns 360 degrees");
				}
			}
			else {
				ImGui::SliderFloat("Speed", &speed, 0.0f, 1.0f);
				//ImGui::InputFloat("Speed", &speed, 0.0f, 1.0f);
				ImGui::SliderInt("FovX", &maxX, 50, width / 2);
				ImGui::SliderInt("FovY", &maxY, 50, height / 2);
				ImGui::InputInt2("Offset XY", offset);
				ImGui::InputInt("Full360", &full360);
				ImGui::Text("Flick Aimbot");
				ImGui::Checkbox("Flick", &flickAim);
				ImGui::InputInt("Flick Update ms", &flickAimTime);
				ImGui::InputInt("Range to update", &checkingRangeSingleTarget);
				ImGui::Checkbox("Recoil Control", &recoilControl);
				//ImGui::Text("Recol New Method");
				ImGui::Checkbox("Recoil New Method", &recoil);
				ImGui::Checkbox("Overload Manual Inputs", &overloadManualInputs);
				ImGui::Checkbox("Mode Switch", &modeSwitchingEnable);

				if (isRunning && modeSwitchingEnable)
				{
					if (GetKeyState(VK_MENU) == 1)
					{
						if (!flickAim)
							Beep(523, 200);
						flickAim = true;
					}
					else
					{
						if (flickAim)
						{
							Beep(223, 100);
							Beep(223, 100);
						}
						flickAim = false;
					}
					Sleep(20);
				}

				if (flickAimTime < 0) {
					flickAimTime = 0;
				}
			}

			ImGui::Text("Input Settings");
			ImGui::Checkbox("Hold", &isHold);
			/*if(isHold) {
				ImGui::Checkbox("Invert Hold", &invertHold);
			}*/

			if (holdKeyIndex > 0) {
				ImGui::Combo(isHold ? "Hold key" : "Toggle Key", &holdKeyIndex, holdKeys, kHoldKeyCount);
				holdKey = holdKeysCodes[holdKeyIndex];
			}
			else {
				ImGui::TextColored(ImVec4(0.4f, 0, 1, 1), "Custom key used: 0x%llX", holdKey);
			}

			if (!testFull360) {
				ImGui::Text("Sorting Method");

				if (ImGui::Button(currentSortingMethodName)) {
					sortingCounter++;
					UpdateSortingMethod(sortingCounter);
				}
				ImGui::SameLine();
				ImGui::Text(currentSortingMethodDescript);
			}

			if (ImGui::Button("Save Config")) {
				SaveConfig();
			}
			ImGui::SameLine();
			if (ImGui::Button("Capture ScreenGrab")) {
				requestDebugCapture.store(true, std::memory_order_relaxed);
				if (!isRunning) {
					if (!ScreenGrab(false)) {
						requestDebugCapture.store(false, std::memory_order_relaxed);
						cout << "Failed to capture frame. Ensure the target window is available." << endl;
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(isRunning ? "Stop colorbot" : "Start colorbot")) {
				isRunning = !isRunning;
			}
			if (isRunning && isReallyRunning) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.4f, 0, 1, 1), "Running");
			}
			if (full360 <= 0) {
				ImGui::Text("TO USE THIS COLORBOT \nFULL360 MUST BE CONFIGURED CORRECTLY\nTHIS IS DIFFERENT FOR ALL COMPUTERS\n\nTHE OPTIMAL SPEED I FOUND OUT TO BE AROUND 0.2\nSO IMO, ONLY CHANGE FULL360\n(FULL360 SHOULD BE AROUND 5000-25000)");
			}
			ImGui::Checkbox("Debug", &isDebugging);

			if (isDebugging)
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

			ImGui::End();
		}

		// Rendering
		ImGui::EndFrame();
		g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
		g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
		if (g_pd3dDevice->BeginScene() >= 0)
		{
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			g_pd3dDevice->EndScene();
		}
		HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

		// Handle loss of D3D9 device
		if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
			ResetDevice();
	}

	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClass(wc.lpszClassName, wc.hInstance);

	//logging::INFO("Exit....");
	return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
	if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
		return false;

	// Create the D3DDevice
	ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
	g_d3dpp.Windowed = TRUE;
	g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
	g_d3dpp.EnableAutoDepthStencil = TRUE;
	g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
	//g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
	if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
		return false;

	return true;
}

void CleanupDeviceD3D()
{
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
	if (g_pD3D) { g_pD3D->Release(); g_pD3D = NULL; }
}

void ResetDevice()
{
	ImGui_ImplDX9_InvalidateDeviceObjects();
	HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
	if (hr == D3DERR_INVALIDCALL)
		IM_ASSERT(0);
	ImGui_ImplDX9_CreateDeviceObjects();
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			g_d3dpp.BackBufferWidth = LOWORD(lParam);
			g_d3dpp.BackBufferHeight = HIWORD(lParam);
			ResetDevice();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
