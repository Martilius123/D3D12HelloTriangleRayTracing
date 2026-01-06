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
	static char modelPathBuffer[128] = "Models/Cube.obj";
	static char environmentPathBuffer[128] = "HDR/studio.hdr";
	ImGui::InputText("Model Path", modelPathBuffer, _countof(modelPathBuffer));

	if (ImGui::Button("Add Model")) {
		// This triggers the heavy function we wrote earlier
		AddModel(modelPathBuffer);
	}
	
	ImGui::Separator();
	ImGui::Text("Camera Parameters");

	ImGui::DragInt("ISO", (int*) & m_ISOIndex, 100, 100, 1600);
	ImGui::Checkbox("Highlight Overexposed", &m_highlightOverexposed);

	ImGui::Separator();


	// Example: Dropdown for shaders
	const char* items[] = { "Flat", "Normal", "Phong", "MirrorDemo", "BDSF"};
	static int item_current = 0;
	// Sync current selection with your std::wstring currentShading
	if (currentShading == L"Flat") item_current = 0;
	else if (currentShading == L"Normal") item_current = 1;
	else if (currentShading == L"Phong") item_current = 2;
	else if (currentShading == L"MirrorDemo") item_current = 3;
	else if (currentShading == L"BDSF") item_current = 4;

	if (ImGui::Combo("Shading Mode", &item_current, items, IM_ARRAYSIZE(items))) {
		if (item_current == 0) SetShadingMode(L"Flat");
		if (item_current == 1) SetShadingMode(L"Normal");
		if (item_current == 2) SetShadingMode(L"Phong");
		if (item_current == 3) SetShadingMode(L"MirrorDemo");
		if (item_current == 4) SetShadingMode(L"BDSF");
	}
	if (currentShading == L"BDSF")
	{
		ImGui::Separator();
		ImGui::Text("BDSF Parameters");
		ImGui::DragInt("Sample Count", (int*) & m_sampleCount, 1, 1, 20);
		ImGui::InputText("HDR Path", environmentPathBuffer, _countof(environmentPathBuffer));

		if (ImGui::Button("Change Environment")) {
			// This triggers the heavy function we wrote earlier
			CreateEnvironmentTexture(LoadHDR(environmentPathBuffer));
		}
	}
	if (currentShading == L"Phong" || currentShading == L"MirrorDemo" || currentShading == L"BDSF")
	{
		ImGui::Separator();
		ImGui::Text("Light Parameters");

		bool lightChanged = false;

		const char* lightTypes[] = { "Point Light", "Directional Light" };

		if (ImGui::Combo("Light Type", &m_lightData.type, lightTypes, IM_ARRAYSIZE(lightTypes)))
		{
			lightChanged = true;
		}

		const char* posLabel = (m_lightData.type == 1) ? "Light Direction" : "Light Position";

		if (ImGui::DragFloat3(posLabel, &m_lightData.position.x, 0.1f))
			lightChanged = true;

		if (ImGui::ColorEdit3("Light Color", &m_lightData.color.x))
			lightChanged = true;

		if (ImGui::DragFloat("Intensity", &m_lightData.intensity, 0.1f, 0.0f, 1000.0f))
			lightChanged = true;

		if (lightChanged)
		{
			UpdateLightsBuffer();
		}
	}
	ImGui::Separator();

	int indexToRemove = -1; 

	for (int i = 0; i < Models.size(); i++) {
		auto& inst = Models[i];
		auto& inst2 = ModelsShaderData[i];

		ImGui::PushID(i);


		if (ImGui::CollapsingHeader((std::to_string(i) + ": " + std::to_string(inst.id)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

			if (ImGui::DragFloat3("Position", &inst.position.x, 0.05f));
			if (ImGui::DragFloat3("Rotation", &inst.rotation.x, 0.05f));
			if (ImGui::DragFloat3("Scale", &inst.scale.x, 0.05f, 0, 100));

			ImGui::Spacing();

			// --- NEW: Nested Header for GPU Instance Data ---
			if (ImGui::TreeNode("Material / Instance Settings")) {
				// COLOR
				{
					bool useVertexData = (inst2.albedo.x == -1.0f &&
						inst2.albedo.y == -1.0f &&
						inst2.albedo.z == -1.0f);

					if (ImGui::Checkbox("Use Vertex Data", &useVertexData)) {
						if (useVertexData) {
							inst2.albedo = DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f);
						}
						else {
							inst2.albedo = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
						}
					}

					if (!useVertexData) {
						ImGui::Indent();
						ImGui::ColorEdit3("Instance Color", &inst2.albedo.x);
						ImGui::Unindent();
					}
				}
				// EMISSION
				{
					bool useTextureEmission = (inst2.emission == -1.0f);
					if (ImGui::Checkbox("Use Texture Emission", &useTextureEmission)) {
						if (useTextureEmission) {
							inst2.emission = -1.0f;
						}
						else {
							inst2.emission = 1.0f;
						}
					}
					if (!useTextureEmission) {
						ImGui::Indent();
						ImGui::DragFloat("Emission Value", &inst2.emission, 0.01f, 0.0f, 10.0f);
						ImGui::Unindent();
					}
				}
				ImGui::Spacing();
				// ROUGHNESS
				{
					bool useTextureRoughness = (inst2.roughness == -1.0f);
					if (ImGui::Checkbox("Use Texture Roughness", &useTextureRoughness)) {
						if (useTextureRoughness) {
							inst2.roughness = -1.0f;
						}
						else {
							inst2.roughness = 0.4f;
						}
					}
					if (!useTextureRoughness) {
						ImGui::Indent();
						ImGui::DragFloat("Roughness Value", &inst2.roughness, 0.01f, 0.0f, 1.0f);
						ImGui::Unindent();
					}
				}
				ImGui::TreePop();
			}

			ImGui::Separator();

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
	{

		BuildTLAS();
		// Get the start of the heap

		ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get(), m_samplerHeap.Get() };

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

		
		//ID3D12DescriptorHeap heaps = {m_srvUavHeap.Get(), m_samplerHeap.Get()};
		m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
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
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Miss.hlsl");
	m_flatShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"FlatShader.hlsl");
	m_normalShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"NormalShader.hlsl");
	m_phongShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"PhongShader.hlsl");
	m_mirrorDemoShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"MirrorDemoShader.hlsl");
	m_BDSFShaderLibrary = nv_helpers_dx12::CompileShaderLibrary(L"BDSFShader.hlsl");
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
	pipeline.AddLibrary(m_BDSFShaderLibrary.Get(), { L"ClosestHit_BDSF" });
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
		std::wstring BDSFHitGroup = L"HitGroup_BDSF_" + std::to_wstring(i);
		pipeline.AddHitGroup(FlatHitGroup, L"ClosestHit_Flat");
		pipeline.AddHitGroup(NormalHitGroup, L"ClosestHit_Normal");
		pipeline.AddHitGroup(PhongHitGroup, L"ClosestHit_Phong");
		pipeline.AddHitGroup(MirrorDemoHitGroup, L"ClosestHit_MirrorDemo");
		pipeline.AddHitGroup(BDSFHitGroup, L"ClosestHit_BDSF");
		hitGroups.push_back(FlatHitGroup.c_str());
		hitGroups.push_back(NormalHitGroup.c_str());
		hitGroups.push_back(PhongHitGroup.c_str());
		hitGroups.push_back(MirrorDemoHitGroup.c_str());
		hitGroups.push_back(BDSFHitGroup.c_str());
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
	pipeline.SetMaxPayloadSize(4 * sizeof(float)+sizeof(int) + 2 * sizeof(uint32_t)); // RGB + distance

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
	pipeline.SetMaxRecursionDepth(4);
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
	// Reserve slots: 0 = UAV(output), 1 = TLAS SRV, 2 = Camera CBV, 3..(3+N-1) = instance SRVs, last = env SRV
	UINT descriptorCount = 4 + static_cast<UINT>(Models.size()); // <- poprawione (dodatkowy slot na env)
	m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
		m_device.Get(), descriptorCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	UINT incSize = m_device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Start of heap (CPU handle)
	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart());

	// Slot 0: UAV for raytracing output
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc, handle);

	// Slot 1: TLAS SRV
	handle.Offset(1, incSize);
	D3D12_SHADER_RESOURCE_VIEW_DESC tlasSrvDesc = {};
	tlasSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	tlasSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	tlasSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	tlasSrvDesc.RaytracingAccelerationStructure.Location =
		m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
	m_device->CreateShaderResourceView(nullptr, &tlasSrvDesc, handle);

	// Slot 2: Camera CBV
	handle.Offset(1, incSize);
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize;
	m_device->CreateConstantBufferView(&cbvDesc, handle);

	// Slot 3..: instance buffer SRVs (one per model slot)
	handle.Offset(1, incSize); // now points to slot index 3
	for (size_t i = 0; i < Models.size(); ++i) {
		D3D12_SHADER_RESOURCE_VIEW_DESC instSrvDesc = {};
		instSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		instSrvDesc.Buffer.FirstElement = 0;
		instSrvDesc.Buffer.NumElements = static_cast<UINT>(ModelsShaderData.size());
		instSrvDesc.Buffer.StructureByteStride = sizeof(ModelInstanceGPU);
		instSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		instSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_device->CreateShaderResourceView(
			m_instancesBuffer.Get(),
			&instSrvDesc,
			handle);

		// next slot
		handle.Offset(1, incSize);
	}

	// Current handle now points to the next free slot -> use it for environment SRV
	m_envSrvIndex = 3 + static_cast<UINT>(Models.size());

	D3D12_SHADER_RESOURCE_VIEW_DESC envSrv = {};
	envSrv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	envSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	envSrv.Texture2D.MipLevels = 1;
	envSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	m_device->CreateShaderResourceView(
		m_envTexture.Get(),
		&envSrv,
		handle
	);

	// Create shader-visible sampler heap (1 sampler)
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