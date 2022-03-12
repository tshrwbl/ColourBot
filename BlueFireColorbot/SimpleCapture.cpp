//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH 
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//*********************************************************

#include "pch.h"
#include "SimpleCapture.h"
#include <fstream>
//#include <windows.graphics.directx.direct3d11.interop.h>

using namespace winrt;
using namespace Windows;
using namespace Windows::UI::Composition;
using namespace Windows::Foundation;
using namespace Windows::System;
using namespace Windows::Graphics;
using namespace Windows::Graphics::Capture;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI;

SimpleCapture::SimpleCapture(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
	ColorSortingMethod currentSortingMethodPtr
	)
{
	m_item = item;
	m_device = device;
	currentSortingMethod = currentSortingMethodPtr;

	// Set up 
	auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
	d3dDevice->GetImmediateContext(m_d3dContext.put());

	auto size = m_item.Size();

	m_swapChain = CreateDXGISwapChain(
		d3dDevice,
		static_cast<uint32_t>(size.Width),
		static_cast<uint32_t>(size.Height),
		static_cast<DXGI_FORMAT>(DirectXPixelFormat::B8G8R8A8UIntNormalized),
		2);

	// Create framepool, define pixel format (DXGI_FORMAT_B8G8R8A8_UNORM), and frame size. 
	m_framePool = Direct3D11CaptureFramePool::Create(
		m_device,
		DirectXPixelFormat::B8G8R8A8UIntNormalized,
		2,
		size);
	m_session = m_framePool.CreateCaptureSession(m_item);
	m_lastSize = size;
	m_frameArrived = m_framePool.FrameArrived(auto_revoke, { this, &SimpleCapture::OnFrameArrived });
}

// Start sending capture frames
void SimpleCapture::StartCapture()
{
	CheckClosed();
	m_session.StartCapture();
}

ICompositionSurface SimpleCapture::CreateSurface(Compositor const& compositor)
{
	CheckClosed();
	return CreateCompositionSurfaceForSwapChain(compositor, m_swapChain.get());
}

bool ProcessColourData(char* data)
{
	return true;
}

bool processFrameForColour(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame& frame, int height, int width) {

	auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
	winrt::com_ptr<ID3D11Texture2D> frameSurface;
	winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), frameSurface.put_void()));

	ID3D11Device* deviceD3D;
	frameSurface->GetDevice(&deviceD3D);
	ID3D11DeviceContext* m_pD3D11Ctx;
	deviceD3D->GetImmediateContext(&m_pD3D11Ctx);

	auto subResource = ::D3D11CalcSubresource(0, 0, 1);
	D3D11_MAPPED_SUBRESOURCE mappedTex;
	auto r = m_pD3D11Ctx->Map(frameSurface.get(), subResource, D3D11_MAP_READ, 0, &mappedTex);
	// FAILS here
	//
	if (FAILED(r)) {
		return false;
	}

	void* d = mappedTex.pData;
	char* data = reinterpret_cast<char*>(d);

	ProcessColourData(data);
	return true;
}


int SaveImageDataCounter = 0;
void SaveImageData(char* data, int height, int width) {
	int trueX = 300;
	int trueY = 300;

	int hWidth = width / 2;
	int hHeight = height / 2;
	//save ofstream file in debug folder
	std::ofstream img(std::to_string(SaveImageDataCounter) + ".ppm"); //#include <fstream>
	//ofstream img2("Test/debugpicDetectionVectors" + std::to_string(counter++) + ".ppm"); //#include <fstream>

	img << "P3" << std::endl;
	img << trueX * 2 << std::endl;
	img << trueY * 2 << std::endl;
	img << "255" << std::endl;


	for (int y = hHeight - trueY; y < hHeight + trueY; y++) {
		for (int x = hWidth - trueX; x < hWidth + trueX; x++) {
			int base = (x + y * width) * 4;
			unsigned short red = data[base + 2] & 255;
			unsigned short green = data[base + 1] & 255;
			unsigned short blue = data[base] & 255;
			img << red << " " << green << " " << blue << "\n";
		}
	}

	//cout << counter << " Images saved" << endl;
}


bool processFrameForColourGitMethod(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame& frame, int height, int width)
{
	//winrt::com_ptr<ID3D11Texture2D> backBuffer;
	//winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
	auto surfaceTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());

	/* Prepare for accessing data bits */
	// Get the device context
	ID3D11Device* d3dDevice;
	surfaceTexture->GetDevice(&d3dDevice);
	ID3D11DeviceContext* d3dContext;
	d3dDevice->GetImmediateContext(&d3dContext);

	// map the texture
	D3D11_MAPPED_SUBRESOURCE mapInfo;
	mapInfo.RowPitch;
	HRESULT hr = d3dContext->Map(
		surfaceTexture.get(),
		0,  // Subresource
		D3D11_MAP_READ,
		0,  // MapFlags
		&mapInfo);


	D3D11_TEXTURE2D_DESC desc;
	surfaceTexture->GetDesc(&desc);

	D3D11_TEXTURE2D_DESC desc2;
	desc2.Width = desc.Width;
	desc2.Height = desc.Height;
	desc2.MipLevels = desc.MipLevels;
	desc2.ArraySize = desc.ArraySize;
	desc2.Format = desc.Format;
	desc2.SampleDesc = desc.SampleDesc;
	desc2.Usage = D3D11_USAGE_STAGING;
	desc2.BindFlags = 0;
	desc2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc2.MiscFlags = 0;


	ID3D11Texture2D* stagingTexture = NULL;
	hr = d3dDevice->CreateTexture2D(&desc2, nullptr, &stagingTexture);
	if (FAILED(hr)) {
		// throw std::invalid_argument("received negative value");
		//std::cout << "Failed to create staging texture";
		return false;
	}

	// copy the texture to a staging resource
	d3dContext->CopyResource(stagingTexture, surfaceTexture.get());

	// now, map the staging resource
	hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapInfo);

	if (FAILED(hr)) {
		// throw std::invalid_argument("received negative value");
		//std::cout << "Failed to map staging texture";
		return false;
	}

	// Frame data is here??
	void* d = mapInfo.pData;
	char* data = reinterpret_cast<char*>(d);


	/* D3D11_MAPPED_SUBRESOURCE mapped = {};
	 winrt::check_hresult(d3dContext->Map(bitmap.get(), 0, D3D11_MAP_READ, 0, &mapped));

	 std::vector<byte> bits(desc.Width * desc.Height * bytesPerPixel, 0);
	 auto source = reinterpret_cast<byte*>(mapped.pData);
	 auto dest = bits.data();
	 for (auto i = 0; i < (int)desc.Height; i++)
	 {
		 memcpy(dest, source, desc.Width * bytesPerPixel);

		 source += mapped.RowPitch;
		 dest += desc.Width * bytesPerPixel;
	 }
	 d3dContext->Unmap(bitmap.get(), 0);*/
	//SaveImageData(data, desc.Height, desc.Width);
			// Frame data is here??


	currentSortingMethod(data, height, width);
	d3dContext->Unmap(stagingTexture, 0);
	d3dContext->Unmap(surfaceTexture.get(), 0);
	//d3dContext->Flush();

	if (stagingTexture != nullptr)
	{
		stagingTexture->Release();
		stagingTexture = nullptr;
	}
	/*if (d3dDevice != nullptr)
	{
		d3dDevice->Release();
		d3dDevice = nullptr;
	}	*/
}

// Process captured frames
void SimpleCapture::Close()
{
	auto expected = false;
	if (m_closed.compare_exchange_strong(expected, true))
	{
		m_frameArrived.revoke();
		m_framePool.Close();
		m_session.Close();

		m_swapChain = nullptr;
		m_framePool = nullptr;
		m_session = nullptr;
		m_item = nullptr;
	}
}

//bool InitColor() {
//	// ==== FIND WINDOW ==== 
//	RECT rect;
//	game_window = FindWindowW(NULL, PROCESS_NAME);
//	//game_window = GetDesktopWindow();
//
//	GetClientRect(game_window, &rect);
//
//	// ==== SCALING FACTOR ====
//	HDC monitor = GetDC(game_window); // GetDC(NULL);
//
//	int current = GetDeviceCaps(monitor, VERTRES);
//	int total = GetDeviceCaps(monitor, DESKTOPVERTRES);
//
//	width = (rect.right - rect.left) * total / current;
//	height = (rect.bottom - rect.top) * total / current;
//
//	// ==== CREATE DEVICE ==== 
//
//	HRESULT hr(E_FAIL);
//	D3D_FEATURE_LEVEL lFeatureLevel;
//
//	for (UINT DriverTypeIndex = 0; DriverTypeIndex < gNumDriverTypes; ++DriverTypeIndex)
//	{
//		hr = D3D11CreateDevice(
//			nullptr,
//			gDriverTypes[DriverTypeIndex],
//			nullptr,
//			0,
//			gFeatureLevels,
//			gNumFeatureLevels,
//			D3D11_SDK_VERSION,
//			&lDevice,
//			&lFeatureLevel,
//			&lImmediateContext);
//
//		if (SUCCEEDED(hr))
//		{
//			// Device creation success, no need to loop anymore
//			break;
//		}
//
//		lDevice.Reset();
//
//		lImmediateContext.Reset();
//	}
//
//	// ==== CREATE TEXTURE ====
//
//	desc.Width = width;
//	desc.Height = height;
//	desc.ArraySize = 1;
//	desc.MipLevels = 1;
//
//	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
//	desc.SampleDesc.Count = 1;
//	desc.SampleDesc.Quality = 0;
//
//	desc.Usage = D3D11_USAGE_DEFAULT;
//
//	desc.BindFlags = 40;
//	desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
//	desc.CPUAccessFlags = 0;
//
//	hr = lDevice->CreateTexture2D(&desc, NULL, &texture);
//
//	if (FAILED(hr)) {
//		cout << "Failed to create texture" << endl;
//		return false;
//	}
//
//	hr = texture->QueryInterface(__uuidof(IDXGISurface1), (void**)&gdiSurface);
//
//	if (FAILED(hr)) {
//		cout << "Failed to create GDI surface" << endl;
//		return false;
//	}
//
//	// REUSE desc FOR FRAMECOPY
//	desc.BindFlags = 0;
//	desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
//	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
//	desc.Usage = D3D11_USAGE_STAGING;
//	InitMoveMouse();
//	cout << "Starting at " << width << "x" << height << endl;
//
//	return true;
//}
bool firstRun = true;
ID3D11DeviceContext* d3dContext;
ID3D11Device* d3dDevice;
D3D11_MAPPED_SUBRESOURCE mapInfo;
D3D11_TEXTURE2D_DESC descr;
D3D11_TEXTURE2D_DESC descr2;
//ID3D11Texture2D* stagingTexture = NULL;
ID3D11Texture2D* stagingTexture = NULL;


void SimpleCapture::OnFrameArrived(
	Direct3D11CaptureFramePool const& sender,
	winrt::Windows::Foundation::IInspectable const&)
{
	//auto newSize = false;

	{
		auto frame = sender.TryGetNextFrame();
		auto frameContentSize = frame.ContentSize();

		//     if (frameContentSize.Width != m_lastSize.Width ||
				 //frameContentSize.Height != m_lastSize.Height)
		//     {
		//         // The thing we have been capturing has changed size.
		//         // We need to resize our swap chain first, then blit the pixels.
		//         // After we do that, retire the frame and then recreate our frame pool.
		//         newSize = true;
		//         m_lastSize = frameContentSize;
		//         m_swapChain->ResizeBuffers(
		//             2, 
				 //	static_cast<uint32_t>(m_lastSize.Width),
				 //	static_cast<uint32_t>(m_lastSize.Height),
		//             static_cast<DXGI_FORMAT>(DirectXPixelFormat::B8G8R8A8UIntNormalized), 
		//             0);
		//     }

		//processFrameForColourGitMethod(frame , frameContentSize.Height, frameContentSize.Width);
		auto surfaceTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
		com_ptr<ID3D11Texture2D> backBuffer;
		check_hresult(m_swapChain->GetBuffer(0, guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
		
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		winrt::check_hresult(d3dContext->Map(bitmap.get(), 0, D3D11_MAP_READ, 0, &mapped));

		std::vector<byte> bits(desc.Width * desc.Height * bytesPerPixel, 0);
		auto source = reinterpret_cast<byte*>(mapped.pData);
		auto dest = bits.data();
		for (auto i = 0; i < (int)desc.Height; i++)
		{
			memcpy(dest, source, desc.Width * bytesPerPixel);

			source += mapped.RowPitch;
			dest += desc.Width * bytesPerPixel;
		}
		d3dContext->Unmap(bitmap.get(), 0);

		////m_d3dContext->CopyResource(backBuffer.get(), surfaceTexture.get());

		//if (firstRun)
		//{
		//	/* Prepare for accessing data bits */
		//	// Get the device context
		//	surfaceTexture->GetDevice(&d3dDevice);
		//	d3dDevice->GetImmediateContext(&d3dContext);

		//	// map the texture
		//	mapInfo.RowPitch;
		//	HRESULT hr = d3dContext->Map(
		//		surfaceTexture.get(),
		//		0,  // Subresource
		//		D3D11_MAP_READ,
		//		0,  // MapFlags
		//		&mapInfo);


		//	surfaceTexture->GetDesc(&descr);

		//	descr2.Width = descr.Width;
		//	descr2.Height = descr.Height;
		//	descr2.MipLevels = descr.MipLevels;
		//	descr2.ArraySize = descr.ArraySize;
		//	descr2.Format = descr.Format;
		//	descr2.SampleDesc = descr.SampleDesc;
		//	descr2.Usage = D3D11_USAGE_STAGING;
		//	descr2.BindFlags = 0;
		//	descr2.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		//	descr2.MiscFlags = 0;


		//	hr = d3dDevice->CreateTexture2D(&descr2, nullptr, &stagingTexture);
		//	if (FAILED(hr)) {
		//		// throw std::invalid_argument("received negative value");
		//		//std::cout << "Failed to create staging texture";
		//		return;
		//	}

		//	// copy the texture to a staging resource
		//	d3dContext->CopyResource(stagingTexture, surfaceTexture.get());

		//	// now, map the staging resource
		//	hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapInfo);
		//	if (FAILED(hr)) {
		//		// throw std::invalid_argument("received negative value");
		//		//std::cout << "Failed to map staging texture";
		//		return;
		//	}

		//	//firstRun = false;
		//}
		//else
		//{
		//	surfaceTexture->GetDevice(&d3dDevice);
		//	d3dDevice->GetImmediateContext(&d3dContext);

		//	HRESULT hr = d3dContext->Map(
		//		surfaceTexture.get(),
		//		0,  // Subresource
		//		D3D11_MAP_READ,
		//		0,  // MapFlags
		//		&mapInfo);

		//	hr = d3dDevice->CreateTexture2D(&descr2, nullptr, &stagingTexture);
		//	if (FAILED(hr)) {		
		//		return;
		//	}



		//	d3dContext->CopyResource(stagingTexture, surfaceTexture.get());
		//	hr = d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapInfo);

		//	if (FAILED(hr)) {
		//		// throw std::invalid_argument("received negative value");
		//		//std::cout << "Failed to map staging texture";
		//		return;
		//	}
		//}

	
	}

	/*DXGI_PRESENT_PARAMETERS presentParameters = { 0 };
	m_swapChain->Present1(1, 0, &presentParameters);*/

	//if (newSize)
	//{
	//    m_framePool.Recreate(
	//        m_device,
	//        DirectXPixelFormat::B8G8R8A8UIntNormalized,
	//        2,
	//        m_lastSize);
	//}
}


