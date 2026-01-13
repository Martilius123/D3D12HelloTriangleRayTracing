#pragma once

#include "NRD/Include/NRD.h"
#include <cstdint>


class NRDIntegration
{
public:
    NRDIntegration();
    ~NRDIntegration();

    // Initialize NRD instance; returns true on success.
    // width/height are the logical NRD resource sizes (full resolution).
    bool Initialize(uint32_t width, uint32_t height);

    // Shutdown / destroy NRD instance.
    void Shutdown();

    // Update per-frame common settings (call every frame before ApplyDenoise).
    // Copies the provided structure and calls NRD SetCommonSettings.
    bool UpdateCommonSettings(const nrd::CommonSettings& cs);

    // Query NRD for dispatches for the single created denoiser and log them.
    // In a real integration you should translate returned DispatchDesc array
    // into D3D12 root signature + Dispatch calls. This function only demonstrates
    // how to obtain dispatch descriptions.
    bool ApplyDenoise();

    nrd::Instance* GetInstance() const { return m_instance; }
    nrd::Identifier GetIdentifier() const { return m_identifier; }

private:
    nrd::Instance* m_instance;
    nrd::Identifier m_identifier;
    uint32_t m_width;
    uint32_t m_height;
};