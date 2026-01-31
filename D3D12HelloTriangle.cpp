//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"
#include "DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"   
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include <vector>
#include "glm/gtc/type_ptr.hpp"
#include "manipulator.h"
#include "Windowsx.h"
#include <string>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <stdexcept>
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include "libraries/stb_image/stb_image.h"


#include "NRDIntegration.h"

// NRD integration header already included via D3D12HelloTriangle.h

// Add near other helper implementations (after CreateShaderBindingTable or at end of file)

#include "NRDIntegration.h"
#include <vector>

// Align helper
static inline uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

void D3D12HelloTriangle::PrepareNRDDescriptorPoolIfNeeded()
{
	if (!m_nrd.GetInstance())
		return;

	const nrd::InstanceDesc* instDesc = nrd::GetInstanceDesc(*m_nrd.GetInstance());
	if (!instDesc)
		return;

	// Policzymy max indexInPool z dispatchy
	uint32_t maxIndex = 0;

	const nrd::DispatchDesc* dispatches = nullptr;
	uint32_t dispatchesNum = 0;
	nrd::Identifier id = m_nrd.GetIdentifier();

	if (nrd::GetComputeDispatches(*m_nrd.GetInstance(), &id, 1, dispatches, dispatchesNum) == nrd::Result::SUCCESS)
	{
		for (uint32_t i = 0; i < dispatchesNum; ++i)
		{
			const nrd::DispatchDesc& d = dispatches[i];
			for (uint32_t r = 0; r < d.resourcesNum; ++r)
				maxIndex = std::max(maxIndex, (uint32_t)d.resources[r].indexInPool);
		}
	}

	// Minimalny bezpieczny rozmiar (zeby nie zrobic heap=1)
	const uint32_t kMinPoolSize = 128;
	uint32_t requiredPoolSize = std::max(kMinPoolSize, maxIndex + 1);

	if (m_nrdPoolHeap && m_nrdPoolSize >= requiredPoolSize)
		return;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	// We need space for both SRV and UAV entries. Allocate twice the per-type count.
	desc.NumDescriptors = requiredPoolSize * 2; // <-- FIX: allocate SRV + UAV ranges
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_nrdPoolHeap)));

	m_nrdPoolSize = requiredPoolSize;
	m_nrdHeapInc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}




// Helper: create SRV/UAV in NRD pool at slot index
void D3D12HelloTriangle::WriteNrdSrv(uint32_t index, ID3D12Resource* res)
{
	if (!m_nrdPoolHeap || !res) return;

	UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_nrdPoolHeap->GetCPUDescriptorHandleForHeapStart(), (INT)index, inc);

	D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.Format = res->GetDesc().Format;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;

	m_device->CreateShaderResourceView(res, &s, h);
}

// UWAGA: UAV-y id¹ w DRUG¥ po³owê tabeli: offset = m_nrdPoolSize
void D3D12HelloTriangle::WriteNrdUav(uint32_t indexInPool, ID3D12Resource* res)
{
	if (!m_nrdPoolHeap || !res) return;

	const uint32_t uavIndex = indexInPool + m_nrdPoolSize; // <-- KLUCZOWE

	UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_nrdPoolHeap->GetCPUDescriptorHandleForHeapStart(), (INT)uavIndex, inc);

	D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
	u.Format = res->GetDesc().Format;
	u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	m_device->CreateUnorderedAccessView(res, nullptr, &u, h);
}


ID3D12Resource* D3D12HelloTriangle::GetResourceForNrdType(nrd::ResourceType t)
{
	// Single switch statement
	switch (t)
	{
		// --- INPUTS ---
	case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: return m_aovDiffuse.Get();
	case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST: return m_aovSpecular.Get();
	case nrd::ResourceType::IN_NORMAL_ROUGHNESS:      return m_aovNormalRoughness.Get();
	case nrd::ResourceType::IN_VIEWZ:                 return m_aovViewZ.Get();
	case nrd::ResourceType::IN_MV:                    return m_aovMotionVectors.Get();

		// --- OUTPUT ---
	case nrd::ResourceType::OUT_SIGNAL:               return m_denoisedOutput.Get();

		// --- INTERMEDIATES (Disable overwrite) ---
	case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return nullptr;
	case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return nullptr;

	default: return nullptr;
	}
}

void D3D12HelloTriangle::ExecuteNRDDispatches()
{
	if (!m_nrd.GetInstance())
		return;

	const nrd::DispatchDesc* dispatches = nullptr;
	uint32_t count = 0;
	nrd::Identifier id = m_nrd.GetIdentifier();

	nrd::Result res = nrd::GetComputeDispatches(*m_nrd.GetInstance(), &id, 1, dispatches, count);
	if (res != nrd::Result::SUCCESS || count == 0)
		return;

	// Musi istnieæ pool heap + pipeline/RS
	PrepareNRDDescriptorPoolIfNeeded();
	if (!m_nrdPoolHeap)
		return;

	if (!m_nrdRootSignature || m_nrdPipelines.empty())
		CreateNRDPipelines();

	// Je¿eli pipeline’y s¹ puste/null -> denoiser nic nie zrobi => czarny
	// (opcjonalnie mo¿esz tu dodaæ fallback kopii, ale na razie wracamy)
	ID3D12DescriptorHeap* heaps[] = { m_nrdPoolHeap.Get() };
	m_commandList->SetDescriptorHeaps(1, heaps);

	D3D12_GPU_DESCRIPTOR_HANDLE poolGpuStart = m_nrdPoolHeap->GetGPUDescriptorHandleForHeapStart();

	for (uint32_t i = 0; i < count; ++i)
	{
		const nrd::DispatchDesc& d = dispatches[i];

		if (d.pipelineIndex >= m_nrdPipelines.size() || !m_nrdPipelines[d.pipelineIndex])
			continue;

		// Wpisz deskryptory wymagane przez ten dispatch
		for (uint32_t r = 0; r < d.resourcesNum; ++r)
		{
			const nrd::ResourceDesc& rd = d.resources[r];

			ID3D12Resource* tex = GetResourceForNrdType(rd.type);
			if (!tex) continue;

			if (rd.descriptorType == nrd::DescriptorType::TEXTURE)
			{
				WriteNrdSrv(rd.indexInPool, tex);
			}
			else if (rd.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE)
			{
				// UAV z offsetem w WriteNrdUav
				WriteNrdUav(rd.indexInPool, tex);
			}
		}

		m_commandList->SetComputeRootSignature(m_nrdRootSignature.Get());
		m_commandList->SetPipelineState(m_nrdPipelines[d.pipelineIndex].Get());

		// constants
		if (d.constantBufferData && d.constantBufferDataSize > 0)
		{
			uint32_t needed = (d.constantBufferDataSize + 255) & ~255u;

			if (!m_nrdConstUpload || needed > m_nrdConstUploadSize)
			{
				m_nrdConstUploadSize = std::max(needed, 256u);
				CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
				CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(m_nrdConstUploadSize);
				ThrowIfFailed(m_device->CreateCommittedResource(
					&heapUpload, D3D12_HEAP_FLAG_NONE, &bufDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
					IID_PPV_ARGS(&m_nrdConstUpload)));
			}

			uint8_t* mapped = nullptr;
			D3D12_RANGE rr = { 0,0 };
			ThrowIfFailed(m_nrdConstUpload->Map(0, &rr, reinterpret_cast<void**>(&mapped)));
			memcpy(mapped, d.constantBufferData, d.constantBufferDataSize);
			m_nrdConstUpload->Unmap(0, nullptr);

			// param1 = CBV (tak jak w CreateNRDPipelines)
			m_commandList->SetComputeRootConstantBufferView(1, m_nrdConstUpload->GetGPUVirtualAddress());
		}

		// param0 = descriptor table start
		m_commandList->SetComputeRootDescriptorTable(0, poolGpuStart);

		m_commandList->Dispatch(d.gridWidth, d.gridHeight, 1);
	}
}



// (3) In PopulateCommandList(), before calling ExecuteNRDDispatches(), call UpdateCommonSettings each frame.
// Replace your current denoiser call block with this:



//
D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_frameIndex(0),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_rtvDescriptorSize(0)
{
}

void D3D12HelloTriangle::OnInit() {

	nv_helpers_dx12::CameraManip.setWindowSize(GetWidth(), GetHeight());
	nv_helpers_dx12::CameraManip.setLookat(glm::vec3(1.5f, 1.5f, 1.5f), glm::vec3(0, 0, 0),
		glm::vec3(0, 1, 0));
	nv_helpers_dx12::CameraManip.setMode(nv_helpers_dx12::Manipulator::Fly);
	nv_helpers_dx12::CameraManip.setSpeed(1);
	LoadPipeline();
	LoadScene("Models/scene.json");
	LoadAssets(); // Models

	HDRImage environment =
		LoadHDR("HDR/studio.hdr"); // loading an HDR image for environment lighting

	CreateEnvironmentTexture(environment);

	// Check the raytracing capabilities of the device
	CheckRaytracingSupport();

	// Setup the acceleration structures (AS) for raytracing. When setting up
	// geometry, each bottom-level AS has its own transform matrix.
	CreateAccelerationStructures();

	// Command lists are created in the recording state, but there is
	// nothing to record yet. The main loop expects it to be closed, so
	// close it now.
	ThrowIfFailed(m_commandList->Close());
	// Create the raytracing pipeline, associating the shader code to symbol names
	// and to their root signatures, and defining the amount of memory carried by
	// rays (ray payload)
	CreateRaytracingPipeline(); // #DXR
	// Allocate the buffer storing the raytracing output, with the same dimensions
	// as the target image	
	CreateRaytracingOutputBuffer(); // #DXR
	CreateAOVResources();

	// Creating the pipeline for our own denoiser
	ComPtr<ID3D12Debug> debugController;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	debugController->EnableDebugLayer();

	ComPtr<IDxcUtils> DxcUtils;
	ComPtr<IDxcCompiler3> DxcCompiler;

	// Create Utils
	ThrowIfFailed(DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(&DxcUtils)
	));

	// Create Compiler
	ThrowIfFailed(DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(&DxcCompiler)
	));
	
	auto sourceData = LoadFile(L"shaders/Denoiser.hlsl");

	DxcBuffer buffer;
	buffer.Ptr = sourceData.data();
	buffer.Size = sourceData.size();
	buffer.Encoding = DXC_CP_UTF8;

	ComPtr<IDxcResult> result;
	LPCWSTR arguments[] = {
	L"-E", L"CSMain",        // entry point
	L"-T", L"cs_6_0"         // target
	};

	// Compile
	buffer.Ptr = sourceData.data();
	buffer.Size = sourceData.size();
	buffer.Encoding = DXC_CP_UTF8;

	ThrowIfFailed(DxcCompiler->Compile(
		&buffer,           // source buffer
		arguments,         // array of wide string arguments
		_countof(arguments),
		nullptr,           // include handler (can be nullptr)
		IID_PPV_ARGS(&result)
	));

	HRESULT hrStatus;
	result->GetStatus(&hrStatus);
	if (FAILED(hrStatus))
	{
		ComPtr<IDxcBlobUtf8> pErrors;
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
			OutputDebugStringA((char*)pErrors->GetBufferPointer());
			throw std::runtime_error((char*)pErrors->GetBufferPointer());
		}
	}

	ThrowIfFailed(result->GetResult(&m_denoiseLibrary));

	CreateDenoiseRootSignature();
	CreateDenoisePipeline();

	// #DXR Extra: Perspective Camera
	// Create a buffer to store the modelview and perspective camera matrices
	CreateCameraBuffer();
	// Lights Buffer
	m_lightData.position = XMFLOAT3(2.0f, 5.0f, -3.0f);
	m_lightData.color = XMFLOAT3(1.0f, 0.0f, 0.0f);
	CreateLightsBuffer();


	CreateModelDataBuffer();

	// Create the buffer containing the raytracing result (always output in a
	// UAV), and create the heap referencing the resources used by the raytracing,
	// such as the acceleration structure
	CreateShaderResourceHeap(); // #DXR
	// Create the shader binding table and indicating which shaders
	// are invoked for each instance in the  AS
	CreateShaderBindingTable();

	// Initialize NRD instance
	if (!m_nrd.Initialize(GetWidth(), GetHeight()))
	{
		OutputDebugStringA("NRD initialization failed or returned false.\n");
	}
	else
	{
		// Create NRD pipelines once we have a valid instance and DXIL embedded inside the NRD InstanceDesc
		CreateNRDPipelines();
	}

	// --- IMGUI INITIALIZATION START ---

	// 1. Create a specific Descriptor Heap for ImGui
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 1; // ImGui only needs 1 descriptor for the font
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imguiHeap)));

	// 2. Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Optional

	// 3. Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// 4. Setup Platform/Renderer backends
	// Note: Use Win32Application::GetHwnd() since your code uses the DXSample framework
	ImGui_ImplWin32_Init(Win32Application::GetHwnd());

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = m_device.Get();
	init_info.CommandQueue = m_commandQueue.Get();
	init_info.NumFramesInFlight = FrameCount; // From D3D12HelloTriangle.h (usually 3 or 2)
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;

	// Pass the heap we just created
	init_info.SrvDescriptorHeap = m_imguiHeap.Get();

	// 1. ALLOCATION FUNCTION (Mandatory)
		// We simply return the start of our heap since we only allocated 1 descriptor for ImGui
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
		ID3D12DescriptorHeap* heap = info->SrvDescriptorHeap;
		*out_cpu = heap->GetCPUDescriptorHandleForHeapStart();
		*out_gpu = heap->GetGPUDescriptorHandleForHeapStart();
		};

	// 2. FREE FUNCTION (Mandatory)
	// We don't need to do anything here because we own the heap and destroy it when the app closes
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
		// No-op (Empty function)
		};

	ImGui_ImplDX12_Init(&init_info);
	// --- IMGUI INITIALIZATION END ---
}

// Load the rendering pipeline dependencies.
void D3D12HelloTriangle::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature"). 
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}

	ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// --- FENCE INITIALIZATION ---
	ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

	// Initialize fence value used for signaling/waiting
	m_fenceValue = 1;

	// Create an auto-reset event for fence completion notifications
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
	// Your camera buffer update (DXR side)
	UpdateCameraBuffer();

	// ImGui frame begin
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// --- UI ---
	ImGui::Begin("Raytracing Settings");

	ImGui::Separator();
	ImGui::TextColored(ImVec4(0, 1, 0, 1), "Scene Manager");

	static char modelPathBuffer[128] = "Models/Cube.obj";
	static char scenePathBuffer[128] = "Models/ExampleScene/scene.json";
	static char environmentPathBuffer[128] = "HDR/studio.hdr";
	ImGui::InputText("Model Path", modelPathBuffer, _countof(modelPathBuffer));

	if (ImGui::Button("Add Model")) {
		try
		{
			AddModel(modelPathBuffer);
		}
		catch (const std::runtime_error& e)
		{
			; // TODO : handle error
		}
	}

	ImGui::InputText("Scene Path", scenePathBuffer, _countof(scenePathBuffer));

	if (ImGui::Button("Load Scene"))
	{
		try
		{
			for (int i = Models.size() - 1; i >= 0; i--)
			{
				RemoveModel(i);
			}
			LoadScene(scenePathBuffer);
			for (int i = 0; i < ModelDescriptions.size(); i++)
			{
				AddModel(ModelDescriptions[i].path, true);
				InitializeShaderData(i);
			}
		}
		catch (const std::runtime_error& e)
		{
			; // TODO : handle error
		}
	}
	if (ImGui::Button("Save Scene"))
	{
		try
		{
			SaveScene(scenePathBuffer);
		}
		catch (const std::runtime_error& e)
		{
			; // TODO : handle error
		}
	}

	ImGui::Separator();
	ImGui::Text("Camera Parameters");

	ImGui::DragInt("ISO", (int*)&m_ISOIndex, 100, 100, 1600);
	ImGui::Checkbox("Highlight Overexposed Areas", &m_highlightOverexposed);

	ImGui::Separator();

	const char* items[] = { "Flat", "Normal", "Phong", "MirrorDemo", "BSDF" };
	static int item_current = 0;

	if (currentShading == L"Flat") item_current = 0;
	else if (currentShading == L"Normal") item_current = 1;
	else if (currentShading == L"Phong") item_current = 2;
	else if (currentShading == L"MirrorDemo") item_current = 3;
	else if (currentShading == L"BSDF") item_current = 4;

	if (ImGui::Combo("Shading Mode", &item_current, items, IM_ARRAYSIZE(items))) {
		if (item_current == 0) SetShadingMode(L"Flat");
		if (item_current == 1) SetShadingMode(L"Normal");
		if (item_current == 2) SetShadingMode(L"Phong");
		if (item_current == 3) SetShadingMode(L"MirrorDemo");
		if (item_current == 4) SetShadingMode(L"BSDF");
	}

	if (currentShading == L"BSDF")
	{
		ImGui::Checkbox("Enable NRD Denoiser", &m_enableDenoise);
		ImGui::Separator();
		ImGui::Text("BSDF Parameters");
		ImGui::DragInt("Sample Count", (int*)&m_sampleCount, 1, 1, 20);
		ImGui::Checkbox("Adaptive Sampling", (bool*)&m_enableAdaptiveSampling);
		if (m_enableAdaptiveSampling)
			AdjustSampleCount();
		ImGui::DragInt("Maximum Recursion Depth", (int*)&m_maximumRecursionDepth, 1, 1, 25);

		ImGui::Separator();

		ImGui::Checkbox("Use An Environment Texture", (bool*)&m_enableEnvironmentTexture);
		if (m_enableEnvironmentTexture)
		{
			ImGui::InputText("HDR Path", environmentPathBuffer, _countof(environmentPathBuffer));

			if (ImGui::Button("Change Environment")) {
				try
				{
					CreateEnvironmentTexture(LoadHDR(environmentPathBuffer));
				}
				catch (const std::runtime_error& e)
				{
					; // TODO : handle error
				}
			}
		}
		else
		{
			ImGui::ColorEdit3("Environment Color", &m_environmentColor.x);
			ImGui::DragFloat("Environment Intensity", &m_environmentIntensity, 0.05f, 0.0f, 10.0f);
		}
	}
	else
	{
		m_enableDenoise = false;
	}

	if (currentShading == L"Phong" || currentShading == L"MirrorDemo" || currentShading == L"BSDF")
	{
		ImGui::Separator();
		ImGui::Text("Light Parameters");

		bool lightChanged = false;

		const char* lightTypes[] = { "Point Light", "Directional Light" };
		if (ImGui::Combo("Light Type", &m_lightData.type, lightTypes, IM_ARRAYSIZE(lightTypes)))
			lightChanged = true;

		const char* posLabel = (m_lightData.type == 1) ? "Light Direction" : "Light Position";

		if (m_lightData.type == 1)
		{
			if (ImGui::DragFloat2("Light Angles", &m_lightData.position.x, 1.0f, -360.0f, 360.0f))
				lightChanged = true;
		}
		else
		{
			if (ImGui::DragFloat3(posLabel, &m_lightData.position.x, 0.1f, -1000.0f, 1000.0f))
				lightChanged = true;
		}

		if (ImGui::ColorEdit3("Light Color", &m_lightData.color.x))
			lightChanged = true;

		if (ImGui::DragFloat("Intensity", &m_lightData.intensity, 10.0f, 0.0f, 10000.0f))
			lightChanged = true;

		if (lightChanged)
			UpdateLightsBuffer();
	}

	ImGui::Separator();

	int indexToRemove = -1;
	for (int i = 0; i < (int)Models.size(); i++)
	{
		auto& inst = ModelDescriptions[i];
		auto& inst2 = ModelsShaderData[i];

		ImGui::PushID(i);

		if (ImGui::CollapsingHeader((std::to_string(i) + ": " + std::to_string(inst.id) + " - " + inst.path).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat3("Position", &inst.position.x, 0.05f);
			ImGui::DragFloat3("Rotation", &inst.rotation.x, 1.0f, -360.0f, 360.0f);
			ImGui::DragFloat3("Scale", &inst.scale.x, 0.05f, 0, 100);

			ImGui::Spacing();

			if (ImGui::TreeNode("Material / Instance Settings"))
			{
				// COLOR
				{
//<<<<<<< HEAD
//					bool useVertexData = (inst2.albedo.x == -1.0f && inst2.albedo.y == -1.0f && inst2.albedo.z == -1.0f);
//					if (ImGui::Checkbox("Use Vertex Data", &useVertexData))
//						inst2.albedo = useVertexData ? DirectX::XMFLOAT3(-1, -1, -1) : DirectX::XMFLOAT3(1, 1, 1);
//=======
					bool useVertexData = !(inst2.albedo.x == -1.0f &&
						inst2.albedo.y == -1.0f &&
						inst2.albedo.z == -1.0f);

					if (ImGui::Checkbox("Set Model Color", &useVertexData)) {
						if (!useVertexData) {
							inst2.albedo = DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f);
						}
						else {
							inst2.albedo = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
						}
					}
//>>>>>>> b4669dd4653344d6f34b8b5847b4f88e7d8d3103

					if (useVertexData) {
						ImGui::Indent();
						ImGui::ColorEdit3("Instance Color", &inst2.albedo.x);
						ImGui::Unindent();
					}
				}

				// EMISSION
				bool useEmission = !(inst2.emission == -1.0f);
				{
					if (ImGui::Checkbox("Emmisive Material", &useEmission))
						inst2.emission = !useEmission ? -1.0f : 1.0f;

					if (useEmission) {
						ImGui::Indent();
						ImGui::DragFloat("Emission Intensity", &inst2.emission, 0.01f, 0.0f, 10.0f);
						ImGui::Unindent();
					}
				}

				// ROUGHNESS
				if(!useEmission)
				{
					bool setRoughness = !(inst2.roughness == -1.0f);
					if (ImGui::Checkbox("Set Roughness", &setRoughness))
						inst2.roughness = !setRoughness ? -1.0f : 0.4f;

					if (setRoughness) {
						ImGui::Indent();
						ImGui::DragFloat("Roughness Value", &inst2.roughness, 0.01f, 0.0f, 1.0f);
						ImGui::Unindent();
					}
				}

				// GLASS
				bool isGlass = inst2.isGlass;
				if(!useEmission)
				{
					if (ImGui::Checkbox("Transparent Material", &isGlass))
						inst2.isGlass = isGlass;

					if (isGlass) {
						ImGui::Indent();
						ImGui::DragFloat("IOR Value", &inst2.IOR, 0.01f, 0.0f, 2.0f);
						ImGui::Unindent();
					}
				}

				//METALLIC
				if(!useEmission && !isGlass)
				{
					ImGui::Checkbox("Metallic Material", (bool*)&inst2.isMetallic);
				}

				ImGui::TreePop();
			}

			ImGui::Separator();

			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
			if (ImGui::Button("Remove Object"))
				indexToRemove = i;
			ImGui::PopStyleColor(2);
		}

		ImGui::PopID();
	}

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
		1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

	ImGui::End();

	if (indexToRemove != -1)
		RemoveModel(indexToRemove);

	UpdateModelTranslations();
	UpdateModelDataBuffer();
}

// Render the scene.
void D3D12HelloTriangle::OnRender()
{
	// Record all the commands we need to render the scene into the command list.

	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));

	WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForPreviousFrame();
	// --- IMGUI SHUTDOWN ---
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	// ----------------------
	CloseHandle(m_fenceEvent);

	// Shutdown NRD
	m_nrd.Shutdown();
}

void D3D12HelloTriangle::PopulateCommandList()
{
	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Backbuffer: PRESENT -> RENDER_TARGET
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_frameIndex,
		m_rtvDescriptorSize);

	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// --- Raytracing stuff ---
	BuildTLAS();

	ID3D12DescriptorHeap* rtHeaps[] = { m_srvUavHeap.Get(), m_samplerHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(rtHeaps), rtHeaps);

	// ------------------------------------------------------------
	// 1) Upewnij siê, ¿e zasoby, do których DXR bêdzie pisaæ, s¹ UAV
	// ------------------------------------------------------------
	{
		std::vector<CD3DX12_RESOURCE_BARRIER> barriers;

		auto toUav = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES from)
			{
				if (!r) return;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
					r, from, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
			};

		// outputResource bywa w COPY_SOURCE po poprzedniej klatce
		// (albo po CreateRaytracingOutputBuffer)
		toUav(m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);

		// AOV-y po poprzednim NRD s¹ SRV -> prze³¹cz na UAV przed DXR
		// (jeœli w pierwszej klatce s¹ ju¿ UAV, to to przejœcie te¿ zadzia³a jeœli "from" bêdzie inne,
		// ale lepiej trzymaæ spójny "from" -> zobacz notkê ni¿ej)
		if (m_aovDiffuse)         toUav(m_aovDiffuse.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (m_aovSpecular)        toUav(m_aovSpecular.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (m_aovNormalRoughness) toUav(m_aovNormalRoughness.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (m_aovViewZ)           toUav(m_aovViewZ.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		if (m_aovMotionVectors)   toUav(m_aovMotionVectors.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		// NRD output te¿ bêdzie UAV (compute write)
		if (m_denoisedOutput)
			toUav(m_denoisedOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);

		if (!barriers.empty())
			m_commandList->ResourceBarrier((UINT)barriers.size(), barriers.data());
	}

	// ---------------------------
	// 2) Dispatch rays
	// ---------------------------
	D3D12_DISPATCH_RAYS_DESC desc = {};
	const uint32_t rayGenSize = m_sbtHelper.GetRayGenSectionSize();
	desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = rayGenSize;

	const uint32_t missSize = m_sbtHelper.GetMissSectionSize();
	desc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenSize;
	desc.MissShaderTable.SizeInBytes = missSize;
	desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

	const uint32_t hitSize = m_sbtHelper.GetHitGroupSectionSize();
	desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenSize + missSize;
	desc.HitGroupTable.SizeInBytes = hitSize;
	desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();

	desc.Width = GetWidth();
	desc.Height = GetHeight();
	desc.Depth = 1;

	m_commandList->SetPipelineState1(m_rtStateObject.Get());
	m_commandList->DispatchRays(&desc);

	// *** KLUCZOWE *** - zapewnia, ¿e wszystkie zapisy UAV z DXR s¹ widoczne dla NRD
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));

	// ---------------- NRD DENOISE ----------------
	ID3D12Resource* src = m_outputResource.Get();
	//src = m_aovNormalRoughness.Get();

	if (m_enableDenoise)
	{
		//our denoiser
		m_commandList->SetPipelineState(m_denoisePSO.Get());
		m_commandList->SetComputeRootSignature(m_denoiseRootSignature.Get());

		ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
		m_commandList->SetDescriptorHeaps(1, heaps);

		m_commandList->SetComputeRootDescriptorTable(
			0,
			m_srvUavHeap->GetGPUDescriptorHandleForHeapStart()
		);

		&CD3DX12_RESOURCE_BARRIER::Transition(
			src,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		);

		m_commandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::UAV(nullptr)
		);

		m_commandList->Dispatch(
			(GetWidth() + 7) / 8,
			(GetHeight() + 7) / 8,
			1
		);

		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_denoisedOutput.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);


		// NRD DENOISER -- not used
		/*
		// 3) AOV-y musz¹ byæ SRV (NRD czyta), wiêc UAV -> SRV
		{
			
			std::vector<CD3DX12_RESOURCE_BARRIER> b;

			auto toSrv = [&](ID3D12Resource* r)
				{
					if (!r) return;
					b.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
						r,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
				};

			toSrv(m_aovDiffuse.Get());
			toSrv(m_aovSpecular.Get());
			toSrv(m_aovNormalRoughness.Get());
			toSrv(m_aovViewZ.Get());
			toSrv(m_aovMotionVectors.Get());

			if (!b.empty())
				m_commandList->ResourceBarrier((UINT)b.size(), b.data());
		}

		// NRD potrzebuje swojego heapu + poprawnych descriptorów
		PrepareNRDDescriptorPoolIfNeeded();
		if (!m_nrdRootSignature || m_nrdPipelines.empty())
			CreateNRDPipelines();

		UpdateNRDCommonSettingsPerFrame();
		ExecuteNRDDispatches();

		// NRD output (m_denoisedOutput) jest UAV -> zaraz bêdziemy kopiowaæ
		//src = m_denoisedOutput.Get();
		src = m_aovSpecular.Get();
		// jeœli NRD pisa³ UAV, dodaj barrier UAV
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));

		// 4) Przywróæ AOV-y na UAV na nastêpn¹ klatkê DXR (SRV -> UAV)
		{
			std::vector<CD3DX12_RESOURCE_BARRIER> b;

			auto srvToUav = [&](ID3D12Resource* r)
				{
					if (!r) return;
					b.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
						r,
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
				};

			srvToUav(m_aovDiffuse.Get());
			srvToUav(m_aovSpecular.Get());
			srvToUav(m_aovNormalRoughness.Get());
			srvToUav(m_aovViewZ.Get());


			if (!b.empty())
				m_commandList->ResourceBarrier((UINT)b.size(), b.data());
		}*/
	}

	// --- Copy src -> backbuffer (TYLKO RAZ) ---
	// src jest UAV (output albo denoisedOutput), wiêc UAV -> COPY_SOURCE
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		src, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_DEST));

	m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(), src);

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Przywróæ src do UAV (¿eby kolejna klatka mog³a pisaæ)
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		src, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	// --- Instance buffer update (jak mia³eœ) ---
	{
		m_commandList->CopyResource(m_instancesBuffer.Get(), m_instancesUpload.Get());
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_instancesBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		m_commandList->ResourceBarrier(1, &barrier);
	}

	// ---------- IMGUI RENDER ----------
	ImGui::Render();
	ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiHeap.Get() };
	m_commandList->SetDescriptorHeaps(1, imguiHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
	
	// Backbuffer: RENDER_TARGET -> PRESENT
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());

}




void D3D12HelloTriangle::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12HelloTriangle::CheckRaytracingSupport() {
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
		&options5, sizeof(options5)));
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		throw std::runtime_error("Raytracing not supported on device");
}

void D3D12HelloTriangle::OnKeyDown(UINT8 key)
{
	switch (key)
	{
	case 'W': keyWDown = true; break;
	case 'S': keySDown = true; break;
	case 'A': keyADown = true; break;
	case 'D': keyDDown = true; break;
	case 'Q': keyQDown = true; break;
	case 'E': keyEDown = true; break;
	}
}

void D3D12HelloTriangle::OnKeyUp(UINT8 key)
{
	switch (key)
	{
	case 'W': keyWDown = false; break;
	case 'S': keySDown = false; break;
	case 'A': keyADown = false; break;
	case 'D': keyDDown = false; break;
	case 'Q': keyQDown = false; break;
	case 'E': keyEDown = false; break;
	}
}

//-----------------------------------------------------------------------------
//
// Create a bottom-level acceleration structure based on a list of vertex
// buffers in GPU memory along with their vertex count. The build is then done
// in 3 steps: gathering the geometry, computing the sizes of the required
// buffers, and building the actual AS
//
D3D12HelloTriangle::AccelerationStructureBuffers
D3D12HelloTriangle::CreateBottomLevelAS(
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t> > vVertexBuffers,
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t> > vIndexBuffers) {
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;
	//std::vector<std::pair<ID3D12Resource*, uint32_t> > vVertexBuffers) {
	//nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

	// Adding all vertex buffers and not transforming their position.
	//for (const auto& buffer : vVertexBuffers) {
	//	bottomLevelAS.AddVertexBuffer(buffer.first, 0, buffer.second,
	//		sizeof(Vertex), 0, 0);
	//}

	// Adding all vertex buffers and not transforming their position.
	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		// for (const auto &buffer : vVertexBuffers) {
		if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0,
				vVertexBuffers[i].second, sizeof(Vertex),
				vIndexBuffers[i].first.Get(), 0,
				vIndexBuffers[i].second, nullptr, 0, true);

		else
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0,
				vVertexBuffers[i].second, sizeof(Vertex), 0,
				0);
	}


	// The AS build requires some scratch space to store temporary information.
	// The amount of scratch memory is dependent on the scene complexity.
	UINT64 scratchSizeInBytes = 0;
	// The final AS also needs to be stored in addition to the existing vertex
	// buffers. It size is also dependent on the scene complexity.
	UINT64 resultSizeInBytes = 0;

	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes,
		&resultSizeInBytes);

	// Once the sizes are obtained, the application is responsible for allocating
	// the necessary buffers. Since the entire generation will be done on the GPU,
	// we can directly allocate those on the default heap
	AccelerationStructureBuffers buffers;
	buffers.pScratch = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), scratchSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
		nv_helpers_dx12::kDefaultHeapProps);
	buffers.pResult = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), resultSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	// Build the acceleration structure. Note that this call integrates a barrier
	// on the generated AS, so that it can be used to compute a top-level AS right
	// after this method.
	bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(),
		buffers.pResult.Get(), false, nullptr);

	return buffers;
}

//-----------------------------------------------------------------------------
// Create the main acceleration structure that holds all instances of the scene.
// Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
// the instances, computing the memory requirements for the AS, and building the
// AS itself
//
void D3D12HelloTriangle::CreateTopLevelAS(
	const std::vector<std::pair<ID3D12Resource*, DirectX::XMMATRIX>>& instances,
	bool updateOnly)
{
	nv_helpers_dx12::TopLevelASGenerator generator;

	for (size_t i = 0; i < instances.size(); i++) {
		generator.AddInstance(instances[i].first,
			instances[i].second, static_cast<UINT>(i),
			static_cast<UINT>(i));
	}

	UINT64 scratchSize, resultSize, instanceDescsSize;
	generator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);

	if (!m_topLevelASBuffers.pScratch || m_topLevelASBuffers.pScratch->GetDesc().Width < scratchSize) {
		m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(
			m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nv_helpers_dx12::kDefaultHeapProps);
	}
	if (!m_topLevelASBuffers.pResult || m_topLevelASBuffers.pResult->GetDesc().Width < resultSize) {
		m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(
			m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nv_helpers_dx12::kDefaultHeapProps);
	}
	if (!m_topLevelASBuffers.pInstanceDesc || m_topLevelASBuffers.pInstanceDesc->GetDesc().Width < instanceDescsSize) {
		m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(
			m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	}
	generator.Generate(m_commandList.Get(),
		m_topLevelASBuffers.pScratch.Get(),
		m_topLevelASBuffers.pResult.Get(),
		m_topLevelASBuffers.pInstanceDesc.Get(),
		updateOnly,
		m_topLevelASBuffers.pResult.Get());
}

//-----------------------------------------------------------------------------
// 
// Combine the BLAS and TLAS builds to construct the entire acceleration
// structure required to raytrace the scene
//
void D3D12HelloTriangle::CreateAccelerationStructures() {
	// Build the bottom AS from the Triangle vertex buffer
	//std::vector<AccelerationStructureBuffers> BLASes;
	BLASes.reserve(Models.size());
	for (auto& model : Models)
	{
		BLASes.push_back(
			CreateBottomLevelAS(
				{ {model.m_vertexBuffer.Get(), (UINT)model.vertices.size()} },
				{ {model.m_indexBuffer.Get(),  (UINT)model.indices.size()} }
			)
		);
	}
	m_instances.clear();
	m_instances.reserve(BLASes.size());

	for (int i = 0; i < BLASes.size(); i++)
	{
		XMMATRIX scaleMatrix = XMMatrixScaling(ModelDescriptions[i].scale.x, ModelDescriptions[i].scale.y, ModelDescriptions[i].scale.z);
		XMMATRIX rotationMatrix =
			XMMatrixRotationRollPitchYaw(degreesToRadians(ModelDescriptions[i].rotation.x), degreesToRadians(ModelDescriptions[i].rotation.y), degreesToRadians(ModelDescriptions[i].rotation.z));
		XMMATRIX translationMatrix = XMMatrixTranslation(ModelDescriptions[i].position.x, ModelDescriptions[i].position.y, ModelDescriptions[i].position.z);
		XMMATRIX transform = scaleMatrix * rotationMatrix * translationMatrix;

		m_instances.push_back({
			BLASes[i].pResult.Get(),
			transform
			});
	}



	CreateTopLevelAS(m_instances);

	// Flush the command list and wait for it to finish
	m_commandList->Close();
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
	m_fenceValue++;
	m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

	m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
	WaitForSingleObject(m_fenceEvent, INFINITE);

	// Once the command list is finished executing, reset it to be reused for
	// rendering
	ThrowIfFailed(
		m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// Store the AS buffers. The rest of the buffers will be released once we exit
	// the function
	m_bottomLevelAS.clear();
	for (auto& blas : BLASes)
		m_bottomLevelAS.push_back(blas.pResult);
}

ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateRayGenSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddHeapRangesParameter(
		{
			// Range 1: UAVs u0..u5 (Count = 6)
			// Occupies descriptor heap slots: 0, 1, 2, 3, 4, 5
			{ 0 /*u0*/, 6 /*count*/, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0 },

			// Range 2: TLAS t0 
			// MUST start at 6 (because 0-5 are taken)
			{ 0 /*t0*/, 1,           0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6 }, // Changed 5 to 6

			// Range 3: Camera b0
			// MUST start at 7
			{ 0 /*b0*/, 1,           0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 7 }  // Changed 6 to 7
		});

	return rsc.Generate(m_device.Get(), true);
}



//-----------------------------------------------------------------------------
// The hit shader communicates only through the ray payload, and therefore does
// not require any resources
//
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateHitSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	//rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 0 /*t0*/); // vertices and colors
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1 /*t1*/); // indices
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 2); // t2 - ModelInstanceGPU buffer
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 1 /*b1*/); // light(s)
	//rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 2 /*b2*/);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 3 /*t3*/);
	return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// The miss shader communicates only through the ray payload, and therefore
// does not require any resources
//
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMissSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;

	// Root parameter 0: SRV descriptor table (t0)
	rsc.AddHeapRangesParameter({
		{ 0 /*base t0*/, 1 /*num*/, 0 /*space*/, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0 }
		});

	// Root parameter 1: Sampler descriptor table (s0)
	rsc.AddHeapRangesParameter({
		{ 0 /*base s0*/, 1 /*num*/, 0 /*space*/, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 0 }
		});

	// IMPORTANT: this must be a LOCAL root signature
	return rsc.Generate(m_device.Get(), true);
}


//-----------------------------------------------------------------------------
//
// The raytracing pipeline binds the shader code, root signatures and pipeline
// characteristics in a single structure used by DXR to invoke the shaders and
// manage temporary memory during raytracing
//
//
void D3D12HelloTriangle::CreateRaytracingPipeline()
{
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());

	// The pipeline contains the DXIL code of all the shaders potentially executed
	// during the raytracing process. This section compiles the HLSL code into a
	// set of DXIL libraries. We chose to separate the code in several libraries
	// by semantic (ray generation, hit, miss) for clarity. Any code layout can be
	// used.
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/Miss.hlsl");
	m_flatShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/FlatShader.hlsl");
	m_normalShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/NormalShader.hlsl");
	m_phongShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/PhongShader.hlsl");
	m_mirrorDemoShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/MirrorDemoShader.hlsl");
	m_BSDFShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"shaders/BSDFShader.hlsl");
	// In a way similar to DLLs, each library is associated with a number of
	// exported symbols. This
	// has to be done explicitly in the lines below. Note that a single library
	// can contain an arbitrary number of symbols, whose semantic is given in HLSL
	// using the [shader("xxx")] syntax
	pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });
	pipeline.AddLibrary(m_flatShaderLibrary.Get(), { L"ClosestHit_Flat" });
	pipeline.AddLibrary(m_normalShaderLibrary.Get(), { L"ClosestHit_Normal" });
	pipeline.AddLibrary(m_phongShaderLibrary.Get(), { L"ClosestHit_Phong" });
	pipeline.AddLibrary(m_mirrorDemoShaderLibrary.Get(), { L"ClosestHit_MirrorDemo" });
	pipeline.AddLibrary(m_BSDFShaderLibrary.Get(), { L"ClosestHit_BSDF" });
	// To be used, each DX12 shader needs a root signature defining which
	// parameters and buffers will be accessed.
	m_rayGenSignature = CreateRayGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();
	// 3 different shaders can be invoked to obtain an intersection: an
	// intersection shader is called
	// when hitting the bounding box of non-triangular geometry. This is beyond
	// the scope of this tutorial. An any-hit shader is called on potential
	// intersections. This shader can, for example, perform alpha-testing and
	// discard some intersections. Finally, the closest-hit program is invoked on
	// the intersection point closest to the ray origin. Those 3 shaders are bound
	// together into a hit group.

	// Note that for triangular geometry the intersection shader is built-in. An
	// empty any-hit shader is also defined by default, so in our simple case each
	// hit group contains only the closest hit shader. Note that since the
	// exported symbols are defined above the shaders can be simply referred to by
	// name.

	// Hit group for the triangles, with a shader simply interpolating vertex
	// colors
	std::vector<std::wstring> hitGroups;
	for (int i = 0; i < Models.size(); i++)
	{
		std::wstring FlatHitGroup = L"HitGroup_Flat_" + std::to_wstring(i);
		std::wstring NormalHitGroup = L"HitGroup_Normal_" + std::to_wstring(i);
		std::wstring PhongHitGroup = L"HitGroup_Phong_" + std::to_wstring(i);
		std::wstring MirrorDemoHitGroup = L"HitGroup_MirrorDemo_" + std::to_wstring(i);
		std::wstring BSDFHitGroup = L"HitGroup_BSDF_" + std::to_wstring(i);
		pipeline.AddHitGroup(FlatHitGroup, L"ClosestHit_Flat");
		pipeline.AddHitGroup(NormalHitGroup, L"ClosestHit_Normal");
		pipeline.AddHitGroup(PhongHitGroup, L"ClosestHit_Phong");
		pipeline.AddHitGroup(MirrorDemoHitGroup, L"ClosestHit_MirrorDemo");
		pipeline.AddHitGroup(BSDFHitGroup, L"ClosestHit_BSDF");
		hitGroups.push_back(FlatHitGroup.c_str());
		hitGroups.push_back(NormalHitGroup.c_str());
		hitGroups.push_back(PhongHitGroup.c_str());
		hitGroups.push_back(MirrorDemoHitGroup.c_str());
		hitGroups.push_back(BSDFHitGroup.c_str());
	}
	// The following section associates the root signature to each shader. Note
	// that we can explicitly show that some shaders share the same root signature
	// (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
	// to as hit groups, meaning that the underlying intersection, any-hit and
	// closest-hit shaders share the same root signature.
	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), hitGroups);
	// The payload size defines the maximum size of the data carried by the rays,
	// ie. the the data
	// exchanged between shaders, such as the HitInfo structure in the HLSL code.
	// It is important to keep this value as low as possible as a too high value
	// would result in unnecessary memory consumption and cache trashing.
	pipeline.SetMaxPayloadSize(96); // 4 * sizeof(float)+sizeof(int) + 2 * sizeof(uint32_t)

	// Upon hitting a surface, DXR can provide several attributes to the hit.
	// in. our sample we just use the barycentric coordinates defined by the weights
	// u,v of the last two vertices of the triangle. The actual barycentrics can
	// be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

	// The raytracing process can shoot rays from existing hit points, resulting
	// in nested TraceRay calls. Our sample code traces only primary rays, which
	// then requires a trace depth of 1. Note that this recursion depth should be
	// kept to a minimum for best performance. Path tracing algorithms can be
	// easily flattened into a simple loop in the ray generation.
	pipeline.SetMaxRecursionDepth(31); //31 is the maximum value
	// Compile the pipeline for execution on the GPU
	m_rtStateObject = pipeline.Generate();

	// Cast the state object into a properties object, allowing to later access
	// the shader pointers by name
	ThrowIfFailed(
		m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));
}


//-----------------------------------------------------------------------------
//
// Allocate the buffer holding the raytracing output, with the same size as the
// output image
//
void D3D12HelloTriangle::CreateRaytracingOutputBuffer() {
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	// The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB
	// formats cannot be used with UAVs. For accuracy we should convert to sRGB
	// ourselves in the shader
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = GetWidth();
	resDesc.Height = GetHeight();
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	ThrowIfFailed(m_device->CreateCommittedResource(
		&nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
		IID_PPV_ARGS(&m_outputResource)));
}

//-----------------------------------------------------------------------------
//
// Create the main heap used by the shaders, which will give access to the
// raytracing output and the top-level acceleration structure
//
void D3D12HelloTriangle::CreateShaderResourceHeap()
{
	// 0..4 UAV, 5 TLAS SRV, 6 Camera CBV, reszta wg Twoich potrzeb (np. inst SRV, env SRV)
	// U Ciebie env SRV jest u¿ywane w Miss przez SBT pointer, wiêc NIE jest wymagane w global heap,
	// ale mo¿esz zostawiæ jeœli chcesz.

	const UINT baseCount = 8; // do Camera
	const UINT extraInstanceSrvs = (UINT)Models.size(); // jeœli dalej chcesz to trzymaæ w heapie
	const UINT descriptorCount = baseCount + extraInstanceSrvs + 1; // +1 jeœli chcesz env SRV na koñcu

	m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
		m_device.Get(), descriptorCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart());

	// --- UAVs u0..u4 ---
	auto createUav = [&](ID3D12Resource* res)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
			u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			u.Format = res->GetDesc().Format;
			m_device->CreateUnorderedAccessView(res, nullptr, &u, h);
			h.Offset(1, inc);
		};

	createUav(m_outputResource.Get());        // u0
	createUav(m_aovDiffuse.Get());            // u1
	createUav(m_aovSpecular.Get());           // u2
	createUav(m_aovNormalRoughness.Get());    // u3
	createUav(m_aovViewZ.Get());              // u4
	createUav(m_aovMotionVectors.Get());

	// --- TLAS SRV t0 (slot 5) ---
	D3D12_SHADER_RESOURCE_VIEW_DESC tlas = {};
	tlas.Format = DXGI_FORMAT_UNKNOWN;
	tlas.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	tlas.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	tlas.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
	m_device->CreateShaderResourceView(nullptr, &tlas, h);
	h.Offset(1, inc);

	// --- Camera CBV b0 (slot 6) ---
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	cbv.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbv.SizeInBytes = m_cameraBufferSize;
	m_device->CreateConstantBufferView(&cbv, h);
	h.Offset(1, inc);

	// --- (Opcjonalnie) SRV instancji ---
	for (size_t i = 0; i < Models.size(); ++i)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC instSrv = {};
		instSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		instSrv.Buffer.FirstElement = 0;
		instSrv.Buffer.NumElements = (UINT)ModelsShaderData.size();
		instSrv.Buffer.StructureByteStride = sizeof(ModelInstanceGPU);
		instSrv.Format = DXGI_FORMAT_UNKNOWN;
		instSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_device->CreateShaderResourceView(m_instancesBuffer.Get(), &instSrv, h);
		h.Offset(1, inc);
	}

	// --- (Opcjonalnie) env SRV (jeœli chcesz nadal w heapie) ---
	m_envSrvIndex = baseCount + (UINT)Models.size();
	if (m_envTexture)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC envSrv = {};
		envSrv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		envSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		envSrv.Texture2D.MipLevels = 1;
		envSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_device->CreateShaderResourceView(m_envTexture.Get(), &envSrv, h);
	}

	// Sampler heap jak mia³eœ
	D3D12_DESCRIPTOR_HEAP_DESC sampDesc = {};
	sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	sampDesc.NumDescriptors = 1;
	sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&sampDesc, IID_PPV_ARGS(&m_samplerHeap)));

	D3D12_SAMPLER_DESC samp = {};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.MaxAnisotropy = 1;
	samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samp.MinLOD = 0;
	samp.MaxLOD = D3D12_FLOAT32_MAX;
	m_device->CreateSampler(&samp, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
}



//-----------------------------------------------------------------------------

void D3D12HelloTriangle::CreateShaderBindingTable()
{
    // 1. Reset the helper
    m_sbtHelper.Reset();

    // 2. RayGen: pass start of SRV/UAV heap (global descriptor table)
    D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
        m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

    void* rayGenHeapPtr =
        reinterpret_cast<void*>(srvUavHeapHandle.ptr);

    m_sbtHelper.AddRayGenerationProgram(L"RayGen", { rayGenHeapPtr });

    // 3. Miss shader: pass ENV SRV + SAMPLER
    assert(m_envSrvIndex != UINT_MAX);

    UINT incSize =
        m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_GPU_DESCRIPTOR_HANDLE envGpuHandle =
        m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

    envGpuHandle.ptr += static_cast<SIZE_T>(incSize) * m_envSrvIndex;

    void* envSrvPtr =
        reinterpret_cast<void*>(envGpuHandle.ptr);

    D3D12_GPU_DESCRIPTOR_HANDLE sampGpuHandle =
        m_samplerHeap->GetGPUDescriptorHandleForHeapStart();

    void* samplerPtr =
        reinterpret_cast<void*>(sampGpuHandle.ptr);

    // Order MUST match Miss root signature:
    //  0 = SRV table (t0)
    //  1 = Sampler table (s0)
    m_sbtHelper.AddMissProgram(
        L"Miss",
        { envSrvPtr, samplerPtr }
    );

    // 4. Hit groups (unchanged)
    for (int i = 0; i < Models.size(); i++)
    {
        std::wstring hitGroupName =
            L"HitGroup_" + currentShading + L"_" + std::to_wstring(i);

        void* vertexBufferAddr =
            (void*)Models[i].m_vertexBuffer->GetGPUVirtualAddress();
        void* indexBufferAddr =
            (void*)Models[i].m_indexBuffer->GetGPUVirtualAddress();
        void* instanceBufferAddr =
            (void*)m_instancesBuffer->GetGPUVirtualAddress();
        void* lightsBufferAddr =
            (void*)m_lightsBuffer->GetGPUVirtualAddress();
        void* tlasBufferAddr =
            (void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

        m_sbtHelper.AddHitGroup(
            hitGroupName.c_str(),
            {
                vertexBufferAddr,
                indexBufferAddr,
                instanceBufferAddr,
                lightsBufferAddr,
                tlasBufferAddr
            }
        );
    }

    // 5. Allocate SBT
    uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

    m_sbtStorage = nv_helpers_dx12::CreateBuffer(
        m_device.Get(),
        sbtSize,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nv_helpers_dx12::kUploadHeapProps
    );

    if (!m_sbtStorage)
        throw std::logic_error("Could not allocate the shader binding table");

    // 6. Generate SBT
    m_sbtHelper.Generate(
        m_sbtStorage.Get(),
        m_rtStateObjectProps.Get()
    );
}

void D3D12HelloTriangle::OnButtonDown(UINT32 lParam)
{
	nv_helpers_dx12::CameraManip.setMousePosition(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam));
}

void D3D12HelloTriangle::OnMouseMove(UINT8 wParam, UINT32 lParam)
{
	using nv_helpers_dx12::Manipulator;
	Manipulator::Inputs inputs;
	inputs.lmb = wParam & MK_LBUTTON;
	inputs.mmb = wParam & MK_MBUTTON;
	inputs.rmb = wParam & MK_RBUTTON;
	if (!inputs.lmb && !inputs.rmb && !inputs.mmb)
		return;

	inputs.ctrl = GetAsyncKeyState(VK_CONTROL);
	inputs.shift = GetAsyncKeyState(VK_SHIFT);
	inputs.alt = GetAsyncKeyState(VK_MENU);

	CameraManip.mouseMove(-GET_X_LPARAM(lParam), -GET_Y_LPARAM(lParam), inputs);
}

void D3D12HelloTriangle::SetShadingMode(const std::wstring& mode)
{
	currentShading = mode;
	CreateShaderBindingTable();
}


void D3D12HelloTriangle::BuildTLAS() {
	if (BLASes.empty()) return;

	m_instances.clear();

	for (size_t i = 0; i < BLASes.size(); i++)
	{
		XMMATRIX scaleMatrix = XMMatrixScaling(ModelDescriptions[i].scale.x, ModelDescriptions[i].scale.y, ModelDescriptions[i].scale.z);
		XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(degreesToRadians(ModelDescriptions[i].rotation.x), degreesToRadians(ModelDescriptions[i].rotation.y), degreesToRadians(ModelDescriptions[i].rotation.z));
		XMMATRIX translationMatrix = XMMatrixTranslation(ModelDescriptions[i].position.x, ModelDescriptions[i].position.y, ModelDescriptions[i].position.z);
		XMMATRIX transform = scaleMatrix * rotationMatrix * translationMatrix;

		m_instances.push_back({ BLASes[i].pResult.Get(), transform });
	}

	CreateTopLevelAS(m_instances, !BLASChanged);

	if (BLASChanged == true) {
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
		UINT inc = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		srvHandle.ptr += 5 * inc;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

		m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
		BLASChanged = false;
	}
}

D3D12HelloTriangle::HDRImage D3D12HelloTriangle::LoadHDR(const std::string& path)
{
	D3D12HelloTriangle::HDRImage img;

	// Force float loading
	float* data = stbi_loadf(
		path.c_str(),
		&img.width,
		&img.height,
		&img.channels,
		3 // force RGB
	);

	if (!data)
	{
		throw std::runtime_error(
			"Failed to load HDR image: " + path
		);
	}

	img.channels = 3;
	img.pixels.assign(
		data,
		data + img.width * img.height * 3
	);

	stbi_image_free(data);

	std::cout << "Loaded HDR: "
		<< img.width << "x" << img.height << "\n";

	return img;
}

void D3D12HelloTriangle::CreateEnvironmentTexture(const HDRImage& img)
{
	// Basic validation
	if (img.width <= 0 || img.height <= 0 || img.pixels.empty())
		throw std::runtime_error("CreateEnvironmentTexture: invalid HDR image (empty or zero size).");

	if (!m_device) throw std::runtime_error("CreateEnvironmentTexture: m_device is null.");
	if (!m_commandQueue) throw std::runtime_error("CreateEnvironmentTexture: m_commandQueue is null.");
	if (!m_fence || m_fenceEvent == nullptr) throw std::runtime_error("CreateEnvironmentTexture: fence or fenceEvent not initialized.");

	// Resource description
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = static_cast<UINT64>(img.width);
	texDesc.Height = static_cast<UINT>(img.height);
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // RGBA float
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = S_OK;
	hr = m_device->CreateCommittedResource(
		&nv_helpers_dx12::kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&m_envTexture));
	if (FAILED(hr)) {
		throw std::runtime_error("CreateCommittedResource(env texture) failed. HRESULT = " + std::to_string(hr));
	}

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_envTexture.Get(), 0, 1);
	if (uploadBufferSize == 0) {
		throw std::runtime_error("CreateEnvironmentTexture: GetRequiredIntermediateSize returned 0.");
	}

	// create upload buffer
	ComPtr<ID3D12Resource> uploadBuffer;
	CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	hr = m_device->CreateCommittedResource(
		&heapUpload,
		D3D12_HEAP_FLAG_NONE,
		&bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer));
	if (FAILED(hr)) {
		throw std::runtime_error("CreateCommittedResource(uploadBuffer) failed. HRESULT = " + std::to_string(hr));
	}

	// Expand RGB -> RGBA
	size_t pixelCount = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
	std::vector<float> rgba;
	rgba.resize(pixelCount * 4);
	for (size_t i = 0, s = 0; i < pixelCount; ++i) {
		rgba[s++] = img.pixels[i * 3 + 0];
		rgba[s++] = img.pixels[i * 3 + 1];
		rgba[s++] = img.pixels[i * 3 + 2];
		rgba[s++] = 1.0f;
	}

	D3D12_SUBRESOURCE_DATA subresource = {};
	subresource.pData = rgba.data();
	subresource.RowPitch = static_cast<SIZE_T>(img.width) * 4 * sizeof(float);
	subresource.SlicePitch = subresource.RowPitch * img.height;

	// Create transient command allocator + list for upload
	ComPtr<ID3D12CommandAllocator> uploadAlloc;
	ComPtr<ID3D12GraphicsCommandList> uploadList;
	hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAlloc));
	if (FAILED(hr)) throw std::runtime_error("CreateCommandAllocator failed. HRESULT = " + std::to_string(hr));
	hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAlloc.Get(), nullptr, IID_PPV_ARGS(&uploadList));
	if (FAILED(hr)) throw std::runtime_error("CreateCommandList failed. HRESULT = " + std::to_string(hr));

	// Record copy
	UpdateSubresources(uploadList.Get(), m_envTexture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresource);
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_envTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	uploadList->ResourceBarrier(1, &barrier);

	hr = uploadList->Close();
	if (FAILED(hr)) throw std::runtime_error("uploadList->Close() failed. HRESULT = " + std::to_string(hr));

	// Execute and wait
	ID3D12CommandList* lists[] = { uploadList.Get() };
	m_commandQueue->ExecuteCommandLists(1, lists);

	const UINT64 fenceValue = ++m_fenceValue;
	hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
	if (FAILED(hr)) throw std::runtime_error("Signal(fence) failed. HRESULT = " + std::to_string(hr));
	hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
	if (FAILED(hr)) throw std::runtime_error("SetEventOnCompletion failed. HRESULT = " + std::to_string(hr));
	WaitForSingleObject(m_fenceEvent, INFINITE);

	// success
}

double D3D12HelloTriangle::degreesToRadians(double degrees) {
	// Conversion formula: radians = degrees * PI / 180
	return degrees * 3.141592 / 180.0;
}

void D3D12HelloTriangle::AdjustSampleCount()
{
	if (1.0f / ImGui::GetIO().DeltaTime < m_targetFrameRate)
	{
		m_slowFrameCount++;
	}
	else
	{
		m_slowFrameCount--;
	}
	if (m_slowFrameCount > 2 && m_sampleCount > 1)
	{
		m_sampleCount--;
		m_slowFrameCount = 0;
	}
	if (m_slowFrameCount < -7)
	{
		m_sampleCount++;
		m_slowFrameCount = 0;
	}
}

void D3D12HelloTriangle::UpdateNRDCommonSettingsPerFrame()
{
	if (!m_nrd.GetInstance())
		return;

	nrd::CommonSettings cs = {};

	// Frame index MUST increase every frame when denoising
	cs.frameIndex = m_nrdFrameIndex++;
	cs.accumulationMode = nrd::AccumulationMode::CONTINUE;
	cs.denoisingRange = 1000.0f;

	// Sizes
	cs.resourceSize[0] = static_cast<uint16_t>(GetWidth());
	cs.resourceSize[1] = static_cast<uint16_t>(GetHeight());
	cs.resourceSizePrev[0] = cs.resourceSize[0];
	cs.resourceSizePrev[1] = cs.resourceSize[1];

	cs.rectSize[0] = cs.resourceSize[0];
	cs.rectSize[1] = cs.resourceSize[1];
	cs.rectSizePrev[0] = cs.rectSize[0];
	cs.rectSizePrev[1] = cs.rectSize[1];

	// If you provide MV in world space -> true. Most pipelines provide view-space MV -> false.
	cs.isMotionVectorInWorldSpace = false;
	cs.isHistoryConfidenceAvailable = false;

	// IMPORTANT: viewZScale must match how you interpret depth (NRD expects viewZ linearization settings)
	cs.viewZScale = 1.0f;

	// Current matrices from CameraManip
	const glm::mat4& glmView = nv_helpers_dx12::CameraManip.getMatrix();
	DirectX::XMMATRIX worldToView;
	memcpy(&worldToView, glm::value_ptr(glmView), sizeof(DirectX::XMMATRIX));

	float fovAngleY = 45.0f * DirectX::XM_PI / 180.0f;
	DirectX::XMMATRIX viewToClip = DirectX::XMMatrixPerspectiveFovRH(
		fovAngleY,
		m_aspectRatio,
		0.1f,
		1000.0f
	);

	// Copy current
	memcpy(cs.worldToViewMatrix, &worldToView, sizeof(cs.worldToViewMatrix));
	memcpy(cs.viewToClipMatrix, &viewToClip, sizeof(cs.viewToClipMatrix));

	// Copy prev (from stored values)
	memcpy(cs.worldToViewMatrixPrev, &m_prevWorldToView, sizeof(cs.worldToViewMatrixPrev));
	memcpy(cs.viewToClipMatrixPrev, &m_prevViewToClip, sizeof(cs.viewToClipMatrixPrev));

	// Push to NRD
	if (!m_nrd.UpdateCommonSettings(cs))
	{
		OutputDebugStringA("NRD: UpdateCommonSettings failed\n");
	}

	// Update prev for next frame
	m_prevWorldToView = worldToView;
	m_prevViewToClip = viewToClip;
}



void D3D12HelloTriangle::CreateNRDPipelines()
{
	if (!m_nrd.GetInstance())
		return;

	const nrd::InstanceDesc* instDesc = nrd::GetInstanceDesc(*m_nrd.GetInstance());
	if (!instDesc)
	{
		OutputDebugStringA("CreateNRDPipelines: GetInstanceDesc returned null\n");
		return;
	}

	// Upewnij siê ¿e pool heap istnieje i ma sensowny rozmiar
	PrepareNRDDescriptorPoolIfNeeded();
	if (!m_nrdPoolHeap || m_nrdPoolSize == 0)
	{
		OutputDebugStringA("CreateNRDPipelines: NRD pool heap missing\n");
		return;
	}

	// Root signature:
	//  param0 = jedna tablica descriptorów (SRV+UAV razem) startuj¹ca od resourcesBaseRegisterIndex
	//  param1 = CBV na sta³e per-dispatch
	CD3DX12_DESCRIPTOR_RANGE range = {};
	range.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, // typ "nie ma znaczenia" dla RS v1? ma znaczenie — ale NRD u¿ywa i SRV i UAV.
		// Dlatego robimy 2 zakresy: SRV + UAV.
		0, 0, 0
	);

	std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
	ranges.resize(2);

	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].NumDescriptors = m_nrdPoolSize;
	ranges[0].BaseShaderRegister = instDesc->resourcesBaseRegisterIndex;
	ranges[0].RegisterSpace = instDesc->resourcesSpaceIndex;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[1].NumDescriptors = m_nrdPoolSize;
	ranges[1].BaseShaderRegister = instDesc->resourcesBaseRegisterIndex;
	ranges[1].RegisterSpace = instDesc->resourcesSpaceIndex;
	ranges[1].OffsetInDescriptorsFromTableStart = 0;

	// UWAGA: D3D12 nie pozwala na “ten sam” slot logiczny dla SRV i UAV w jednej tabeli.
	// Dlatego w praktyce NRD integracje robi¹ JEDEN pool i zapisuj¹ SRV/UAV w TE SAME indeksy,
	// ale w RS maj¹ osobne zakresy: SRV range i UAV range z APPEND.
	// To jest poprawna forma:
	ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	CD3DX12_ROOT_PARAMETER params[2];
	params[0].InitAsDescriptorTable((UINT)ranges.size(), ranges.data(), D3D12_SHADER_VISIBILITY_ALL);

	params[1].InitAsConstantBufferView(
		instDesc->constantBufferRegisterIndex,
		instDesc->constantBufferAndSamplersSpaceIndex,
		D3D12_SHADER_VISIBILITY_ALL
	);

	CD3DX12_ROOT_SIGNATURE_DESC rsDesc(
		_countof(params), params,
		0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	ComPtr<ID3DBlob> sigBlob;
	ComPtr<ID3DBlob> errBlob;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) OutputDebugStringA((const char*)errBlob->GetBufferPointer());
		ThrowIfFailed(hr);
	}

	ThrowIfFailed(m_device->CreateRootSignature(
		0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&m_nrdRootSignature)
	));

	// PSO
	m_nrdPipelines.clear();
	m_nrdPipelines.resize(instDesc->pipelinesNum);

	for (uint32_t i = 0; i < instDesc->pipelinesNum; ++i)
	{
		const nrd::PipelineDesc& pd = instDesc->pipelines[i];
		if (!pd.computeShaderDXIL.bytecode || pd.computeShaderDXIL.size == 0)
		{
			m_nrdPipelines[i] = nullptr;
			continue;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_nrdRootSignature.Get();
		psoDesc.CS.pShaderBytecode = pd.computeShaderDXIL.bytecode;
		psoDesc.CS.BytecodeLength = pd.computeShaderDXIL.size;

		ComPtr<ID3D12PipelineState> pso;
		hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
		if (FAILED(hr))
		{
			OutputDebugStringA("NRD: CreateComputePipelineState failed\n");
			m_nrdPipelines[i] = nullptr;
			continue;
		}

		m_nrdPipelines[i] = pso;
	}

	// Upload buffer dla constants
	uint32_t cbMax = instDesc->constantBufferMaxDataSize;
	if (cbMax > 0)
	{
		uint32_t aligned = AlignUp(cbMax, 256);
		if (!m_nrdConstUpload || aligned > m_nrdConstUploadSize)
		{
			m_nrdConstUploadSize = aligned;
			CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(m_nrdConstUploadSize);
			ThrowIfFailed(m_device->CreateCommittedResource(
				&heapUpload, D3D12_HEAP_FLAG_NONE, &bufDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(&m_nrdConstUpload)
			));
		}
	}

	OutputDebugStringA("NRD: Pipelines + root signature created\n");
}




void D3D12HelloTriangle::CreateAOVResources()
{
	const UINT w = GetWidth();
	const UINT h = GetHeight();

	auto makeTex = [&](DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& out)
		{
			D3D12_RESOURCE_DESC d = {};
			d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			d.Width = w;
			d.Height = h;
			d.DepthOrArraySize = 1;
			d.MipLevels = 1;
			d.Format = fmt;
			d.SampleDesc.Count = 1;
			d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			ThrowIfFailed(m_device->CreateCommittedResource(
				&nv_helpers_dx12::kDefaultHeapProps,
				D3D12_HEAP_FLAG_NONE,
				&d,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // <-- KLUCZOWE
				nullptr,
				IID_PPV_ARGS(&out)
			));
		};

	// 1. Color/Diffuse/Spec can be UNORM (Standard Color)
	makeTex(DXGI_FORMAT_R8G8B8A8_UNORM, m_aovDiffuse);
	makeTex(DXGI_FORMAT_R8G8B8A8_UNORM, m_aovSpecular);

	// 2. Normals need precision. R8G8B8A8_UNORM is "okay" but R16_FLOAT is safer for NRD.
	// Let's stick to FLOAT to be safe.
	makeTex(DXGI_FORMAT_R16G16B16A16_FLOAT, m_aovNormalRoughness);

	// 3. CRITICAL: ViewZ MUST be FLOAT (R32 or R16)
	// UNORM destroys depth data.
	makeTex(DXGI_FORMAT_R32_FLOAT, m_aovViewZ);

	// 4. Motion Vectors
	makeTex(DXGI_FORMAT_R16G16_FLOAT, m_aovMotionVectors);

	// 5. Output
	makeTex(DXGI_FORMAT_R8G8B8A8_UNORM, m_denoisedOutput);
}

// Our own denoising

void D3D12HelloTriangle::CreateDenoiseRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE ranges[1];
	ranges[0].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		6,    // u0, u1
		0     // base register
	);

	CD3DX12_ROOT_PARAMETER rootParams[1];
	rootParams[0].InitAsDescriptorTable(
		1,
		&ranges[0]
	);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init(
		1,
		rootParams,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	ComPtr<ID3DBlob> serialized;
	ComPtr<ID3DBlob> error;

	D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&serialized,
		&error
	);

	ThrowIfFailed(m_device->CreateRootSignature(
		0,
		serialized->GetBufferPointer(),
		serialized->GetBufferSize(),
		IID_PPV_ARGS(&m_denoiseRootSignature)
	));
}

void D3D12HelloTriangle::CreateDenoisePipeline()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_denoiseRootSignature.Get();
	psoDesc.CS = {
		m_denoiseLibrary->GetBufferPointer(),
		m_denoiseLibrary->GetBufferSize()
	};

	ThrowIfFailed(m_device->CreateComputePipelineState(
		&psoDesc,
		IID_PPV_ARGS(&m_denoisePSO)
	));

}

std::vector<char> D3D12HelloTriangle::LoadFile(const wchar_t* filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file) throw std::runtime_error("Cannot open file");
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<char> buffer(size);
	file.read(buffer.data(), size);
	return buffer;
}