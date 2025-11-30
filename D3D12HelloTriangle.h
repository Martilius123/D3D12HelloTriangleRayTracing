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

#pragma once

#include "DXSample.h"
#include <dxcapi.h>
#include <d3d12.h>
#include <array>
#include <stdexcept>
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include <string>

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class D3D12HelloTriangle : public DXSample
{
public:
	D3D12HelloTriangle(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

private:
	ComPtr<ID3D12DescriptorHeap> m_imguiHeap;
	static const UINT FrameCount = 2;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
		XMFLOAT3 normal;
	};

	struct ModelInstance
	{
		int id;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		// App resources.
		ComPtr<ID3D12Resource> m_vertexBuffer;
		D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

		// 3D Model
		ComPtr<ID3D12Resource> m_indexBuffer;
		D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

		DirectX::XMFLOAT3 position = { 0, 0, 0 };
		DirectX::XMFLOAT3 rotation = { 0, 0, 0 }; // pitch, yaw, roll
		DirectX::XMFLOAT3 scale = { 1, 1, 1 };

		//DirectX::XMFLOAT4X4 worldMatrix; // computed per frame
		DirectX::XMMATRIX worldMatrix;
	};

	std::vector<ModelInstance> Models;

	struct ModelInstanceGPU
	{
		int id;
		//DirectX::XMFLOAT4X4 worldMatrix;
		DirectX::XMMATRIX worldMatrix;
	};

	std::vector<ModelInstanceGPU> ModelsShaderData;
	
	struct LightData {
		XMFLOAT3 position; float pad1;
		XMFLOAT3 color;    float pad2;
	};

	// #DXR Extra: Perspective Camera
	void CreateCameraBuffer();
	void UpdateCameraBuffer();
	void CreateLightsBuffer();
	void UpdateLightsBuffer();
	ComPtr< ID3D12Resource > m_cameraBuffer;
	LightData m_lightData;
	ComPtr< ID3D12Resource > m_lightsBuffer;
	ComPtr< ID3D12DescriptorHeap > m_constHeap;
	uint32_t m_cameraBufferSize = 0;
	uint32_t m_lightsBufferSize = 0;

	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device5> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
	UINT m_rtvDescriptorSize;

	


	// Test
	UINT IndexCount;
	UINT VertexCount;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();
	void CheckRaytracingSupport();
	virtual void OnKeyUp(UINT8 key);
	virtual void OnKeyDown(UINT8 key);
	bool m_raster = false;
	std::wstring currentShading = L"Flat";
	// #DXR
	struct AccelerationStructureBuffers
	{
		ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		ComPtr<ID3D12Resource> pResult;       // Where the AS is
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};
	std::vector<ComPtr<ID3D12Resource> > m_bottomLevelAS; // Storage for the bottom Level AS

	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
	AccelerationStructureBuffers m_topLevelASBuffers;
	std::vector<std::pair<ID3D12Resource*, DirectX::XMMATRIX> > m_instances;
	std::vector<AccelerationStructureBuffers> BLASes;

AccelerationStructureBuffers CreateBottomLevelAS(
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t> > vVertexBuffers,
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t> > vIndexBuffers =
	{});

void CreateTopLevelAS(const std::vector<std::pair<ID3D12Resource*, DirectX::XMMATRIX> >
	& instances,bool updateOnly = false);

void CreateAccelerationStructures();

// #DXR additions

// Methods to create root signatures and pipeline
ComPtr<ID3D12RootSignature> CreateRayGenSignature();
ComPtr<ID3D12RootSignature> CreateMissSignature();
ComPtr<ID3D12RootSignature> CreateHitSignature();

void CreateRaytracingPipeline();

// Shader libraries (compiled DXIL)
ComPtr<IDxcBlob> m_rayGenLibrary;
ComPtr<IDxcBlob> m_missLibrary;
// Different Hit Shaders:
ComPtr<IDxcBlob> m_flatShaderLibrary;
ComPtr<IDxcBlob> m_normalShaderLibrary;
ComPtr<IDxcBlob> m_phongShaderLibrary;

// Root signatures for each shader stage
ComPtr<ID3D12RootSignature> m_rayGenSignature;
ComPtr<ID3D12RootSignature> m_hitSignature;
ComPtr<ID3D12RootSignature> m_missSignature;

// Ray tracing pipeline state
ComPtr<ID3D12StateObject> m_rtStateObject;
// Pipeline state properties (used to query shader identifiers)
ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

// #DXR
void CreateRaytracingOutputBuffer();
void CreateShaderResourceHeap();
ComPtr<ID3D12Resource> m_outputResource;
ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
// #DXR
void CreateShaderBindingTable();
nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
ComPtr<ID3D12Resource> m_sbtStorage;

void D3D12HelloTriangle::LoadModel(const std::string& modelPath,
	std::vector<Vertex>& outVertices,
	std::vector<uint32_t>& outIndices);

// #DXR Extra: Perspective Camera++
void OnButtonDown(UINT32 lParam);
void OnMouseMove(UINT8 wParam, UINT32 lParam);
//for changing shading mode
public:
	void D3D12HelloTriangle::SetShadingMode(const std::wstring& mode);


	bool keyWDown = false;
	bool keySDown = false;
	bool keyADown = false;
	bool keyDDown = false;
	bool keyQDown = false;
	bool keyEDown = false;

};

