#include "D3D12HelloTriangle.h"
#include "DXRHelper.h"
#include "libraries/stb_image/stb_image.h"
#include <wincodec.h>

ComPtr<ID3D12Resource> D3D12HelloTriangle::CreateTexture2DFromRGBAFloat(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& fenceValue,
    int width,
    int height,
    const float* rgbaPixels,
    DXGI_FORMAT format)
{
    ComPtr<ID3D12Resource> texture;

    // Describe texture
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &nv_helpers_dx12::kDefaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));
    if (FAILED(hr))
        throw std::runtime_error("CreateCommittedResource(texture) failed.");

    // Upload buffer
    UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr))
        throw std::runtime_error("CreateCommittedResource(upload) failed.");

    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = rgbaPixels;
    subresource.RowPitch = width * 4 * sizeof(float);
    subresource.SlicePitch = subresource.RowPitch * height;

    // Temp command list
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list));

    UpdateSubresources(list.Get(), texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresource);

    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    list->ResourceBarrier(1, &barrier);
    list->Close();

    ID3D12CommandList* lists[] = { list.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    fenceValue++;
    commandQueue->Signal(fence, fenceValue);
    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);

    return texture;
}

int D3D12HelloTriangle::LoadTextureFromFile(
    const std::string& path,
    bool isSRGB,
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& fenceValue)
{
    ImageData img = LoadImage(path); // width, height, uint8 RGBA

    std::vector<float> rgba(img.width * img.height * 4);
    for (int i = 0; i < img.width * img.height; ++i)
    {
        rgba[i * 4 + 1] = img.pixels[i * 4 + 1] / 255.0f;
        rgba[i * 4 + 2] = img.pixels[i * 4 + 2] / 255.0f;
        rgba[i * 4 + 3] = img.pixels[i * 4 + 3] / 255.0f;
        rgba[i * 4 + 0] = img.pixels[i * 4 + 0] / 255.0f;
    }

    DXGI_FORMAT format =
        isSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12Resource> tex =
        CreateTexture2DFromRGBAFloat(
            device,
            commandQueue,
            fence,
            fenceEvent,
            fenceValue,
            img.width,
            img.height,
            rgba.data(),
            format);

    int index = (int)g_Textures.size();
    g_TextureMap[path] = index;

    TextureCPU entry{};
    entry.path = path;
    entry.resource = tex;

    // compute CPU and GPU handles from next free index
    if (m_nextTextureDescriptor >= m_textureHeapSize)
        throw std::runtime_error("Texture descriptor heap full");

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_textureHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += SIZE_T(m_nextTextureDescriptor) * m_srvDescriptorSize;

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_textureHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += SIZE_T(m_nextTextureDescriptor) * m_srvDescriptorSize;

    // Describe SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;

    // create SRV at cpuHandle
    device->CreateShaderResourceView(tex.Get(), &srv, cpuHandle);

    // store handles in the entry
    entry.cpuHandle = cpuHandle;
    entry.gpuHandle = gpuHandle;

    m_nextTextureDescriptor++;

    // push entry
    g_Textures.push_back(entry);

    return index;
}

D3D12HelloTriangle::ImageData D3D12HelloTriangle::LoadImage(const std::string& path)
{
    ImageData img{};

    // get extension in lowercase
    std::string ext;
    auto pos = path.find_last_of('.');
    if (pos != std::string::npos)
    {
        ext = path.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    // Use WIC for TIFF (and other formats supported by Windows Imaging Component)
    if (ext == "tif" || ext == "tiff")
    {
        // Initialize COM if needed
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool coInitSucceeded = SUCCEEDED(hr);

        IWICImagingFactory* pFactory = nullptr;
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pFactory));
        if (FAILED(hr) || !pFactory)
        {
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to create imaging factory");
        }

        IWICBitmapDecoder* pDecoder = nullptr;
        hr = pFactory->CreateDecoderFromFilename(
            std::wstring(path.begin(), path.end()).c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &pDecoder);
        if (FAILED(hr) || !pDecoder)
        {
            pFactory->Release();
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to create decoder for " + path);
        }

        IWICBitmapFrameDecode* pFrame = nullptr;
        hr = pDecoder->GetFrame(0, &pFrame);
        if (FAILED(hr) || !pFrame)
        {
            pDecoder->Release();
            pFactory->Release();
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to get frame from " + path);
        }

        UINT w = 0, h = 0;
        hr = pFrame->GetSize(&w, &h);
        if (FAILED(hr))
        {
            pFrame->Release();
            pDecoder->Release();
            pFactory->Release();
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to get image size for " + path);
        }

        IWICFormatConverter* pConverter = nullptr;
        hr = pFactory->CreateFormatConverter(&pConverter);
        if (FAILED(hr) || !pConverter)
        {
            pFrame->Release();
            pDecoder->Release();
            pFactory->Release();
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to create format converter");
        }

        // Convert to 32bpp RGBA
        hr = pConverter->Initialize(
            pFrame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            pConverter->Release();
            pFrame->Release();
            pDecoder->Release();
            pFactory->Release();
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to initialize converter");
        }

        std::vector<unsigned char> pixels;
        pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        UINT stride = w * 4;
        hr = pConverter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr))
        {
            pConverter->Release();
            pFrame->Release();
            pDecoder->Release();
            pFactory->Release();
            if (coInitSucceeded) CoUninitialize();
            throw std::runtime_error("WIC: failed to copy pixels");
        }

        // release WIC COM objects
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        if (coInitSucceeded) CoUninitialize();

        img.width = static_cast<int>(w);
        img.height = static_cast<int>(h);
        img.channels = 4;
        img.pixels = std::move(pixels);
        return img;
    }

    // Fallback: use stb_image for other formats (PNG, JPG, TGA, BMP, ...)
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!data)
    {
        const char* reason = stbi_failure_reason();
        std::string msg = "stb_image failed to load: " + path + " -> ";
        msg += (reason ? reason : "unknown reason");
        throw std::runtime_error(msg);
    }

    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.assign(data, data + (w * h * 4));
    stbi_image_free(data);
    return img;
}

int D3D12HelloTriangle::GetOrCreateTextureIndex(std::string path)
{
    // Normalize the path so map keys are consistent.
    NormalizePath(path);

    // Fast path: already loaded
    auto it = g_TextureMap.find(path);
    if (it != g_TextureMap.end())
        return it->second;

    // Ensure texture descriptor heap exists (lazy-create if needed).
    if (!m_textureHeap)
    {
        if (!m_device) // defensive
            throw std::runtime_error("GetOrCreateTextureIndex: device not initialized");

        m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = m_textureHeapSize;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_textureHeap)));
        m_nextTextureDescriptor = 0;
    }

    // Decide sRGB usage heuristically from file extension (common case).
    bool isSRGB = false;
    {
        auto extPos = path.find_last_of('.');
        if (extPos != std::string::npos)
        {
            std::string ext = path.substr(extPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "tga")
            {
                // For typical color textures use sRGB; normal/roughness maps should be loaded with isSRGB=false
                // You may refine this later by inspecting material metadata or filename conventions.
                isSRGB = true;
            }
        }
    }

    // Load the texture and create SRV (LoadTextureFromFile will register it in g_TextureMap).
    try
    {
        int idx = LoadTextureFromFile(path, isSRGB, m_device.Get(), m_commandQueue.Get(), m_fence.Get(), m_fenceEvent, m_fenceValue);
        return idx;
    }
    catch (const std::exception& e)
    {
        std::string msg = "GetOrCreateTextureIndex: failed to load texture '" + path + "': " + e.what() + "\n";
        OutputDebugStringA(msg.c_str());
        return -1;
    }
}