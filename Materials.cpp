#include "D3D12HelloTriangle.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

D3D12HelloTriangle::MaterialKey D3D12HelloTriangle::ExtractMaterialKey(
    aiMaterial* mat,
    const std::string& modelPath)
{
    D3D12HelloTriangle::MaterialKey key{};

    // Albedo factor
    aiColor4D albedo(1, 1, 1, 1);
    mat->Get(AI_MATKEY_COLOR_DIFFUSE, albedo);
    key.albedoFactor = { albedo.r, albedo.g, albedo.b };

    // --- Roughness ---
    key.roughness = 0.4f; // default
    // Try the common material keys depending on Assimp version
#if defined(AI_MATKEY_ROUGHNESS_FACTOR)
    mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, key.roughness);
#elif defined(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR)
    mat->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, key.roughness);
#endif

    std::string modelDir = GetDirectoryFromPath(modelPath);
    aiString texPath;

    // Albedo texture
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
    {
        key.albedoTexturePath = JoinPath(modelDir, texPath.C_Str());
        NormalizePath(key.albedoTexturePath);
    }

    // Normal map
    if (mat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS ||
        mat->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS)
    {
        key.normalTexturePath = JoinPath(modelDir, texPath.C_Str());
        NormalizePath(key.normalTexturePath);
    }

    key.isMetallic = false;
    key.isGlass = false;
    key.IOR = 1.5f;

    return key;
}

int D3D12HelloTriangle::GetOrCreateMaterialIndex(const D3D12HelloTriangle::MaterialKey& key)
{
    auto it = g_MaterialMap.find(key);
    if (it != g_MaterialMap.end())
        return it->second;

    int index = (int)MaterialsGPU.size();
    g_MaterialMap[key] = index;

    D3D12HelloTriangle::MaterialGPU gpuMat{};
    gpuMat.albedoFactor = key.albedoFactor;
    gpuMat.roughness = key.roughness;
	gpuMat.albedoTextureIndex = GetOrCreateTextureIndex(key.albedoTexturePath);
    gpuMat.isMetallic = key.isMetallic;
    gpuMat.isGlass = key.isGlass;
    gpuMat.IOR = key.IOR;

    MaterialsGPU.push_back(gpuMat);
    return index;
}