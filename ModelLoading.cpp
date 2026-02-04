#include "stdafx.h"
#include "D3D12HelloTriangle.h"
#include "DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"   
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "d3dx12.h"
#include <d3dcompiler.h>
#include "Windowsx.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "libraries/nlohmann/json.hpp"
#include "manipulator.h"


using json = nlohmann::json;

namespace
{
	std::wstring Utf8ToWString(const std::string& str)
	{
		if (str.empty())
			return {};

		int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
		if (sizeNeeded <= 0)
			return {};

		std::wstring result(static_cast<size_t>(sizeNeeded - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], sizeNeeded);
		return result;
	}
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
		//std::vector<std::string> modelPaths = {
		//	"Models/Clamp.stl",
		//	"Models/Cube.obj",
		//	"Models/Cube.obj",
		//	"Models/FinalBaseMesh.obj",
		//	//"Models/Square.obj"
		//};
		//MODEL
		Models.resize(ModelDescriptions.size());

		ModelsShaderData.resize(ModelDescriptions.size());
		//Models[1].scale = { 50.0f,1.0f,50.0f };
		//Models[2].scale = { 50.0f,1.0f,50.0f };
		//Models[2].position = { 0.0f,50.0f,0.0f };
		//ModelsShaderData[1].roughness = 0.0f;
		//ModelsShaderData[2].roughness = 0.0f;
		//Models[0].rotation = { XMConvertToRadians(45.0f), 0.0f, 0.0f };
		//Models[1].rotation = { XMConvertToRadians(-45.0f), 0.0f, 0.0f };
		//ModelsShaderData[0].albedo = { 1.0f,1.0f,0.5f };
		//ModelsShaderData[0].albedo = { 0.0f,1.0f,0.5f };
		//ModelsShaderData[1].albedo = { 1.0f,1.0f,0.5f };
		//ModelsShaderData[2].albedo = { 0.7f,0.7f,0.0f };
		//ModelsShaderData[3].albedo = { 0.9f,0.0f,0.1f };
		//Models[3].scale = { 2.0f,2.0f,2.0f };
		//Models[3].position = { 0.0f,0.0f,10.0f };
		//Models[4].scale = { 50.0f,50.0f,1.0f };
		//ModelsShaderData[4].isGlass = true;
		for (int i = 0; i < ModelDescriptions.size(); i++)
		{

			LoadModel(ModelDescriptions[i].path, Models[i].vertices, Models[i].indices);

			InitializeShaderData(i);

			const UINT vertexBufferSize = static_cast<UINT>(Models[i].vertices.size()) * sizeof(Vertex);

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&(Models[i].m_vertexBuffer))));

			UINT8* pVertexDataBegin;
			CD3DX12_RANGE readRange(0, 0); 		// We do not intend to read from this resource on the CPU.
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

		DirectX::XMFLOAT4 meshColor = { diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a };

		UINT vertexOffset = (UINT)outVertices.size();

		for (UINT i = 0; i < mesh->mNumVertices; ++i)
		{
			Vertex v;
			v.position.x = mesh->mVertices[i].x;
			v.position.y = mesh->mVertices[i].y;
			v.position.z = mesh->mVertices[i].z;

			v.color = meshColor;
			v.roughness = 0.4f; // Default roughness

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

void D3D12HelloTriangle::AddModel(const std::string& path, bool reloading) {
	WaitForPreviousFrame();

	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	ModelDesc newDescription;
	ModelInstance newModel = {};
	newDescription.id = newModel.id = static_cast<int>(Models.size());
	newDescription.path = path;

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

	if (!reloading) {
		ModelDescriptions.push_back(newDescription);
	}
	Models.push_back(newModel);

	ModelInstanceGPU newModelInstance;
	newModelInstance.id = newModel.id;
	newModelInstance.albedo = newDescription.albedo;
	newModelInstance.roughness = newDescription.roughness;
	ModelsShaderData.push_back(newModelInstance);

	CreateModelDataBuffer();

	m_instances.clear();
	for (size_t i = 0; i < BLASes.size(); i++) {
		XMMATRIX tr = XMMatrixIdentity();
		if (i < ModelDescriptions.size()) {
			XMMATRIX sc = XMMatrixScaling(ModelDescriptions[i].scale.x, ModelDescriptions[i].scale.y, ModelDescriptions[i].scale.z);
			XMMATRIX rot = XMMatrixRotationRollPitchYaw(degreesToRadians(ModelDescriptions[i].rotation.x), degreesToRadians(ModelDescriptions[i].rotation.y), degreesToRadians(ModelDescriptions[i].rotation.z));
			XMMATRIX trans = XMMatrixTranslation(ModelDescriptions[i].position.x, ModelDescriptions[i].position.y, ModelDescriptions[i].position.z);
			tr = sc * rot * trans;
		}
		m_instances.push_back({ BLASes[i].pResult.Get(), tr });
	}

	CreateTopLevelAS(m_instances, false);


	CreateShaderResourceHeap();
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

	ModelsShaderData.erase(ModelsShaderData.begin() + index);
	for (int i = 0; i < ModelsShaderData.size(); i++) ModelsShaderData[i].id = i;

	ModelDescriptions.erase(ModelDescriptions.begin() + index);
	for (int i = 0; i < ModelDescriptions.size(); i++) ModelDescriptions[i].id = i;

	Models.erase(Models.begin() + index);
	for (int i = 0; i < Models.size(); i++) Models[i].id = i;

	BLASes.erase(BLASes.begin() + index);

	m_bottomLevelAS.clear();
	for (auto& b : BLASes) m_bottomLevelAS.push_back(b.pResult);

	UpdateModelDataBuffer();

	m_instances.clear();
	for (size_t i = 0; i < BLASes.size(); i++) {
		m_instances.push_back({ BLASes[i].pResult.Get(), XMMatrixIdentity() });
	}
	CreateTopLevelAS(m_instances, false);

	CreateShaderResourceHeap();
	CreateRaytracingPipeline();
	CreateShaderBindingTable();

	ThrowIfFailed(m_commandAllocator->Reset());
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));
	ThrowIfFailed(m_commandList->Close());

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

	WaitForPreviousFrame();

	BLASChanged = true;
}

//for loading a scene based on a json file
std::vector<D3D12HelloTriangle::ModelDesc> D3D12HelloTriangle::LoadScene(const std::string& filename)
{
	std::ifstream file(filename);
	if (!file.is_open())
	{
		std::wstring widePath = Utf8ToWString(filename);
		if (!widePath.empty())
		{
			std::wstring fullPathW = GetAssetFullPath(widePath.c_str());
			std::string fullPath = WStringToUtf8(fullPathW);
			file.open(fullPath);
			if (!file.is_open())
			{
				m_sceneLoadError = "Cannot open scene file: " + filename;
				return ModelDescriptions;
			}
		}
		else
		{
			m_sceneLoadError = "Cannot open scene file: " + filename;
			return ModelDescriptions;
		}
	}

	json j;
	try
	{
		file >> j;
	}
	catch (const json::parse_error& e)
	{
		m_sceneLoadError = std::string("Scene JSON parse error: ") + e.what();
		return ModelDescriptions;
	}

	//Camera
	if (j.contains("camera"))
	{
		nv_helpers_dx12::Manipulator& manip = nv_helpers_dx12::CameraManip;
		auto eye = j["camera"]["eye"];
		auto center = j["camera"]["center"];
		auto up = j["camera"]["up"];
		manip.setLookat(
			glm::vec3(eye[0], eye[1], eye[2]),
			glm::vec3(center[0], center[1], center[2]),
			glm::vec3(up[0], up[1], up[2])
		);
	}

	//Light
	if (j.contains("light"))
	{
		auto pos = j["light"]["position"];
		auto color = j["light"]["color"];
		m_lightData.position = { pos[0], pos[1], pos[2] };
		m_lightData.color = { color[0], color[1], color[2] };
		m_lightData.intensity = j["light"]["intensity"];
		m_lightData.type = j["light"]["type"];
		UpdateLightsBuffer();
	}

	std::vector<D3D12HelloTriangle::ModelDesc> result;

	for (auto& m : j["models"])
	{
		D3D12HelloTriangle::ModelDesc desc;
		desc.id = m["id"];
		desc.path = m["path"];

		if (m.contains("position"))
		{
			auto p = m["position"];
			desc.position = { p[0], p[1], p[2] };
		}

		if (m.contains("rotation"))
		{
			auto r = m["rotation"];
			desc.rotation = { r[0], r[1], r[2] };
		}

		if (m.contains("scale"))
		{
			auto s = m["scale"];
			desc.scale = { s[0], s[1], s[2] };
		}

		if (m.contains("albedo"))
		{
			auto a = m["albedo"];
			desc.albedo = { a[0], a[1], a[2] };
		}

		if (m.contains("emission"))
		{
			desc.emission = m["emission"];
		}

		if (m.contains("roughness"))
		{
			desc.roughness = m["roughness"];
		}

		if (m.contains("isMetallic"))
		{
			desc.isMetallic = m["isMetallic"];
		}

		if (m.contains("isGlass"))
		{
			desc.isGlass = m["isGlass"];
		}

		if (m.contains("IOR"))
		{
			desc.IOR = m["IOR"];
		}

		if (m.contains("animationFrames") &&
			m["animationFrames"].contains("frames"))
		{
			DirectX::XMFLOAT3 prevPosition = desc.position;
			DirectX::XMFLOAT3 prevRotation = desc.rotation;
			DirectX::XMFLOAT3 prevScale = desc.scale;
			for (auto& af : m["animationFrames"]["frames"])
			{
				D3D12HelloTriangle::AnimationFrame frame;
				if (af.contains("time"))
				{
					frame.time = af["time"];
				}
				else
					frame.time = 1.0f;
				if (af.contains("position"))
				{
					auto p = af["position"];
					prevPosition = frame.position = { p[0], p[1], p[2] };
				}
				else
					frame.position = prevPosition;
				if (af.contains("rotation"))
				{
					auto p = af["rotation"];
					prevRotation = frame.rotation = { p[0], p[1], p[2] };
				}
				else
					frame.rotation = prevRotation;
				if (af.contains("scale"))
				{
					auto p = af["scale"];
					prevScale = frame.scale = { p[0], p[1], p[2] };
				}
				else
					frame.scale = prevScale;
				desc.animationFrames.push_back(frame);
			}
		}

		result.push_back(desc);
	}

	return result;
}

void D3D12HelloTriangle::SaveScene(const std::string& filename)
{
	json j;
	j["models"] = json::array();

	for (int i = 0; i < ModelDescriptions.size(); i++)
	{
		auto m = ModelDescriptions[i];
		auto m2 = ModelsShaderData[i];
		json jm;
		jm["id"] = m.id;
		jm["path"] = m.path;
		jm["position"] = { m.position.x, m.position.y, m.position.z };
		jm["rotation"] = { m.rotation.x, m.rotation.y, m.rotation.z };
		jm["scale"] = { m.scale.x, m.scale.y, m.scale.z };
		jm["albedo"] = { m2.albedo.x, m2.albedo.y, m2.albedo.z };
		jm["emission"] = m2.emission;
		jm["roughness"] = m2.roughness;
		jm["isMetallic"] = m2.isMetallic;
		jm["isGlass"] = m2.isGlass;
		jm["IOR"] = m2.IOR;

		if (m.animationFrames.size() > 0)
		{
			jm["animationFrames"]["frames"] = json::array();
			for (auto& af : m.animationFrames)
			{
				json jaf;
				jaf["time"] = af.time;
				jaf["position"] = { af.position.x, af.position.y, af.position.z };
				jaf["rotation"] = { af.rotation.x, af.rotation.y, af.rotation.z };
				jaf["scale"] = { af.scale.x, af.scale.y, af.scale.z };
				jm["animationFrames"]["frames"].push_back(jaf);
			}
		}

		j["models"].push_back(jm);
	}

	//Camera
	nv_helpers_dx12::Manipulator& manip = nv_helpers_dx12::CameraManip;
	glm::vec3 eye, center, up;
	nv_helpers_dx12::CameraManip.getLookat(eye, center, up);
	j["camera"]["eye"] = { eye.x, eye.y, eye.z };
	j["camera"]["center"] = { center.x, center.y, center.z };
	j["camera"]["up"] = { up.x, up.y, up.z };

	//Light
	j["light"]["position"] = { m_lightData.position.x, m_lightData.position.y, m_lightData.position.z };
	j["light"]["intensity"] = m_lightData.intensity;
	j["light"]["color"] = { m_lightData.color.x, m_lightData.color.y, m_lightData.color.z };
	j["light"]["type"] = m_lightData.type;

	std::ofstream file(filename);
	file << j.dump(4);
}

void D3D12HelloTriangle::InitializeShaderData(int i)
{
	ModelsShaderData[i].id = Models[i].id = i;
	ModelsShaderData[i].albedo = ModelDescriptions[i].albedo;
	ModelsShaderData[i].emission = ModelDescriptions[i].emission;
	ModelsShaderData[i].roughness = ModelDescriptions[i].roughness;
	ModelsShaderData[i].isGlass = ModelDescriptions[i].isGlass;
	ModelsShaderData[i].isMetallic = ModelDescriptions[i].isMetallic;
	ModelsShaderData[i].IOR = ModelDescriptions[i].IOR;
}