#include "NRDIntegration.h"
#include <iostream>
#include <cstring>

NRDIntegration::NRDIntegration() = default;

NRDIntegration::~NRDIntegration()
{
    Shutdown();
}

bool NRDIntegration::Initialize(uint32_t width, uint32_t height)
{
    if (m_instance)
        return true;

    m_width = width;
    m_height = height;

    // Allocation callbacks: nullptr => NRD uses its internal defaults (OK for sample)
    nrd::AllocationCallbacks allocCb = {};
    allocCb.Allocate = nullptr;
    allocCb.Reallocate = nullptr;
    allocCb.Free = nullptr;
    allocCb.userArg = nullptr;

    // Choose denoiser
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

    // Set a minimal initial CommonSettings.
    // NOTE: Matrices are NOT set here - you must do that every frame in UpdateCommonSettings.
    nrd::CommonSettings cs = {};
    cs.resourceSize[0] = static_cast<uint16_t>(m_width);
    cs.resourceSize[1] = static_cast<uint16_t>(m_height);
    cs.resourceSizePrev[0] = cs.resourceSize[0];
    cs.resourceSizePrev[1] = cs.resourceSize[1];
    cs.rectSize[0] = cs.resourceSize[0];
    cs.rectSize[1] = cs.resourceSize[1];
    cs.rectSizePrev[0] = cs.rectSize[0];
    cs.rectSizePrev[1] = cs.rectSize[1];
    cs.frameIndex = 0;
    cs.viewZScale = 1.0f;
    cs.denoisingRange = 1000.0f;
    cs.accumulationMode = nrd::AccumulationMode::CONTINUE;
    cs.isMotionVectorInWorldSpace = false;
    cs.isHistoryConfidenceAvailable = false;

    r = nrd::SetCommonSettings(*m_instance, cs);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: SetCommonSettings failed in Initialize\n";
        // not fatal
    }

    // OPTIONAL: set default REBLUR settings (safe defaults)
    nrd::ReblurSettings reblur = {};
    // These are conservative defaults; tune later.
  //  reblur.enableReferenceAccumulation = false;
    reblur.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::OFF;
    reblur.maxAccumulatedFrameNum = 30;

    // Set default settings now
    UpdateDenoiserSettings_REBLUR(reblur);

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
        m_width = m_height = 0;
        m_hasReblurSettings = false;
        m_reblurSettings = {};
    }
}

bool NRDIntegration::UpdateCommonSettings(const nrd::CommonSettings& cs)
{
    if (!m_instance)
        return false;

    nrd::Result r = nrd::SetCommonSettings(*m_instance, cs);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: SetCommonSettings failed\n";
        return false;
    }
    return true;
}

bool NRDIntegration::UpdateDenoiserSettings_REBLUR(const nrd::ReblurSettings& settings)
{
    if (!m_instance)
        return false;

    // IMPORTANT: For multiple denoisers you'd use identifier-specific calls,
    // but with 1 denoiser we can call SetDenoiserSettings with our identifier.
    nrd::Result r = nrd::SetDenoiserSettings(*m_instance, m_identifier, &settings);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: SetDenoiserSettings(REBLUR) failed\n";
        return false;
    }

    m_hasReblurSettings = true;
    m_reblurSettings = settings;
    return true;
}

bool NRDIntegration::GetDispatches(const nrd::DispatchDesc*& outDispatches, uint32_t& outCount)
{
    outDispatches = nullptr;
    outCount = 0;

    if (!m_instance)
        return false;

    nrd::Identifier ids[1] = { m_identifier };

    nrd::Result r = nrd::GetComputeDispatches(*m_instance, ids, 1, outDispatches, outCount);
    if (r != nrd::Result::SUCCESS)
    {
        std::cerr << "NRD: GetComputeDispatches failed\n";
        outDispatches = nullptr;
        outCount = 0;
        return false;
    }

    return true;
}

const nrd::InstanceDesc* NRDIntegration::GetInstanceDesc() const
{
    if (!m_instance)
        return nullptr;

    return nrd::GetInstanceDesc(*m_instance);
}
