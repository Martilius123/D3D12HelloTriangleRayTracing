#include "D3D12HelloTriangle.h"
#include "DXRHelper.h"
#include "d3dx12.h"
#include "manipulator.h"
#include "glm/gtc/type_ptr.hpp"

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
	m_cameraBufferSize = (sizeof(SceneCB) + 255) & ~255;

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


void D3D12HelloTriangle::UpdateLightsBuffer()
{
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

//material buffers

void D3D12HelloTriangle::CreateMaterialDataBuffer()
{

	//for (int i = 0; i < Models.size(); i++)
	{

		UINT bufferSize = sizeof(MaterialGPU) * MaterialsGPU.size();

		CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);



		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapDefault,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&(m_materialsBuffer)))
		);

		// Upload heap
		CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapUpload,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&(m_materialsUpload)))
		);

		UpdateMaterialDataBuffer();
	}

}

void D3D12HelloTriangle::UpdateMaterialDataBuffer() {

	UINT bufferSize = sizeof(MaterialGPU) * MaterialsGPU.size();
	void* mapped = nullptr;
	m_instancesUpload->Map(0, nullptr, &mapped);
	memcpy(mapped, MaterialsGPU.data(), bufferSize);
	m_instancesUpload->Unmap(0, nullptr);
}


// #DXR Extra: Perspective Camera
//--------------------------------------------------------------------------------
// Create and copies the viewmodel and perspective matrices of the camera
//
void D3D12HelloTriangle::UpdateCameraBuffer()
{
	using namespace nv_helpers_dx12;

	Manipulator& manip = CameraManip;

	// --- Timing ---
	static DWORD lastTime = GetTickCount();
	DWORD now = GetTickCount();
	float deltaTime = (now - lastTime) / 1000.0f;
	lastTime = now;

	// --- Camera matrices ---
	SceneCB sceneCB = {};

	// View matrix from manipulator (GLM -> XMMATRIX)
	const glm::mat4& glmView = manip.getMatrix();
	memcpy(&sceneCB.View, glm::value_ptr(glmView), sizeof(XMMATRIX));

	// Projection matrix
	float fovAngleY = 45.0f * XM_PI / 180.0f;
	sceneCB.Proj = XMMatrixPerspectiveFovRH(
		fovAngleY,
		m_aspectRatio,
		0.1f,
		1000.0f
	);

	// Inverses (needed for ray tracing)
	XMVECTOR det;
	sceneCB.InvView = XMMatrixInverse(&det, sceneCB.View);
	sceneCB.InvProj = XMMatrixInverse(&det, sceneCB.Proj);

	// Frame index
	sceneCB.FrameIndex = m_frameIndexCPU++;
	sceneCB.SampleCount = m_sampleCount;
	sceneCB.MaxRecursionDepth = m_maximumRecursionDepth;
	sceneCB.ISOIndex = m_ISOIndex;
	sceneCB.HighlightOverexposed = m_highlightOverexposed;
	sceneCB.EnableEnvironmentTexture = m_enableEnvironmentTexture;
	sceneCB.EnvironmentColor = { m_environmentColor.x * m_environmentIntensity, m_environmentColor.y * m_environmentIntensity, m_environmentColor.z * m_environmentIntensity };

	// --- Upload constant buffer ---
	uint8_t* pData;
	ThrowIfFailed(m_cameraBuffer->Map(0, nullptr, (void**)&pData));
	memcpy(pData, &sceneCB, sizeof(SceneCB));
	m_cameraBuffer->Unmap(0, nullptr);

	// --- Keyboard movement ---
	glm::vec3 eye, center, up;
	manip.getLookat(eye, center, up);

	glm::vec3 forward = glm::normalize(center - eye);
	glm::vec3 right = glm::normalize(glm::cross(forward, up));

	float speed = manip.getSpeed();

	if (keyWDown) eye += forward * speed * deltaTime;
	if (keySDown) eye -= forward * speed * deltaTime;
	if (keyADown) eye -= right * speed * deltaTime;
	if (keyDDown) eye += right * speed * deltaTime;
	if (keyQDown) eye -= up * speed * deltaTime;
	if (keyEDown) eye += up * speed * deltaTime;

	// Update manipulator
	manip.setLookat(eye, eye + forward, up);

}

//Animating Model translations
void D3D12HelloTriangle::UpdateModelTranslations()
{
	// --- Timing ---
	DWORD now = GetTickCount();

	for (int i = 0; i < ModelDescriptions.size(); i++ )
	{
		if (ModelDescriptions[i].animationFrames.size() < 2)
			continue;
		// Find current and next frame
		float animationTime = fmodf(now / 1000.0f, ModelDescriptions[i].animationFrames.back().time);
		AnimationFrame* currentFrame = nullptr;
		AnimationFrame* nextFrame = nullptr;
		int numberOfFrames = ModelDescriptions[i].animationFrames.size();
		for (size_t j = 0; j < numberOfFrames; j++)
		{
			if (animationTime >= ModelDescriptions[i].animationFrames[j].time &&
				animationTime < ModelDescriptions[i].animationFrames[j + 1].time)
			{
				currentFrame = &ModelDescriptions[i].animationFrames[j];
				nextFrame = &ModelDescriptions[i].animationFrames[(j + 1)%numberOfFrames];
				break;
			}
		}
		if (!currentFrame || !nextFrame)
			continue;
		// Interpolate
		float frameDelta = nextFrame->time - currentFrame->time;
		float factor = (animationTime - currentFrame->time) / frameDelta;
		ModelDescriptions[i].position.x = currentFrame->position.x + factor * (nextFrame->position.x - currentFrame->position.x);
		ModelDescriptions[i].position.y = currentFrame->position.y + factor * (nextFrame->position.y - currentFrame->position.y);
		ModelDescriptions[i].position.z = currentFrame->position.z + factor * (nextFrame->position.z - currentFrame->position.z);
		ModelDescriptions[i].rotation.x = currentFrame->rotation.x + factor * (nextFrame->rotation.x - currentFrame->rotation.x);
		ModelDescriptions[i].rotation.y = currentFrame->rotation.y + factor * (nextFrame->rotation.y - currentFrame->rotation.y);
		ModelDescriptions[i].rotation.z = currentFrame->rotation.z + factor * (nextFrame->rotation.z - currentFrame->rotation.z);
		ModelDescriptions[i].scale.x = currentFrame->scale.x + factor * (nextFrame->scale.x - currentFrame->scale.x);
		ModelDescriptions[i].scale.y = currentFrame->scale.y + factor * (nextFrame->scale.y - currentFrame->scale.y);
		ModelDescriptions[i].scale.z = currentFrame->scale.z + factor * (nextFrame->scale.z - currentFrame->scale.z);
	}
}