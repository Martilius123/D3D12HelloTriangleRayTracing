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
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>
#include "glm/gtc/type_ptr.hpp"
#include "manipulator.h"
#include "Windowsx.h"
#include <string>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
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
	LoadAssets();

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
}

// Load the sample assets.
void D3D12HelloTriangle::LoadAssets()
{
	// #DXR Extra: Perspective Camera
	// The root signature describes which data is accessed by the shader. The camera matrices are held
	// in a constant buffer, itself referenced the heap. To do this we reference a range in the heap,
	// and use that range as the sole parameter of the shader. The camera buffer is associated in the
	// index 0, making it accessible in the shader in the b0 register.
	CD3DX12_ROOT_PARAMETER constantParameter;
	CD3DX12_DESCRIPTOR_RANGE range;
	range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	constantParameter.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(1, &constantParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// Create an empty root signature.
	{
		//CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		//rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.

	// Create the vertex buffer.
	{
		std::vector<std::string> modelPaths = {
			"Models/Clamp.stl",
			"Models/Cube.obj",
			"Models/Cube.obj",
			"Models/FinalBaseMesh.obj",
			//"Models/13517_Beach_Ball_v2_L3.obj"
		};
		//MODEL
		Models.resize(modelPaths.size());

		ModelsShaderData.resize(modelPaths.size());
		//Models[0].position = { 3.0f,0.0f,0.0f };
		//Models[0].rotation = { XMConvertToRadians(45.0f), 0.0f, 0.0f };
		//Models[1].rotation = { XMConvertToRadians(-45.0f), 0.0f, 0.0f };
		//ModelsShaderData[0].testColor = { 1.0f,1.0f,0.5f };
		ModelsShaderData[0].testColor = { 0.0f,1.0f,0.5f };
		ModelsShaderData[1].testColor = { 1.0f,1.0f,0.5f };
		ModelsShaderData[2].testColor = { 0.7f,0.7f,0.0f };
		ModelsShaderData[3].testColor = { 0.9f,0.0f,0.1f };
		Models[3].scale = { 20.0f,20.0f,20.0f };
		for (int i = 0; i < modelPaths.size(); i++)
		{
			
			LoadModel(modelPaths[i], Models[i].vertices, Models[i].indices);
			

			ModelsShaderData[i].id = Models[i].id = i;

			const UINT vertexBufferSize = static_cast<UINT>(Models[i].vertices.size()) * sizeof(Vertex);

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&(Models[i].m_vertexBuffer))));

			UINT8* pVertexDataBegin;
			CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
			ThrowIfFailed(Models[i].m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
			memcpy(pVertexDataBegin, Models[i].vertices.data(), vertexBufferSize);
			Models[i].m_vertexBuffer->Unmap(0, nullptr);

			Models[i].m_vertexBufferView.BufferLocation = Models[i].m_vertexBuffer->GetGPUVirtualAddress();
			Models[i].m_vertexBufferView.StrideInBytes = sizeof(Vertex);
			Models[i].m_vertexBufferView.SizeInBytes = vertexBufferSize;

			const UINT indexBufferSize = static_cast<UINT>(Models[i].indices.size()) * sizeof(UINT);

			CD3DX12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC bufferResource = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
			ThrowIfFailed(m_device->CreateCommittedResource(
				&heapProperty, D3D12_HEAP_FLAG_NONE, &bufferResource, //
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&(Models[i].m_indexBuffer))));

			// Copy the triangle data to the index buffer.
			UINT8* pIndexDataBegin;
			ThrowIfFailed(Models[i].m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
			memcpy(pIndexDataBegin, Models[i].indices.data(), indexBufferSize);
			Models[i].m_indexBuffer->Unmap(0, nullptr);

			// Initialize the index buffer view.
			Models[i].m_indexBufferView.BufferLocation = Models[i].m_indexBuffer->GetGPUVirtualAddress();
			Models[i].m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
			Models[i].m_indexBufferView.SizeInBytes = indexBufferSize;

			//IndexCount = static_cast<UINT>(Models[i].indices.size());
			//VertexCount = static_cast<UINT>(Models[i].vertices.size());
		}
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame();
	}
}

// Update frame-based values.
void D3D12HelloTriangle::OnUpdate()
{
	// #DXR Extra: Perspective Camera
	UpdateCameraBuffer();

	// --- IMGUI UPDATE ---
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Define your UI here
	ImGui::Begin("Raytracing Settings");

	ImGui::Separator();
	ImGui::TextColored(ImVec4(0, 1, 0, 1), "Scene Manager");

	// Static buffer to hold the path text (persists between frames)
	static char pathBuffer[128] = "Models/Cube.obj";
	ImGui::InputText("Model Path", pathBuffer, _countof(pathBuffer));

	if (ImGui::Button("Add Model")) {
		// This triggers the heavy function we wrote earlier
		AddModel(pathBuffer);
	}

	ImGui::Separator();

	//if (ImGui::Button("Switch Raster/Raytrace")) {
	//	m_raster = !m_raster;
	//}

	// Example: Dropdown for shaders
	const char* items[] = { "Flat", "Normal", "Phong", "MirrorDemo"};
	static int item_current = 0;
	// Sync current selection with your std::wstring currentShading
	if (currentShading == L"Flat") item_current = 0;
	else if (currentShading == L"Normal") item_current = 1;
	else if (currentShading == L"Phong") item_current = 2;
	else if (currentShading == L"MirrorDemo") item_current = 3;

	if (ImGui::Combo("Shading Mode", &item_current, items, IM_ARRAYSIZE(items))) {
		if (item_current == 0) SetShadingMode(L"Flat");
		if (item_current == 1) SetShadingMode(L"Normal");
		if (item_current == 2) SetShadingMode(L"Phong");
		if (item_current == 3) SetShadingMode(L"MirrorDemo");
	}
	if (currentShading == L"Phong")
	{
		ImGui::Separator();
		ImGui::Text("Light Parameters");

		bool lightChanged = false;

		if (ImGui::DragFloat3("Light Pos", &m_lightData.position.x, 0.1f))
			lightChanged = true;

		if (ImGui::ColorEdit3("Light Color", &m_lightData.color.x))
			lightChanged = true;

		// Only upload to GPU if the user actually touched the UI
		if (lightChanged)
		{
			UpdateLightsBuffer();
		}
	}
	ImGui::Separator();

	int indexToRemove = -1; 

	for (int i = 0; i < Models.size(); i++) {
		auto& inst = Models[i];

		ImGui::PushID(i);


		if (ImGui::CollapsingHeader((std::to_string(i) + ": " + std::to_string(inst.id)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

			if (ImGui::DragFloat3("Position", &inst.position.x, 0.05f));
			if (ImGui::DragFloat3("Rotation", &inst.rotation.x, 0.05f));
			if (ImGui::DragFloat3("Scale", &inst.scale.x, 0.05f, 0, 100));

			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
			if (ImGui::Button("Remove Object")) {
				indexToRemove = i;
			}
			ImGui::PopStyleColor(2);
		}

		ImGui::PopID();
	}

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();
	if (indexToRemove != -1)
		RemoveModel(indexToRemove);
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
}

void D3D12HelloTriangle::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record commands.
	// #DXR
	//if (m_raster)
	//{
	//	// #DXR Extra: Perspective Camera
	//	std::vector< ID3D12DescriptorHeap* > heaps = { m_constHeap.Get() };
	//	m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
	//	// set the root descriptor table 0 to the constant buffer descriptor heap
	//	m_commandList->SetGraphicsRootDescriptorTable(
	//		0, m_constHeap->GetGPUDescriptorHandleForHeapStart());
	//	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	//	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	//	//m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	//	//m_commandList->DrawInstanced(3, 1, 0, 0);
	//	//////////m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	//	//////////m_commandList->IASetIndexBuffer(&m_indexBufferView);
	//	//////////m_commandList->DrawIndexedInstanced(12, 1, 0, 0, 0);
	//	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	//	m_commandList->IASetIndexBuffer(&m_indexBufferView);
	//	m_commandList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
	//}
	//else
	{
		//m_instances.clear();
		//for (int i = 0; i < BLASes.size(); i++)
		//{
		//	XMMATRIX scaleMatrix = XMMatrixScaling(Models[i].scale.x, Models[i].scale.y, Models[i].scale.z);
		//	XMMATRIX rotationMatrix =
		//		XMMatrixRotationRollPitchYaw(Models[i].rotation.x, Models[i].rotation.y, Models[i].rotation.z);
		//	XMMATRIX translationMatrix = XMMatrixTranslation(Models[i].position.x, Models[i].position.y, Models[i].position.z);
		//	XMMATRIX transform = scaleMatrix * rotationMatrix * translationMatrix;

		//	m_instances.push_back({
		//		BLASes[i].pResult.Get(),
		//		transform
		//		});
		//}
		//CreateTopLevelAS(m_instances, true);

		BuildTLAS();
		// Get the start of the heap

		ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };

		m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
		// 
		// 
		// On the last frame, the raytracing output was used as a copy source, to
		// copy its contents into the render target. Now we need to transition it to
		// a UAV so that the shaders can write in it.
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		m_commandList->ResourceBarrier(1, &transition);
		// Setup the raytracing task
		D3D12_DISPATCH_RAYS_DESC desc = {};
		// The layout of the SBT is as follows: ray generation shader, miss
		// shaders, hit groups. As described in the CreateShaderBindingTable method,
		// all SBT entries of a given type have the same size to allow a fixed stride.

		// The ray generation shaders are always at the beginning of the SBT. 
		uint32_t rayGenerationSectionSizeInBytes = m_sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;
		// The miss shaders are in the second SBT section, right after the ray
		// generation shader. We have one miss shader for the camera rays and one
		// for the shadow rays, so this section has a size of 2*m_sbtEntrySize. We
		// also indicate the stride between the two miss shaders, which is the size
		// of a SBT entry
		uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
		desc.MissShaderTable.StartAddress =
			m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
		desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
		desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();
		// The hit groups section start after the miss shaders. In this sample we
		// have one 1 hit group for the triangle
		uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() +
			rayGenerationSectionSizeInBytes +
			missSectionSizeInBytes;
		desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
		desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();
		// Dimensions of the image to render, identical to a kernel launch dimension
		desc.Width = GetWidth();
		desc.Height = GetHeight();
		desc.Depth = 1;
		// Bind the raytracing pipeline
		m_commandList->SetPipelineState1(m_rtStateObject.Get());
		// Dispatch the rays and write to the raytracing output
		m_commandList->DispatchRays(&desc);
		// The raytracing output needs to be copied to the actual render target used
		// for display. For this, we need to transition the raytracing output from a
		// UAV to a copy source, and the render target buffer to a copy destination.
		// We can then do the actual copy, before transitioning the render target
		// buffer into a render target, that will be then used to display the image
		transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE);
		m_commandList->ResourceBarrier(1, &transition);
		transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_COPY_DEST);
		m_commandList->ResourceBarrier(1, &transition);


		m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(),
			m_outputResource.Get());


		//for (int i = 0; i < Models.size(); i++)
		{
			m_commandList->CopyResource(m_instancesBuffer.Get(), m_instancesUpload.Get());


			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				m_instancesBuffer.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_GENERIC_READ
			);
			m_commandList->ResourceBarrier(1, &barrier);
		}


		///



		transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &transition);
	}
	// --- IMGUI RENDER START ---
	// 1. Prepare ImGui Draw Data
	ImGui::Render();

	// 2. Set the Descriptor Heap specific to ImGui
	// IMPORTANT: ImGui cannot use the Raytracing Heap. It needs its own.
	ID3D12DescriptorHeap* ppHeaps[] = { m_imguiHeap.Get() };
	m_commandList->SetDescriptorHeaps(1, ppHeaps);

	// 3. Render ImGui to the command list
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
	// --- IMGUI RENDER END ---
	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

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
		// add more keys if needed
	}
}

void D3D12HelloTriangle::OnKeyUp(UINT8 key)
{
	// Alternate between rasterization and raytracing using the spacebar
	if (key == VK_SPACE)
	{
		m_raster = !m_raster;
	}
	switch (key)
	{
	case 'W': keyWDown = false; break;
	case 'S': keySDown = false; break;
	case 'A': keyADown = false; break;
	case 'D': keyDDown = false; break;
	case 'Q': keyQDown = false; break;
	case 'E': keyEDown = false; break;
	}
	/*if (key == 'Q')
	{
		if (currentShading == L"Flat")
			currentShading = L"Normal";
		else
			currentShading = L"Flat";
		CreateShaderBindingTable();
	}*/
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
//////void D3D12HelloTriangle::CreateTopLevelAS(
//////	const std::vector<std::pair<ID3D12Resource*, DirectX::XMMATRIX> >
//////	& instances, bool updateOnly) {
//////	if (!updateOnly)
//////	{
//////		// Gather all the instances into the builder helper
//////		for (size_t i = 0; i < instances.size(); i++) {
//////			m_topLevelASGenerator.AddInstance(instances[i].first,
//////				instances[i].second, static_cast<UINT>(0),
//////				static_cast<UINT>(i));
//////		}
//////
//////		// As for the bottom-level AS, the building the AS requires some scratch space
//////		// to store temporary data in addition to the actual AS. In the case of the
//////		// top-level AS, the instance descriptors also need to be stored in GPU
//////		// memory. This call outputs the memory requirements for each (scratch,
//////		// results, instance descriptors) so that the application can allocate the
//////		// corresponding memory
//////		UINT64 scratchSize, resultSize, instanceDescsSize;
//////
//////		m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize,
//////			&resultSize, &instanceDescsSize);
//////
//////		// Create the scratch and result buffers. Since the build is all done on GPU,
//////		// those can be allocated on the default heap
//////		m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(
//////			m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
//////			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
//////			nv_helpers_dx12::kDefaultHeapProps);
//////		m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(
//////			m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
//////			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
//////			nv_helpers_dx12::kDefaultHeapProps);
//////
//////		// The buffer describing the instances: ID, shader binding information,
//////		// matrices ... Those will be copied into the buffer by the helper through
//////		// mapping, so the buffer has to be allocated on the upload heap.
//////		m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(
//////			m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE,
//////			D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
//////	}
//////	// After all the buffers are allocated, or if only an update is required, we
//////	// can build the acceleration structure. Note that in the case of the update
//////	// we also pass the existing AS as the 'previous' AS, so that it can be
//////	// refitted in place.
//////	m_topLevelASGenerator.Generate(m_commandList.Get(),
//////		m_topLevelASBuffers.pScratch.Get(),
//////		m_topLevelASBuffers.pResult.Get(),
//////		m_topLevelASBuffers.pInstanceDesc.Get(),
//////		updateOnly, m_topLevelASBuffers.pResult.Get());
//////}
void D3D12HelloTriangle::CreateTopLevelAS(
	const std::vector<std::pair<ID3D12Resource*, DirectX::XMMATRIX>>& instances,
	bool updateOnly)
{
	nv_helpers_dx12::TopLevelASGenerator generator;

	for (size_t i = 0; i < instances.size(); i++) {
		generator.AddInstance(instances[i].first,
			instances[i].second, static_cast<UINT>(0),
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
		XMMATRIX scaleMatrix = XMMatrixScaling(Models[i].scale.x, Models[i].scale.y, Models[i].scale.z);
		XMMATRIX rotationMatrix =
			XMMatrixRotationRollPitchYaw(Models[i].rotation.x, Models[i].rotation.y, Models[i].rotation.z);
		XMMATRIX translationMatrix = XMMatrixTranslation(Models[i].position.x, Models[i].position.y, Models[i].position.z);
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

ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateRayGenSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddHeapRangesParameter(
		{ {0 /*u0*/, 1 /*1 descriptor */, 0 /*use the implicit register space 0*/,
		  D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer*/,
		  0 /*heap slot where the UAV is defined*/},
		 {0 /*t0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/, 1},
		 {0 /*b0*/, 1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_CBV /*Camera parameters*/, 2} });

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
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 2 /*b2*/);
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 3 /*t3*/);
	return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// The miss shader communicates only through the ray payload, and therefore
// does not require any resources
//
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMissSignature() {
	nv_helpers_dx12::RootSignatureGenerator rsc;
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
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Miss.hlsl");
	m_flatShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"FlatShader.hlsl");
	m_normalShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"NormalShader.hlsl");
	m_phongShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"PhongShader.hlsl");
	m_mirrorDemoShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"MirrorDemoShader.hlsl");
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
		pipeline.AddHitGroup(FlatHitGroup, L"ClosestHit_Flat");
		pipeline.AddHitGroup(NormalHitGroup, L"ClosestHit_Normal");
		pipeline.AddHitGroup(PhongHitGroup, L"ClosestHit_Phong");
		pipeline.AddHitGroup(MirrorDemoHitGroup, L"ClosestHit_MirrorDemo");

		hitGroups.push_back(FlatHitGroup.c_str());
		hitGroups.push_back(NormalHitGroup.c_str());
		hitGroups.push_back(PhongHitGroup.c_str());
		hitGroups.push_back(MirrorDemoHitGroup.c_str());

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
	pipeline.SetMaxPayloadSize(4 * sizeof(float)+2*sizeof(uint32_t)); // RGB + distance

	// Upon hitting a surface, DXR can provide several attributes to the hit. In
	// our sample we just use the barycentric coordinates defined by the weights
	// u,v of the last two vertices of the triangle. The actual barycentrics can
	// be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

	// The raytracing process can shoot rays from existing hit points, resulting
	// in nested TraceRay calls. Our sample code traces only primary rays, which
	// then requires a trace depth of 1. Note that this recursion depth should be
	// kept to a minimum for best performance. Path tracing algorithms can be
	// easily flattened into a simple loop in the ray generation.
	pipeline.SetMaxRecursionDepth(3);
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
void D3D12HelloTriangle::CreateShaderResourceHeap() {
	//// Create a SRV/UAV/CBV descriptor heap. We need 2 entries - 1 UAV for the
	//// raytracing output and 1 SRV for the TLAS
	//m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
	//	m_device.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
	// #DXR Extra: Perspective Camera
	// Create a SRV/UAV/CBV descriptor heap. We need 3 entries - 1 SRV for the TLAS, 1 UAV for the
	// raytracing output and 1 CBV for the camera matrices
	UINT descriptorCount = 3 + Models.size();
	m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
		m_device.Get(), descriptorCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	UINT incSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Get a handle to the heap memory on the CPU side, to be able to write the
	// descriptors directly
	// Start of heap
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

	// Create the UAV. Based on the root signature we created it is the first
	// entry. The Create*View methods write the view information directly into
	// srvHandle
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView(
        m_outputResource.Get(), nullptr, &uavDesc, handle);

	// Add the Top Level AS SRV right after the raytracing output buffer
	handle.ptr += incSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC tlasSrvDesc = {};
    tlasSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    tlasSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    tlasSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    tlasSrvDesc.RaytracingAccelerationStructure.Location =
        m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

    m_device->CreateShaderResourceView(nullptr, &tlasSrvDesc, handle);

	// #DXR Extra: Perspective Camera
	// Add the constant buffer for the camera after the TLAS
	//srvHandle.ptr +=
	//	m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Describe and create a constant buffer view for the camera
	handle.ptr += incSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes   = m_cameraBufferSize;
    m_device->CreateConstantBufferView(&cbvDesc, handle);

    // --- [3..] Instance buffer SRVs (one per model) ---
    handle.ptr += incSize; // now handle points to slot index 3

///
	//for (int i = 0; i < Models.size(); i++)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC instSrvDesc = {};
        instSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        instSrvDesc.Buffer.FirstElement = 0;
        instSrvDesc.Buffer.NumElements = (UINT)ModelsShaderData.size(); // or per-model count
        instSrvDesc.Buffer.StructureByteStride = sizeof(ModelInstanceGPU);
        instSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        instSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        m_device->CreateShaderResourceView(
            m_instancesBuffer.Get(),
            &instSrvDesc,
            handle);

        // Advance to the next descriptor slot
        handle.ptr += incSize;

	}
}

//-----------------------------------------------------------------------------
//
// The Shader Binding Table (SBT) is the cornerstone of the raytracing setup:
// this is where the shader resources are bound to the shaders, in a way that
// can be interpreted by the raytracer on GPU. In terms of layout, the SBT
// contains a series of shader IDs with their resource pointers. The SBT
// contains the ray generation shader, the miss shaders, then the hit groups.
// Using the helper class, those can be specified in arbitrary order.
//void D3D12HelloTriangle::CreateShaderBindingTable() {
//	// The SBT helper class collects calls to Add*Program.  If called several
//	// times, the helper must be emptied before re-adding shaders.
//	m_sbtHelper.Reset();
//
//	// The pointer to the beginning of the heap is the only parameter required by
//	// shaders without root parameters
//	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
//		m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
//	// The helper treats both root parameter pointers and heap pointers as void*,
//	// while DX12 uses the
//	// D3D12_GPU_DESCRIPTOR_HANDLE to define heap pointers. The pointer in this
//	// struct is a UINT64, which then has to be reinterpreted as a pointer.
//	//auto heapPointer = reinterpret_cast<UINT64>(srvUavHeapHandle.ptr);
//	//// The ray generation only uses heap data
//	//m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });
//	auto heapPointer = reinterpret_cast<void*>(srvUavHeapHandle.ptr);
//	m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });
//
//	// The miss and hit shaders do not access any external resources: instead they
//	// communicate their results through the ray payload
//	m_sbtHelper.AddMissProgram(L"Miss", {});
//
//	// Adding the triangle hit shader
//	//m_sbtHelper.AddHitGroup(L"HitGroup", { (void*)(m_vertexBuffer->GetGPUVirtualAddress()) });
//
//	D3D12_SHADER_RESOURCE_VIEW_DESC tlasSrvDesc = {};
//	tlasSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
//	tlasSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
//	tlasSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
//	tlasSrvDesc.RaytracingAccelerationStructure.Location =
//		m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
//
////	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle1 = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
//	//void* heapPointer = reinterpret_cast<void*>(srvUavHeapHandle.ptr);
//
//
//	for (int i = 0; i < Models.size(); i++)
//	{
//		std::wstring hitGroupName = L"HitGroup_" + currentShading + L"_" + std::to_wstring(i);
//
//		//m_sbtHelper.AddHitGroup(hitGroupName.c_str(), { &hitDataVec.back() });
//		m_sbtHelper.AddHitGroup(hitGroupName.c_str(), { (void*)(Models[i].m_vertexBuffer->GetGPUVirtualAddress()),
//			(void*)(Models[i].m_indexBuffer->GetGPUVirtualAddress()),
//			(void*)(m_instancesBuffer->GetGPUVirtualAddress()),
//			(void*)(m_lightsBuffer->GetGPUVirtualAddress()),
//			(void*)(m_topLevelASBuffers.pResult->GetGPUVirtualAddress())});
//	}
//	
//	// Compute the size of the SBT given the number of shaders and their
//	// parameters
//	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();
//
//	// Create the SBT on the upload heap. This is required as the helper will use
//	// mapping to write the SBT contents. After the SBT compilation it could be
//	// copied to the default heap for performance.
//	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
//		m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
//		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
//	if (!m_sbtStorage) {
//		throw std::logic_error("Could not allocate the shader binding table");
//	}
//	// Compile the SBT from the shader and parameters info
//	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
//}

//void D3D12HelloTriangle::CreateShaderBindingTable() {
//	// Reset helper
//	m_sbtHelper.Reset();
//
//	// Get GPU descriptor heap pointer for raygen if raygen uses descriptor table.
//	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
//	void* heapPointer = reinterpret_cast<void*>(srvUavHeapHandle.ptr);
//
//	// RayGen uses descriptor-table pointer (as before)
//	m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });
//
//	// Miss program
//	m_sbtHelper.AddMissProgram(L"Miss", {});
//
//	// Helper to convert GPU virtual addresses to void* for SBT parameters.
//	auto GPUVaToVoidPtr = [](D3D12_GPU_VIRTUAL_ADDRESS va) -> void* {
//		return reinterpret_cast<void*>(static_cast<uintptr_t>(va));
//		};
//
//	// Ensure TLAS is available
//	if (!m_topLevelASBuffers.pResult) {
//		throw std::logic_error("TLAS not built before creating SBT");
//	}
//
//	// Add one hit group per model; match names with pipeline (HitGroup_<Shading>_<index>)
//	for (int i = 0; i < static_cast<int>(Models.size()); ++i) {
//		std::wstring hitGroupName = L"HitGroup_" + currentShading + L"_" + std::to_wstring(i);
//
//		// For a hit signature that declares SRV root parameters (t0,t1,t2,t3),
//		// pass GPU virtual addresses (vertex/index/instances/lights/TLAS).
//		void* v0_va = GPUVaToVoidPtr(Models[i].m_vertexBuffer->GetGPUVirtualAddress());
//		void* idx_va = GPUVaToVoidPtr(Models[i].m_indexBuffer->GetGPUVirtualAddress());
//		void* instances_va = GPUVaToVoidPtr(m_instancesBuffer->GetGPUVirtualAddress());
//		void* lights_va = GPUVaToVoidPtr(m_lightsBuffer->GetGPUVirtualAddress());
//		void* tlas_va = GPUVaToVoidPtr(m_topLevelASBuffers.pResult->GetGPUVirtualAddress());
//
//		m_sbtHelper.AddHitGroup(hitGroupName.c_str(), {
//			v0_va,
//			idx_va,
//			instances_va,
//			lights_va,
//			tlas_va
//			});
//	}
//
//	// Create SBT storage
//	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();
//	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
//		m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
//		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
//	if (!m_sbtStorage) {
//		throw std::logic_error("Could not allocate the shader binding table");
//	}
//	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
//}

void D3D12HelloTriangle::CreateShaderBindingTable() {
	// 1. Reset the helper
	m_sbtHelper.Reset();

	// 2. Add Ray Generation Program
	// This shader only needs the heap pointer (global resources)
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
	auto heapPointer = reinterpret_cast<void*>(srvUavHeapHandle.ptr);
	m_sbtHelper.AddRayGenerationProgram(L"RayGen", { heapPointer });

	// 3. Add Miss Program
	m_sbtHelper.AddMissProgram(L"Miss", {});

	// 4. Prepare TLAS GPU Address
	// CRITICAL FIX: We need the GPU Virtual Address of the result buffer, NOT the descriptor struct.
	void* tlasAddress = (void*)m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

	// 5. Add Hit Groups for every model
	for (int i = 0; i < Models.size(); i++)
	{
		// Construct the hit group name based on current shading mode
		std::wstring hitGroupName = L"HitGroup_" + currentShading + L"_" + std::to_wstring(i);

		// Define the pointers for the Hit Shader arguments.
		// These MUST match the order defined in CreateHitSignature EXACTLY.

		// Slot 0: t0 (Vertices)
		void* vertexBufferAddr = (void*)(Models[i].m_vertexBuffer->GetGPUVirtualAddress());

		// Slot 1: t1 (Indices)
		void* indexBufferAddr = (void*)(Models[i].m_indexBuffer->GetGPUVirtualAddress());

		// Slot 2: t2 (Instance Data / ModelInstanceGPU)
		void* instanceBufferAddr = (void*)(m_instancesBuffer->GetGPUVirtualAddress());

		// Slot 3: b1 (Lights)
		void* lightsBufferAddr = (void*)(m_lightsBuffer->GetGPUVirtualAddress());

		// Slot 4: b2 (Extra Buffer) 
		// You said this is needed for another shader. We MUST fill this slot.
		// If the Mirror shader doesn't use it, we can pass the lights buffer again or nullptr.
		// If it DOES use it, replace 'lightsBufferAddr' below with the correct buffer.
		void* b2BufferPlaceholder = lightsBufferAddr;

		// Slot 5: t3 (TLAS)
		// This is the one that was failing before because the slots were misaligned.
		void* tlasBufferAddr = tlasAddress;

		// Add to SBT
		m_sbtHelper.AddHitGroup(hitGroupName.c_str(), {
			vertexBufferAddr,       // t0
			indexBufferAddr,        // t1
			instanceBufferAddr,     // t2
			lightsBufferAddr,       // b1
			b2BufferPlaceholder,    // b2 (The slot you requested)
			tlasBufferAddr          // t3 (Now correctly aligned!)
			});
	}

	// 6. Compute Size and Allocate
	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	if (!m_sbtStorage) {
		throw std::logic_error("Could not allocate the shader binding table");
	}

	// 7. Generate SBT on GPU
	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
}

//----------------------------------------------------------------------------------
//
// The camera buffer is a constant buffer that stores the transform matrices of
// the camera, for use by both the rasterization and raytracing. This method
// allocates the buffer where the matrices will be copied. For the sake of code
// clarity, it also creates a heap containing only this buffer, to use in the
// rasterization path.
//
// #DXR Extra: Perspective Camera
void D3D12HelloTriangle::CreateCameraBuffer() {
	uint32_t nbMatrix = 4; // view, perspective, viewInv, perspectiveInv
	m_cameraBufferSize = nbMatrix * sizeof(XMMATRIX);

	// Create the constant buffer for all matrices
	m_cameraBuffer = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), m_cameraBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	// Create a descriptor heap that will be used by the rasterization shaders //TODO: remove RAST
	m_constHeap = nv_helpers_dx12::CreateDescriptorHeap(
		m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	// Describe and create the constant buffer view.
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize;

	// Get a handle to the heap memory on the CPU side, to be able to write the
	// descriptors directly
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
		m_constHeap->GetCPUDescriptorHandleForHeapStart();
	m_device->CreateConstantBufferView(&cbvDesc, srvHandle);
}

void D3D12HelloTriangle::CreateLightsBuffer() {
	m_lightsBufferSize = sizeof(LightData);
	m_lightsBuffer = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), m_lightsBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	// Copy CPU memory to GPU
	uint8_t* pData;
	ThrowIfFailed(m_lightsBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, &m_lightData, sizeof(LightData));
	m_lightsBuffer->Unmap(0, nullptr);
}





void D3D12HelloTriangle::CreateModelDataBuffer()
{

	//for (int i = 0; i < Models.size(); i++)
	{

		UINT bufferSize = sizeof(ModelInstanceGPU) * ModelsShaderData.size();

		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);



		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapDefault,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&(m_instancesBuffer)))
		);

		// Upload heap
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapUpload,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&(m_instancesUpload)))
		);

		UpdateModelDataBuffer();
	}

}

void D3D12HelloTriangle::UpdateModelDataBuffer() {

	UINT bufferSize = sizeof(ModelInstanceGPU) * ModelsShaderData.size();
	void* mapped = nullptr;
	m_instancesUpload->Map(0, nullptr, &mapped);
	memcpy(mapped, ModelsShaderData.data(), bufferSize);
	m_instancesUpload->Unmap(0, nullptr);



}


// #DXR Extra: Perspective Camera
//--------------------------------------------------------------------------------
// Create and copies the viewmodel and perspective matrices of the camera
//
void D3D12HelloTriangle::UpdateCameraBuffer() {
	using namespace nv_helpers_dx12;
	Manipulator& manip = CameraManip;
	static DWORD lastTime = GetTickCount();
	DWORD now = GetTickCount();
	float deltaTime = (now - lastTime) / 1000.0f; // seconds
	lastTime = now;
	std::vector<XMMATRIX> matrices(4);
	// 
	// Initialize the view matrix, ideally this should be based on user
	// interactions The lookat and perspective matrices used for rasterization are
	// defined to transform world-space vertices into a [0,1]x[0,1]x[0,1] camera
	// space
	XMVECTOR Eye = XMVectorSet(1.5f, 1.5f, 1.5f, 0.0f);
	XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	//matrices[0] = XMMatrixLookAtRH(Eye, At, Up);
	const glm::mat4& mat = nv_helpers_dx12::CameraManip.getMatrix();
	memcpy(&matrices[0].r->m128_f32[0], glm::value_ptr(mat), 16 * sizeof(float));

	float fovAngleY = 45.0f * XM_PI / 180.0f;
	matrices[1] =
		XMMatrixPerspectiveFovRH(fovAngleY, m_aspectRatio, 0.1f, 1000.0f);

	// Raytracing has to do the contrary of rasterization: rays are defined in
	// camera space, and are transformed into world space. To do this, we need to
	// store the inverse matrices as well.
	XMVECTOR det;
	matrices[2] = XMMatrixInverse(&det, matrices[0]);
	matrices[3] = XMMatrixInverse(&det, matrices[1]);

	// Copy the matrix contents
	uint8_t* pData;
	ThrowIfFailed(m_cameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, matrices.data(), m_cameraBufferSize);
	m_cameraBuffer->Unmap(0, nullptr);
	// --- 2. Keyboard movement ---
	glm::vec3 eye, center, up;
	manip.getLookat(eye, center, up);

//=======
	glm::vec3 forward = glm::normalize(center - eye);
	glm::vec3 right = glm::normalize(glm::cross(forward, up));

	float speed = manip.getSpeed();

	if (keyWDown) eye += forward * speed * deltaTime;
	if (keySDown) eye -= forward * speed * deltaTime;
	if (keyADown) eye -= right * speed * deltaTime;
	if (keyDDown) eye += right * speed * deltaTime;
	if (keyQDown) eye -= up * speed * deltaTime;
	if (keyEDown) eye += up * speed * deltaTime;

	// Update manipulator with new position
	manip.setLookat(eye, eye + forward, up);

	// --- 3. Retrieve view matrix for raytracing ---
	glm::mat4 viewMatrix = manip.getMatrix();
	// Use viewMatrix for ray generation

}


//>>>>>>> d39e81118fd7890f397c9ccb920fcc011bd96d50
void D3D12HelloTriangle::LoadModel(const std::string& modelPath,
	std::vector<Vertex>& outVertices,
	std::vector<uint32_t>& outIndices)
{
	static int modelId = -1;
	modelId++;
	Assimp::Importer importer;

	const aiScene* scene = importer.ReadFile(modelPath,
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_GenNormals |
		aiProcess_JoinIdenticalVertices
	);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		return;
	}

	for (UINT m = 0; m < scene->mNumMeshes; ++m)
	{
		aiMesh* mesh = scene->mMeshes[m];
		aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
		aiColor4D diffuseColor(1.0f, 1.0f, 1.0f, 1.0f); // Default to white
		material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor);

		// Convert from Assimp's color to DirectX's XMFLOAT4
		DirectX::XMFLOAT4 meshColor = { diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a };

		UINT vertexOffset = (UINT)outVertices.size();

		for (UINT i = 0; i < mesh->mNumVertices; ++i)
		{
			Vertex v;
			v.position.x = mesh->mVertices[i].x;
			v.position.y = mesh->mVertices[i].y;
			v.position.z = mesh->mVertices[i].z;

			v.color = meshColor;
			v.id = modelId; // Assign mesh index as ID

			if (mesh->HasNormals())
			{
				v.normal.x = mesh->mNormals[i].x;
				v.normal.y = mesh->mNormals[i].y;
				v.normal.z = mesh->mNormals[i].z;
			}
			else
			{
				v.normal = { 0.0f, 1.0f, 0.0f };
			}

			outVertices.push_back(v);
		}

		for (UINT i = 0; i < mesh->mNumFaces; ++i)
		{
			aiFace face = mesh->mFaces[i];
			for (UINT j = 0; j < face.mNumIndices; ++j)
			{
				outIndices.push_back(face.mIndices[j] + vertexOffset);
			}
		}
	}
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

void D3D12HelloTriangle::UpdateLightsBuffer()
{
	uint8_t* pData;
	ThrowIfFailed(m_lightsBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, &m_lightData, sizeof(LightData));
	m_lightsBuffer->Unmap(0, nullptr);
}


void D3D12HelloTriangle::BuildTLAS() {
	m_instances.clear();

	for (size_t i = 0; i < BLASes.size(); i++)
	{
		XMMATRIX scaleMatrix = XMMatrixScaling(Models[i].scale.x, Models[i].scale.y, Models[i].scale.z);
		XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(Models[i].rotation.x, Models[i].rotation.y, Models[i].rotation.z);
		XMMATRIX translationMatrix = XMMatrixTranslation(Models[i].position.x, Models[i].position.y, Models[i].position.z);
		XMMATRIX transform = scaleMatrix * rotationMatrix * translationMatrix;

		m_instances.push_back({
			BLASes[i].pResult.Get(),
			transform
			});
	}

	CreateTopLevelAS(m_instances, !BLASChanged);
	if (BLASChanged == true) {
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

		srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

		m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
		BLASChanged = false;
	}
}

void D3D12HelloTriangle::AddModel(const std::string& path) {
	WaitForPreviousFrame();

	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	ModelInstance newModel = {};
	newModel.id = static_cast<int>(Models.size());
	LoadModel(path, newModel.vertices, newModel.indices);
	const UINT vertexBufferSize = static_cast<UINT>(newModel.vertices.size()) * sizeof(Vertex);
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&(newModel.m_vertexBuffer))));

	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	ThrowIfFailed(newModel.m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, newModel.vertices.data(), vertexBufferSize);
	newModel.m_vertexBuffer->Unmap(0, nullptr);

	const UINT indexBufferSize = static_cast<UINT>(newModel.indices.size()) * sizeof(uint32_t);
	ThrowIfFailed(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize), D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&(newModel.m_indexBuffer))));

	UINT8* pIndexDataBegin;
	ThrowIfFailed(newModel.m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, newModel.indices.data(), indexBufferSize);
	newModel.m_indexBuffer->Unmap(0, nullptr);

	AccelerationStructureBuffers newBLAS = CreateBottomLevelAS(
		{ {newModel.m_vertexBuffer.Get(), (uint32_t)newModel.vertices.size()} },
		{ {newModel.m_indexBuffer.Get(),  (uint32_t)newModel.indices.size()} }
	);

	BLASes.push_back(newBLAS);
	m_bottomLevelAS.clear();
	for (auto& b : BLASes) m_bottomLevelAS.push_back(b.pResult);
	Models.push_back(newModel);

	CreateRaytracingPipeline();
	CreateShaderBindingTable();

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

	WaitForPreviousFrame();
	BLASChanged = true;
}

void D3D12HelloTriangle::RemoveModel(int index) {

	if (index < 0 || index >= Models.size()) return;

	WaitForPreviousFrame();


	Models.erase(Models.begin() + index);
	for (int i = 0; i < Models.size(); i++)
	{
		Models[i].id = i;
	}
	BLASes.erase(BLASes.begin() + index);
	BLASChanged = true;
	m_bottomLevelAS.clear();
	for (auto& b : BLASes) m_bottomLevelAS.push_back(b.pResult);

	CreateRaytracingPipeline();
	CreateShaderBindingTable();
	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

	WaitForPreviousFrame();
}