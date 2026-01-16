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

#include <windows.h>
#include "d3dx12.h"
#include "DXSampleHelper.h"
#include <dxcapi.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <array>
#include <stdexcept>
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include <string>
#include "DXSample.h"
#include "NRDIntegration.h"

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




public:




	// ---------------- NRD runtime state ----------------
	uint32_t m_nrdFrameIndex = 0;

	// Previous frame matrices for NRD
	DirectX::XMMATRIX m_prevWorldToView = DirectX::XMMatrixIdentity();
	DirectX::XMMATRIX m_prevViewToClip = DirectX::XMMatrixIdentity();

	// NRD descriptor pool (SRV+UAV in one shader-visible heap)
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_nrdSrvUavHeap;
	uint32_t m_nrdSrvCount = 0;
	uint32_t m_nrdUavCount = 0;
	uint32_t m_nrdPoolSize = 0; // srv + uav
	uint32_t m_nrdBaseRegister = 0;
	uint32_t m_nrdRegisterSpace = 0;

	// CB upload for NRD constants (you already have these, keep if present)
	Microsoft::WRL::ComPtr<ID3D12Resource> m_nrdConstUpload;
	uint32_t m_nrdConstUploadSize = 0;

	// NRD runtime objects for executing compute dispatches produced by NRD
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_nrdRootSignature;
	std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_nrdPipelines;

	// Upload CB for NRD per-dispatch constants
//	Microsoft::WRL::ComPtr<ID3D12Resource> m_nrdConstUpload; // upload heap
//	uint32_t m_nrdConstUploadSize = 0;

	// Toggle denoiser
	bool m_enableDenoise = false;

	// Create PSOs/root signature from NRD instance description (call after m_nrd.Initialize)
	void CreateNRDPipelines();

	// Query NRD dispatches and record compute dispatches into current m_commandList
	void ExecuteNRDDispatches();


	//	uint32_t m_nrdFrameIndex = 0;




	ComPtr<ID3D12Resource> m_aovNormalRoughness; // u1
	ComPtr<ID3D12Resource> m_aovViewZ;           // u2
	ComPtr<ID3D12Resource> m_aovDiffHitDist;     // u3
	ComPtr<ID3D12Resource> m_aovSpecHitDist;     // u4

	void D3D12HelloTriangle::CreateAOVResources();

	ComPtr<ID3D12Resource> m_aovDiffuse;
	ComPtr<ID3D12Resource> m_aovSpecular;
	//ComPtr<ID3D12Resource> m_aovNormalRoughness;
	//ComPtr<ID3D12Resource> m_aovViewZ;
	ComPtr<ID3D12Resource> m_denoisedOutput; // wynik NRD (UAV)

	ID3D12Resource* D3D12HelloTriangle::GetResourceForNrdType(nrd::ResourceType t);

	void D3D12HelloTriangle::WriteNrdUav(uint32_t index, ID3D12Resource* res);
	void D3D12HelloTriangle::WriteNrdSrv(uint32_t index, ID3D12Resource* res);


	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_nrdPoolHeap;
	//uint32_t m_nrdPoolSize = 0;       // total descriptors in pool = srvCount + uavCount
//	uint32_t m_nrdSrvCount = 0;
	//uint32_t m_nrdUavCount = 0;
	UINT m_nrdHeapInc = 0;
	//UINT m_nrdHeapInc = 0;


private:
	ComPtr<ID3D12DescriptorHeap> m_imguiHeap;
	static const UINT FrameCount = 2;

	// NRD integration instance
	NRDIntegration m_nrd;

	UINT m_frameIndexCPU = 0;
	UINT m_sampleCount = 4;
	UINT m_maximumRecursionDepth = 25;
	bool m_enableAdaptiveSampling = false;
	float m_targetFrameRate = 30.0f;
	int m_slowFrameCount = 0;
	UINT m_ISOIndex = 400;
	bool m_highlightOverexposed = false;
	bool m_enableEnvironmentTexture = true;
	XMFLOAT3 m_environmentColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
	float m_environmentIntensity = 1.0f;

	struct SceneCB
	{
		XMMATRIX View;
		XMMATRIX Proj;
		XMMATRIX InvView;
		XMMATRIX InvProj;
		UINT FrameIndex;
		UINT SampleCount;
		UINT MaxRecursionDepth;
		UINT ISOIndex;
		XMFLOAT3 EnvironmentColor;
		bool HighlightOverexposed;
		bool padding[3];
		bool EnableEnvironmentTexture;
		bool padding2[3];
		float pad[2];
	};

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
		XMFLOAT3 normal;
		float roughness;
		XMFLOAT3 emmision;
		float pad1;
	};

	struct AnimationFrame
	{
		float time;
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 rotation;
		DirectX::XMFLOAT3 scale;
	};

	struct ModelDesc
	{
		int id;
		std::string path;
		DirectX::XMFLOAT3 position = { 0, 0, 0 };
		DirectX::XMFLOAT3 rotation = { 0, 0, 0 }; // pitch, yaw, roll
		DirectX::XMFLOAT3 scale = { 1, 1, 1 };
		DirectX::XMFLOAT3 albedo = { -1.0f, -1.0f, -1.0f };
		int emission = -1;
		float roughness = -1;
		int isMetallic = false;
		int isGlass = false;
		float IOR = 1.5f;
		std::vector<AnimationFrame> animationFrames;
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

		DirectX::XMMATRIX worldMatrix;
		
	};
public:
	//buffers:
	ComPtr<ID3D12Resource> m_instancesBuffer;       // GPU buffer (ModelInstanceGPU)
	ComPtr<ID3D12Resource> m_instancesUpload;       // Upload buffer

	std::vector<ModelDesc> ModelDescriptions;
	std::vector<ModelInstance> Models;

	struct ModelInstanceGPU
	{
		XMFLOAT3 albedo = { -1.0f, -1.0f, -1.0f };
		int id;
		float emission = -1;
		float roughness = -1;
		int isMetallic = false;
		int isGlass = false;
		float IOR = 1.5f;
		float pad[3];
	};

	std::vector<ModelInstanceGPU> ModelsShaderData;
	
	XMFLOAT3 tempLight;
	struct LightData {
		XMFLOAT3 position; float intensity = 100;
		XMFLOAT3 color;    int type;
	};
	//HDR Image
	struct HDRImage
	{
		int width = 0;
		int height = 0;
		int channels = 0; // should be 3
		std::vector<float> pixels; // RGBRGBRGB...
	};
	HDRImage LoadHDR(const std::string& path);
	ComPtr<ID3D12Resource> m_envTexture;

	// #DXR Extra: Perspective Camera
	void CreateCameraBuffer();
	void UpdateCameraBuffer();
	void UpdateModelTranslations(); // animating models
	void CreateLightsBuffer();
	void UpdateLightsBuffer();

	void CreateModelDataBuffer();
	void UpdateModelDataBuffer();
	void CreateEnvironmentTexture(const HDRImage& img);

	ComPtr< ID3D12Resource > m_cameraBuffer;
	LightData m_lightData;
	ComPtr< ID3D12Resource > m_lightsBuffer;
	ComPtr< ID3D12DescriptorHeap > m_constHeap;
	ComPtr< ID3D12DescriptorHeap > m_samplerHeap;
	uint32_t m_cameraBufferSize = 0;
	uint32_t m_lightsBufferSize = 0;
	UINT m_envSrvIndex = UINT_MAX;

	double D3D12HelloTriangle::degreesToRadians(double degrees);

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
	void InitializeShaderData(int i);
	void PopulateCommandList();
	void WaitForPreviousFrame();
	void CheckRaytracingSupport();
	virtual void OnKeyUp(UINT8 key);
	virtual void OnKeyDown(UINT8 key);
	void AdjustSampleCount();
	bool m_raster = false;
	std::wstring currentShading = L"BSDF";
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
ComPtr<IDxcBlob> m_mirrorDemoShaderLibrary;
ComPtr<IDxcBlob> m_BSDFShaderLibrary;

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

void D3D12HelloTriangle::UpdateNRDCommonSettingsPerFrame();
void D3D12HelloTriangle::PrepareNRDDescriptorPoolIfNeeded();
void D3D12HelloTriangle::WriteNRDPoolDescriptor(
	const nrd::ResourceDesc& rd,
	ID3D12Resource* resource,
	DXGI_FORMAT format,
	bool isUav);

nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
ComPtr<ID3D12Resource> m_sbtStorage;

void D3D12HelloTriangle::LoadModel(const std::string& modelPath,
	std::vector<Vertex>& outVertices,
	std::vector<uint32_t>& outIndices);
std::vector<ModelDesc> D3D12HelloTriangle::LoadScene(const std::string& filename);
void D3D12HelloTriangle::SaveScene(const std::string& filename);

// #DXR Extra: Perspective Camera++
void OnButtonDown(UINT32 lParam);
void OnMouseMove(UINT8 wParam, UINT32 lParam);
//for changing shading mode
void D3D12HelloTriangle::AddModel(const std::string& path, bool realoading = false);
void D3D12HelloTriangle::RemoveModel(int index);
void D3D12HelloTriangle::BuildTLAS();
bool BLASChanged=false;
public:
	void D3D12HelloTriangle::SetShadingMode(const std::wstring& mode);


	bool keyWDown = false;
	bool keySDown = false;
	bool keyADown = false;
	bool keyDDown = false;
	bool keyQDown = false;
	bool keyEDown = false;

	// NRD runtime objects for executing compute dispatches produced by NRD
	//ComPtr<ID3D12RootSignature> m_nrdRootSignature;
	//std::vector<ComPtr<ID3D12PipelineState>> m_nrdPipelines;

	// Upload CB for NRD per-dispatch constants
	//ComPtr<ID3D12Resource> m_nrdConstUpload; // upload heap
	//uint32_t m_nrdConstUploadSize = 0;

	// Toggle
	//bool m_enableDenoise = true;

	// Create PSOs/root signature from NRD instance description (call after m_nrd.Initialize)
	//void CreateNRDPipelines();

	// Query NRD dispatches and record compute dispatches into the current m_commandList
	// This function uses m_srvUavHeap and m_samplerHeap as the descriptor heaps for NRD resources.
	//void ExecuteNRDDispatches();
};

