#pragma once

#include "NRD/Include/NRD.h"
#include "NRD/Include/NRDDescs.h"
#include <cstdint>

class NRDIntegration
{
public:
    NRDIntegration();
    ~NRDIntegration();

    // Create NRD instance (one denoiser: REBLUR_DIFFUSE_SPECULAR)
    bool Initialize(uint32_t width, uint32_t height);
    void Shutdown();

    // Per-frame common settings (must be called once per frame when denoising)
    bool UpdateCommonSettings(const nrd::CommonSettings& cs);

    // Optional: set per-denoiser settings (REBLUR has own settings)
    bool UpdateDenoiserSettings_REBLUR(const nrd::ReblurSettings& settings);

    // Fetch dispatches for the denoiser
    bool GetDispatches(const nrd::DispatchDesc*& outDispatches, uint32_t& outCount);

    // Instance / identifier
    nrd::Instance* GetInstance() const { return m_instance; }
    nrd::Identifier GetIdentifier() const { return m_identifier; }

    // Expose instance description (pipelines + descriptor pool info)
    const nrd::InstanceDesc* GetInstanceDesc() const;

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    nrd::Instance* m_instance = nullptr;
    nrd::Identifier m_identifier = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // cache of last denoiser settings (not required, but useful)
    bool m_hasReblurSettings = false;
    nrd::ReblurSettings m_reblurSettings = {};
};
