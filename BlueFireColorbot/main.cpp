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
#include <charconv>
#include <vector>
#include <atomic>
#include <filesystem>
#include <string_view>
#include <wrl/client.h>
#include <thread>
#include "interception.h"
#include <string>

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


constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kDegToRad = kPi / 180.0;
constexpr int kPixelStride = 4;

struct Vector2 {
	int x;
	int y;
	constexpr Vector2(int X, int Y) noexcept : x(X), y(Y) {}
	[[nodiscard]] constexpr int LenSq() const noexcept {
		return (x * x) + (y * y);
	}
	[[nodiscard]] constexpr Vector2 operator+(const Vector2& a) const noexcept
	{
		return Vector2(a.x + x, a.y + y);
	}
};

bool flickAim = false;
bool recoilControl = false;
bool overloadManualInputs = false;
bool isDebugging = false;
bool NotfirstRunSingleTarget = false;
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
HWND game_window;
std::atomic<bool> requestDebugCapture{ false };
std::atomic<unsigned long long> captureCounter{ 0ULL };

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
typedef bool(*ColorSortingMethod)(PixelBuffer, int, int, int);
ColorSortingMethod currentSortingMethod;

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

static bool IsPurpleColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
	// updated PURPLE FROM https://www.unknowncheats.me/forum/valorant/437368-updated-colors-pixel-bot-act-4-a.html
	if (green >= 170) {
		return false;
	}

	if (green >= 120) {
		return std::abs(int(red) - int(blue)) <= 8 &&
			red - green >= 50 &&
			blue - green >= 50 &&
			red >= 105 &&
			blue >= 105;
	}

	return std::abs(int(red) - int(blue)) <= 13 &&
		red - green >= 60 &&
		blue - green >= 60 &&
		red >= 110 &&
		blue >= 100;
}

// Bounds helpers keep all pixel scans clipped to the mapped frame.
struct ScanBounds {
	int minX;
	int maxX;
	int minY;
	int maxY;
};

static ScanBounds MakeBounds(int centerX, int centerY, int rangeX, int rangeY, int width, int height) {
	const int boundedRangeX = (std::max)(0, rangeX);
	const int boundedRangeY = (std::max)(0, rangeY);
	return {
		(std::max)(0, centerX - boundedRangeX),
		(std::min)(width, centerX + boundedRangeX),
		(std::max)(0, centerY - boundedRangeY),
		(std::min)(height, centerY + boundedRangeY)
	};
}

static void CollectPurpleCandidates(PixelBuffer data, int rowPitch, int halfWidth, int halfHeight, const ScanBounds& bounds, std::vector<Vector2>& out) {
	for (int y = bounds.minY; y < bounds.maxY; ++y) {
		const auto* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(rowPitch)) + (static_cast<size_t>(bounds.minX) * kPixelStride);
		for (int x = bounds.minX; x < bounds.maxX; ++x) {
			const std::uint8_t blue = pixel[0];
			const std::uint8_t green = pixel[1];
			const std::uint8_t red = pixel[2];
			if (IsPurpleColor(red, green, blue)) {
				out.emplace_back(x - halfWidth, y - halfHeight);
			}
			pixel += kPixelStride;
		}
	}
}

static bool PickPriorityTarget(std::vector<Vector2>& candidates, Vector2& targetOut) {
	constexpr int maxCount = 5;
	constexpr int forSize = 100;
	constexpr int forSizeSq = forSize * forSize;

	if (candidates.empty()) {
		return false;
	}

	std::sort(candidates.begin(), candidates.end(), [](const Vector2& lhs, const Vector2& rhs) {
		return lhs.y < rhs.y;
	});

	std::vector<Vector2> forbidden;
	forbidden.reserve(maxCount + 1);
	for (const auto& current : candidates) {
		bool canUpdate = true;
		if (std::abs(current.x) > trueX || std::abs(current.y) > trueY) {
			continue;
		}
		for (const auto& forb : forbidden) {
			const auto sum = current + forb;
			if (sum.LenSq() < forSizeSq || std::abs(current.x + forb.x) < forSize) {
				canUpdate = false;
				break;
			}
		}
		if (canUpdate) {
			forbidden.push_back(current);
			if (static_cast<int>(forbidden.size()) > maxCount) {
				break;
			}
		}
	}

	if (forbidden.empty()) {
		return false;
	}

	const auto bestIt = std::min_element(forbidden.begin(), forbidden.end(), [](const Vector2& lhs, const Vector2& rhs) {
		return lhs.LenSq() < rhs.LenSq();
	});
	targetOut = *bestIt;
	return true;
}

static bool FirstColorSorting(PixelBuffer data, int height, int width, int rowPitch) {
	const int hWidth = width / 2;
	const int hHeight = height / 2;
	const auto bounds = MakeBounds(hWidth, hHeight, trueX, trueY, width, height);

	for (int y = bounds.minY; y < bounds.maxY; ++y) {
		const auto* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(rowPitch)) + (static_cast<size_t>(bounds.minX) * kPixelStride);
		for (int x = bounds.minX; x < bounds.maxX; ++x) {
			const std::uint8_t blue = pixel[0];
			const std::uint8_t green = pixel[1];
			const std::uint8_t red = pixel[2];
			if (IsPurpleColor(red, green, blue)) {
				MoveMouseFromScreenPosition(Vector2(x - hWidth, y - hHeight), height, width);
				return true;
			}
			pixel += kPixelStride;
		}
	}
	return false;
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

static bool isZoomedFunc() {
	return (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
}

static Vector2 FindXhair(PixelBuffer data, int height, int width, int rowPitch) {
	const int hWidth = width / 2;
	const int hHeight = height / 2;
	const Vector2 center(hWidth, hHeight);
	if (!isZoomedFunc())
		return center;

	const auto bounds = MakeBounds(hWidth, hHeight, maxX, maxY, width, height);
	for (int y = bounds.minY; y < bounds.maxY; ++y) {
		const auto* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(rowPitch)) + (static_cast<size_t>(bounds.minX) * kPixelStride);
		for (int x = bounds.minX; x < bounds.maxX; ++x) {
			const std::uint8_t blue = pixel[0];
			const std::uint8_t green = pixel[1];
			const std::uint8_t red = pixel[2];
			if (red == 0 && blue == 255 && green == 255) { // cyan
				return Vector2(x, y);
			}
			pixel += kPixelStride;
		}
	}
	return center;
}

Vector2 SaveLastLocation = Vector2(0, 0);

static bool SingleTargetPrioritySorting(PixelBuffer data, int height, int width, int rowPitch) {
	thread_local std::vector<Vector2> vects;
	if (vects.capacity() < 4096) {
		vects.reserve(4096);
	}
	vects.clear();

	const int hWidth = width / 2;
	const int hHeight = height / 2;

	if (!NotfirstRunSingleTarget) {
		const auto bounds = MakeBounds(hWidth, hHeight, trueX, trueY, width, height);
		CollectPurpleCandidates(data, rowPitch, hWidth, hHeight, bounds, vects);
	}
	else {
		const int lastX = static_cast<int>((SaveLastLocation.x * (1.0f - speed)) + hWidth);
		const int lastY = static_cast<int>((SaveLastLocation.y * (1.0f - speed)) + hHeight);
		const auto bounds = MakeBounds(lastX, lastY, checkingRangeSingleTarget, checkingRangeSingleTarget, width, height);
		CollectPurpleCandidates(data, rowPitch, hWidth, hHeight, bounds, vects);
	}

	Vector2 selected(0, 0);
	if (!PickPriorityTarget(vects, selected)) {
		return false;
	}

	SaveLastLocation = selected;
	MoveMouseFromScreenPosition(SaveLastLocation, height, width);
	NotfirstRunSingleTarget = true;
	return true;
}

static bool CustomPrioritySorting(PixelBuffer data, int height, int width, int rowPitch) {
	thread_local std::vector<Vector2> vects;
	if (vects.capacity() < 4096) {
		vects.reserve(4096);
	}
	vects.clear();

	const int hWidth = width / 2;
	const int hHeight = height / 2;
	const auto bounds = MakeBounds(hWidth, hHeight, trueX, trueY, width, height);
	CollectPurpleCandidates(data, rowPitch, hWidth, hHeight, bounds, vects);

	Vector2 selected(0, 0);
	if (!PickPriorityTarget(vects, selected)) {
		return false;
	}

	MoveMouseFromScreenPosition(selected, height, width);
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

	//For debugging 
	static std::uint8_t last_r = 0;
	static std::uint8_t last_g = 0;
	static std::uint8_t last_b = 0;
	std::chrono::high_resolution_clock::time_point start;
	if (isDebugging)
		start = std::chrono::high_resolution_clock::now();


	// ==== SCEENGRAB ==== 

	HDC hDC = nullptr;
	if (FAILED(gdiSurface->GetDC(true, &hDC))) {
		return false;
	}
	hdc_target = GetDC(game_window);
	if (hdc_target == nullptr) {
		gdiSurface->ReleaseDC(nullptr);
		return false;
	}

	// === THE COPY TEXTURE ===
	while (!BitBlt(hDC, 0, 0, width, height, hdc_target, 0, 0, SRCCOPY)) {
		cout << "FAILED" << endl;
		Sleep(1000);
	}

	// VERY IMPORTANT TO RELEASE BEFORE COPY
	ReleaseDC(game_window, hdc_target);
	gdiSurface->ReleaseDC(nullptr);

	// === COPY TO CPU ===
	D3D11_MAPPED_SUBRESOURCE tempsubsource{};
	lImmediateContext->CopyResource(frameCopyTexture.Get(), texture.Get());
	const HRESULT hr = lImmediateContext->Map(frameCopyTexture.Get(), 0, D3D11_MAP_READ, 0, &tempsubsource);
	if (FAILED(hr)) {
		return false;
	}
	const auto* data = static_cast<const std::uint8_t*>(tempsubsource.pData);
	const int rowPitch = static_cast<int>(tempsubsource.RowPitch);

	if (runTargeting && !currentSortingMethod(data, static_cast<int>(desc.Height), static_cast<int>(desc.Width), rowPitch) && flickAim)
	{
		trueX = maxX;
		trueY = maxY;
	}

	// TESTING FRAME UPDATE, REMOVE THIS
	if (isDebugging)
	{
		const int debugX = 100;
		const int debugY = 100;
		const int base = debugY * rowPitch + debugX * kPixelStride;
		const std::uint8_t red = data[base + 2];
		const std::uint8_t green = data[base + 1];
		const std::uint8_t blue = data[base];
		if ((last_b != blue || last_g != green || last_r != red) && (red > 0 && blue > 0 && green > 0)) {
			auto finish = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double> elapsed = finish - start;
			cout << "Time: " << (elapsed.count() * 1000) << "ms" << endl;
			//logging::INFO(  "Time: " + std::to_string((elapsed.count() * 1000)));
			start = std::chrono::high_resolution_clock::now();
			last_b = blue;
			last_g = green;
			last_r = red;
		}
	}
	TrySaveCapture(data, static_cast<int>(desc.Height), static_cast<int>(desc.Width), rowPitch);
	lImmediateContext->Unmap(frameCopyTexture.Get(), 0);
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
				NotfirstRunSingleTarget = false;
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
	switch (id % 3)
	{
	case 0:
		currentSortingMethod = CustomPrioritySorting;
		currentSortingMethodName = "Priority Sorter";
		currentSortingMethodDescript = "A bit slower, but priorities heads";
		break;
	case 1:
		currentSortingMethod = FirstColorSorting;
		currentSortingMethodName = "First Color Sorter";
		currentSortingMethodDescript = "Fast, but no sorting";
		break;
	case 2:
		currentSortingMethod = SingleTargetPrioritySorting;
		currentSortingMethodName = "Target Priority Sorter";
		currentSortingMethodDescript = "Locks on one";
		break;
	default:
		break;
	}
}

// Config parsing/writing uses typed conversions to avoid C-style atof behavior.
static std::string RemoveWhitespace(std::string_view source) {
	std::string result;
	result.reserve(source.size());
	for (const char ch : source) {
		if (!std::isspace(static_cast<unsigned char>(ch))) {
			result.push_back(ch);
		}
	}
	return result;
}

template <typename TInt>
static bool ParseInteger(std::string_view text, TInt& output) {
	const auto begin = text.data();
	const auto end = begin + text.size();
	const auto [ptr, ec] = std::from_chars(begin, end, output);
	return ec == std::errc{} && ptr == end;
}

static bool ParseFloat(std::string_view text, float& output) {
	std::string temp(text);
	char* parseEnd = nullptr;
	const float value = std::strtof(temp.c_str(), &parseEnd);
	if (parseEnd != (temp.c_str() + temp.size())) {
		return false;
	}
	output = value;
	return true;
}

static bool ParseBool(std::string_view text, bool& output) {
	int numeric = 0;
	if (ParseInteger(text, numeric)) {
		output = numeric != 0;
		return true;
	}

	if (text == "true" || text == "TRUE") {
		output = true;
		return true;
	}
	if (text == "false" || text == "FALSE") {
		output = false;
		return true;
	}
	return false;
}

static bool ReadConfig() {
	std::ifstream configFile("config.txt");
	if (!configFile.is_open()) {
		return false;
	}

	std::string line;
	int offsetX = offset[0];
	int offsetY = offset[1];

	while (std::getline(configFile, line)) {
		const std::string cleaned = RemoveWhitespace(line);
		if (cleaned.empty() || cleaned[0] == '#') {
			continue;
		}

		const size_t delimiterPos = cleaned.find('=');
		if (delimiterPos == std::string::npos || delimiterPos == 0 || delimiterPos + 1 >= cleaned.size()) {
			continue;
		}

		const std::string_view name(cleaned.data(), delimiterPos);
		const std::string_view value(cleaned.data() + delimiterPos + 1, cleaned.size() - delimiterPos - 1);

		if (name == "speed") {
			float parsed = speed;
			if (ParseFloat(value, parsed)) speed = parsed;
		}
		else if (name == "maxX") {
			ParseInteger(value, maxX);
		}
		else if (name == "maxY") {
			ParseInteger(value, maxY);
		}
		else if (name == "offsetX") {
			ParseInteger(value, offsetX);
		}
		else if (name == "offsetY") {
			ParseInteger(value, offsetY);
		}
		else if (name == "flickAim") {
			ParseBool(value, flickAim);
		}
		else if (name == "flickAimTime") {
			ParseInteger(value, flickAimTime);
		}
		else if (name == "full360") {
			ParseInteger(value, full360);
		}
		else if (name == "sortingCounter") {
			ParseInteger(value, sortingCounter);
		}
		else if (name == "holdKey") {
			ParseInteger(value, holdKey);
		}
		else if (name == "isHold") {
			ParseBool(value, isHold);
		}
		else if (name == "invertHold") {
			ParseBool(value, invertHold);
		}
		else if (name == "recoilControl") {
			ParseBool(value, recoilControl);
		}
		else if (name == "overloadManualInputs") {
			ParseBool(value, overloadManualInputs);
		}
	}

	offset[0] = offsetX;
	offset[1] = offsetY;
	return true;
}

static void SaveConfig() {
	std::ofstream configFile("config.txt", std::ios::trunc);
	if (!configFile.is_open()) {
		cout << "Failed to save config" << endl;
		return;
	}

	const int offsetX = offset[0];
	const int offsetY = offset[1];
	const auto writeSetting = [&configFile](std::string_view name, const auto value) {
		configFile << name << "=" << value << '\n';
	};

	writeSetting("speed", speed);
	writeSetting("maxX", maxX);
	writeSetting("maxY", maxY);
	writeSetting("offsetX", offsetX);
	writeSetting("offsetY", offsetY);
	writeSetting("flickAim", static_cast<int>(flickAim));
	writeSetting("flickAimTime", flickAimTime);
	writeSetting("full360", full360);
	writeSetting("sortingCounter", sortingCounter);
	writeSetting("recoilControl", static_cast<int>(recoilControl));
	writeSetting("overloadManualInputs", static_cast<int>(overloadManualInputs));

	configFile << "#All keycodes can be found at https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes\n";
	if (holdKeyIndex > 0 && holdKeyIndex < kHoldKeyCount) {
		writeSetting("holdKey", holdKeysCodes[holdKeyIndex]);
	}
	else {
		writeSetting("holdKey", holdKey);
	}
	writeSetting("isHold", static_cast<int>(isHold));
	writeSetting("invertHold", static_cast<int>(invertHold));
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
