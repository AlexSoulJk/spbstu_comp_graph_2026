#include "Postprocessing.h"

#include <d3dcompiler.h>
#include <cmath>

HRESULT Postprocessing::CompileShaderFromFile(const WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut)
{
	HRESULT hr = S_OK;

	DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	// Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
	// Setting this flag improves the shader debugging experience, but still allows 
	// the shaders to be optimized and to run exactly the way they will run in 
	// the release configuration of this program.
	dwShaderFlags |= D3DCOMPILE_DEBUG;

	// Disable optimizations to further improve shader debugging
	dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	D3DInclude includeObj;

	ID3DBlob* pErrorBlob = nullptr;
	hr = D3DCompileFromFile(szFileName, nullptr, &includeObj, szEntryPoint, szShaderModel, dwShaderFlags, 0, ppBlobOut, &pErrorBlob);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			OutputDebugStringA(reinterpret_cast<const char*>(pErrorBlob->GetBufferPointer()));
			pErrorBlob->Release();
		}
		return hr;
	}

	if (pErrorBlob)
		pErrorBlob->Release();

	return S_OK;
}

Postprocessing::Postprocessing()
	: maxTextureHeight(0)
	, maxTextureWidth(0)
	, last(std::chrono::steady_clock::now())
{
}

Postprocessing::~Postprocessing() {
	Release();
}

void Postprocessing::Update(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext)
{
	D3D11_VIEWPORT vp = { 0 };
	unsigned vpNum = 1;
	pContext->RSGetViewports(&vpNum, &vp);
	if (vp.Width != maxTextureWidth || vp.Height != maxTextureHeight)
	{
		clearScaledHDRTargets();
		createDownsamplingRTT((int)vp.Width, (int)vp.Height, pDevice, pContext);
	}
}

HRESULT Postprocessing::Init(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext)
{
	// create pixel shaders
	// Compile the pixel shader
	ID3DBlob* pPSBlob = nullptr;
	HRESULT hr = CompileShaderFromFile(L"BrightnessCalc.ps", "main", "ps_5_0", &pPSBlob);
	if (FAILED(hr))
	{
		MessageBox(nullptr,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK);
		return hr;
	}

	// Create the pixel shader
	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &PSBrightness);
	pPSBlob->Release();
	if (FAILED(hr))
		return hr;


	// Compile the pixel shader
	pPSBlob = nullptr;
	hr = CompileShaderFromFile(L"sampling.ps", "main", "ps_5_0", &pPSBlob);
	if (FAILED(hr))
	{
		MessageBox(nullptr,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK);
		return hr;
	}

	// Create the pixel shader
	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &PSCopy);
	pPSBlob->Release();
	if (FAILED(hr))
		return hr;

	// Compile the pixel shader
	pPSBlob = nullptr;
	hr = CompileShaderFromFile(L"HDR.ps", "main", "ps_5_0", &pPSBlob);
	if (FAILED(hr))
	{
		MessageBox(nullptr,
			L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK);
		return hr;
	}

	// Create the pixel shader
	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &PSHdr);
	pPSBlob->Release();
	if (FAILED(hr))
		return hr;

	// create cpu average lumen texture 
	D3D11_TEXTURE2D_DESC td;
	ZeroMemory(&td, sizeof(td));
	td.Width = 1;
	td.Height = 1;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	td.SampleDesc.Count = 1;
	td.SampleDesc.Quality = 0;
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	hr = pDevice->CreateTexture2D(&td, nullptr, &pAverageLumenCPUTexture);
	if (FAILED(hr))
		return hr;

	D3D11_VIEWPORT vp = { 0 };
	unsigned vpNum = 1;
	pContext->RSGetViewports(&vpNum, &vp);
	createDownsamplingRTT((int)vp.Width, (int)vp.Height, pDevice, pContext);

	// create texture samplers for downsampling process
	D3D11_SAMPLER_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sd.MipLODBias = 0.0f;
	sd.MaxAnisotropy = 1;
	sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sd.BorderColor[0] = 0;
	sd.BorderColor[1] = 0;
	sd.BorderColor[2] = 0;
	sd.BorderColor[3] = 0;
	sd.MinLOD = 0;
	sd.MaxLOD = D3D11_FLOAT32_MAX;

	hr = pDevice->CreateSamplerState(&sd, &pSamplerState);
	if (FAILED(hr))
		return hr;

	hr = screenPlane.Init(pDevice, pContext);
	if (FAILED(hr))
		return hr;

	const HDRConstantBuffer hdrcb = { { prevExposure, 0.f, 0.f, 0.f } };
	D3D11_BUFFER_DESC hdrcbDesc = { 0 };
	hdrcbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hdrcbDesc.Usage = D3D11_USAGE_DYNAMIC;
	hdrcbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	hdrcbDesc.MiscFlags = 0u;
	hdrcbDesc.ByteWidth = sizeof(hdrcb);
	hdrcbDesc.StructureByteStride = 0u;
	D3D11_SUBRESOURCE_DATA hdrsd = {};
	hdrsd.pSysMem = &hdrcb;

	hr = pDevice->CreateBuffer(&hdrcbDesc, &hdrsd, &PSConstantBuffer);

	return hr;
}

void Postprocessing::createDownsamplingRTT(
	int width, int height,
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext)
{
	maxTextureHeight = height;
	maxTextureWidth = width;
	unsigned rtv_num = static_cast<int>(std::floor(std::log2(width < height ? width : height)));
	RenderTargetTexture* rtt = new RenderTargetTexture(height, width);
	rtt->initResource(pDevice, pContext);
	scaledHDRTargets.push_back(rtt);

	for (size_t i = 0; i <= rtv_num; ++i) {
		int dim = (1 << (rtv_num - i));
		rtt = new RenderTargetTexture(dim, dim);
		rtt->initResource(pDevice, pContext);
		scaledHDRTargets.push_back(rtt);
	}
}

HRESULT Postprocessing::applyTonemapEffect(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext,
	ID3DUserDefinedAnnotation* pAnnotation,
	RenderTargetTexture* inputRTT,
	RenderTargetTexture* resultRTT)
{
	if (pDevice == nullptr || pContext == nullptr || inputRTT == nullptr || resultRTT == nullptr)
		return E_INVALIDARG;

	if (scaledHDRTargets.empty() || pAverageLumenCPUTexture == nullptr || PSConstantBuffer == nullptr)
		return E_FAIL;

#ifdef _DEBUG
	if (pAnnotation != nullptr)
		pAnnotation->BeginEvent(L"Average Brightness");
#endif

	// Convert input RTT into BW texture
	pContext->PSSetShader(PSBrightness, nullptr, 0u);
	processTexture(inputRTT, scaledHDRTargets[0], pDevice, pContext);

	// Recursive sampling texture
	pContext->PSSetShader(PSCopy, nullptr, 0u);
	for (size_t i = 1; i < scaledHDRTargets.size(); i++)
		processTexture(scaledHDRTargets[i - 1], scaledHDRTargets[i], pDevice, pContext);

	// Get average brightness of inputed texture
	pContext->OMSetRenderTargets(0, nullptr, nullptr);
	resultRTT->set(pDevice, pContext);

	float averageLogBrightness = (prevExposure > 1e-4f) ? prevExposure : 1.0f;
	HRESULT hr = S_OK;
	if (!m_disableLuminanceReadback)
	{
		D3D11_MAPPED_SUBRESOURCE averageTextureData;
		ZeroMemory(&averageTextureData, sizeof(averageTextureData));
		scaledHDRTargets.back()->copyToTexture(pAverageLumenCPUTexture, pDevice, pContext);

		hr = pContext->Map(pAverageLumenCPUTexture, 0, D3D11_MAP_READ, 0, &averageTextureData);
		if (FAILED(hr) || averageTextureData.pData == nullptr)
		{
			// On runtime skybox rebuilds the GPU can be temporarily busy.
			// Retry once after Flush and keep previous exposure if mapping still fails.
			pContext->Flush();
			ZeroMemory(&averageTextureData, sizeof(averageTextureData));
			hr = pContext->Map(pAverageLumenCPUTexture, 0, D3D11_MAP_READ, 0, &averageTextureData);
		}

		if (SUCCEEDED(hr) && averageTextureData.pData != nullptr)
		{
			averageLogBrightness = std::exp(*(float*)averageTextureData.pData) - 1.0f;
			pContext->Unmap(pAverageLumenCPUTexture, 0u);
		}
		else
		{
			if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
			{
				m_disableLuminanceReadback = true;
				OutputDebugStringA("[PostFX] Luminance readback disabled after device-removed/reset; using exposure fallback.\n");
			}
			else
			{
				OutputDebugStringA("[PostFX] Map(pAverageLumenCPUTexture) failed, using previous exposure fallback.\n");
			}
		}
	}

	// Make exposuring with EyeAdaptation
	const auto old = last;
	last = std::chrono::steady_clock::now();
	float duration = std::chrono::duration<float>(last - old).count();

	float expGain = (1 - std::exp(-duration / eyeAdaptationS));
	prevExposure += (averageLogBrightness - prevExposure) * expGain;

#ifdef _DEBUG
	if (pAnnotation != nullptr)
		pAnnotation->EndEvent(); // Average Brightness
#endif

	// Implementing tonemap
#ifdef _DEBUG
	if (pAnnotation != nullptr)
		pAnnotation->BeginEvent(L"Tonemaping");
#endif
	pContext->PSSetShader(PSHdr, nullptr, 0u);

	// Update data in buffer
	// Get the view matrix
	D3D11_MAPPED_SUBRESOURCE subresource;
	hr = pContext->Map(PSConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
	if (FAILED(hr))
		return hr;

	HDRConstantBuffer& sceneBuffer = *reinterpret_cast<HDRConstantBuffer*>(subresource.pData);
	sceneBuffer.averageLumen = DirectX::XMFLOAT4(prevExposure, 0.f, 0.f, 0.f);
	pContext->Unmap(PSConstantBuffer, 0);

	pContext->PSSetConstantBuffers(0u, 1u, &PSConstantBuffer);
	processTexture(inputRTT, resultRTT, pDevice, pContext);

#ifdef _DEBUG
	if (pAnnotation != nullptr)
		pAnnotation->EndEvent(); // Tonemaping
#endif

	return hr;
}

HRESULT Postprocessing::applyCopyEffect(
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext,
	RenderTargetTexture* inputRTT,
	RenderTargetTexture* resultRTT)
{
	pContext->PSSetShader(PSCopy, nullptr, 0u);
	processTexture(inputRTT, resultRTT, pDevice, pContext);
	return S_OK;
}

void Postprocessing::processTexture(
	RenderTargetTexture* inputTex,
	RenderTargetTexture* resultTex,
	ID3D11Device* pDevice,
	ID3D11DeviceContext* pContext)
{
	ID3D11ShaderResourceView* const pSRV[1] = { nullptr };
	//pContext->PSSetShaderResources(0u, 1u, pSRV);

	pContext->OMSetRenderTargets(0, nullptr, nullptr);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	inputTex->setAsResource(pDevice, pContext);
	resultTex->set(pDevice, pContext);
	pContext->PSSetSamplers(0, 1, &pSamplerState);

	screenPlane.setVS(pDevice, pContext);
	screenPlane.Render(pDevice, pContext);
}

void Postprocessing::Release() {
	clearScaledHDRTargets();
	m_disableLuminanceReadback = false;

	if (PSConstantBuffer) { PSConstantBuffer->Release(); PSConstantBuffer = nullptr; }

	screenPlane.Release();

	if (pSamplerState) { pSamplerState->Release(); pSamplerState = nullptr; }

	if (pAverageLumenCPUTexture) { pAverageLumenCPUTexture->Release(); pAverageLumenCPUTexture = nullptr; }

	if (PSHdr) {
		PSHdr->Release(); PSHdr = nullptr;
	}
	if (PSCopy) {
		PSCopy->Release(); PSCopy = nullptr;
	}
	if (PSBrightness) {
		PSBrightness->Release(); PSBrightness = nullptr;
	}
}

void Postprocessing::clearScaledHDRTargets() {
	for (size_t i = 0; i < scaledHDRTargets.size(); i++) {
		delete scaledHDRTargets[i];
	}
	scaledHDRTargets.clear();
}
