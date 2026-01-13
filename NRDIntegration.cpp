#include "NRDIntegration.h"
#include "NRD/Include/NRDDescs.h"
#include <iostream>
#include <cstring>

NRDIntegration::NRDIntegration()
    : m_instance(nullptr)
    , m_identifier(0)
    , m_width(0)
    , m_height(0)
{
}

NRDIntegration::~NRDIntegration()
{
    Shutdown();
}

bool NRDIntegration::Initialize(uint32_t width, uint32_t height)
{
    if (m_instance)
        return true; // already initialized

    m_width = width;
    m_height = height;

    // Minimal allocation callbacks (null = use default/new/delete semantics in your integration)
    nrd::AllocationCallbacks allocCb;
    std::memset(&allocCb, 0, sizeof(allocCb));
    allocCb.Allocate = nullptr;
    allocCb.Reallocate = nullptr;
    allocCb.Free = nullptr;
    allocCb.userArg = nullptr;

    // Choose a denoiser and unique identifier (opaque to NRD)
    nrd::DenoiserDesc denoiserDesc = {};
    denoiserDesc.identifier = 1; // app-unique id
    denoiserDesc.denoiser = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;

    nrd::InstanceCreationDesc icd = {};
    icd.allocationCallbacks = allocCb;
    icd.denoisers = &denoiserDesc;
    icd.denoisersNum = 1;

    nrd::Result r = nrd::CreateInstance(icd, m_instance);
    if (r != nrd::Result::SUCCESS || !m_instance)
    {
        std::cerr << "NRD: CreateInstance failed\n";
        m_instance = nullptr;
        return false;
    }

    m_identifier = denoiserDesc.identifier;

    // Prepare an initial CommonSettings and set it
    nrd::CommonSettings cs = {};
    cs.resourceSize[0] = static_cast<uint16_t>(m_width);
    cs.resourceSize[1] = static_cast<uint16_t>(m_height);
    cs.resourceSizePrev[0] = cs.resourceSize[0];
    cs.resourceSizePrev[1] = cs.resourceSize[1];
    cs.rectSize[0] = cs.resourceSize[0];
    cs.rectSize[1] = cs.resourceSize[1];
    cs.frameIndex = 0;
    cs.viewZScale = 1.0f;
    cs.denoisingRange = 1000.0f;
    cs.accumulationMode = nrd::AccumulationMode::CONTINUE;
    cs.isMotionVectorInWorldSpace = false;
    cs.isHistoryConfidenceAvailable = false;

    r = nrd::SetCommonSettings(*m_instance, cs);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: SetCommonSettings failed\n";
        // Not fatal for skeleton; you may choose to DestroyInstance here.
    }

    std::cout << "NRD: Instance created (identifier=" << m_identifier << ")\n";
    return true;
}

void NRDIntegration::Shutdown()
{
    if (m_instance)
    {
        nrd::DestroyInstance(*m_instance);
        m_instance = nullptr;
        m_identifier = 0;
    }
}

bool NRDIntegration::UpdateCommonSettings(const nrd::CommonSettings& cs)
{
    if (!m_instance) return false;
    nrd::Result r = nrd::SetCommonSettings(*m_instance, cs);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: SetCommonSettings failed\n";
        return false;
    }
    return true;
}

bool NRDIntegration::ApplyDenoise()
{
    if (!m_instance) return false;

    // Query compute dispatches for our denoiser identifier
    const nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchDescsNum = 0;
    nrd::Identifier ids[1] = { m_identifier };

    nrd::Result r = nrd::GetComputeDispatches(*m_instance, ids, 1, dispatchDescs, dispatchDescsNum);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: GetComputeDispatches failed\n";
        return false;
    }

    // The returned memory is owned by the NRD instance and will be overwritten on the next call.
    // In a full integration you must:
    //  - iterate dispatchDescs[0..dispatchDescsNum-1]
    //  - for each DispatchDesc bind resources described in dispatchDescs[i].resources (SRV/UAV)
    //  - set constant buffer bytes from dispatchDescs[i].constantBufferData if required
    //  - set pipeline according to dispatchDescs[i].pipelineIndex and record a Dispatch with grid sizes
    std::cout << "NRD: Received " << dispatchDescsNum << " dispatch(es)\n";
    for (uint32_t i = 0; i < dispatchDescsNum; ++i)
    {
        const nrd::DispatchDesc& d = dispatchDescs[i];
        const char* name = d.name ? d.name : "<unnamed>";
        std::cout << "  dispatch[" << i << "] name=\"" << name << "\" grid=("
            << d.gridWidth << "x" << d.gridHeight << ") pipelineIndex=" << d.pipelineIndex
            << " constantsSize=" << d.constantBufferDataSize << " resources=" << d.resourcesNum << "\n";
    }

    // Return success; actual dispatch recording should be implemented in the app.
    return true;
}