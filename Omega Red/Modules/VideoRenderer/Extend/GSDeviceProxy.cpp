/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "stdafx.h"
#include "GSdx.h"
#include "GSDeviceProxy.h"
#include "GSUtil.h"
#include "resource.h"
#include <fstream>
#include "resource.h"
#include "GSTables.h"

bool GSDeviceProxy::CreateTextureFX()
{
    HRESULT hr;

    D3D11_BUFFER_DESC bd;

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(VSConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_vs_cb);

    if (FAILED(hr))
        return false;

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(GSConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_gs_cb);

    if (FAILED(hr))
        return false;

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(PSConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_ps_cb);

    if (FAILED(hr))
        return false;

    D3D11_SAMPLER_DESC sd;

    memset(&sd, 0, sizeof(sd));

    sd.Filter = theApp.GetConfigI("MaxAnisotropy") && !theApp.GetConfigB("paltex") ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MinLOD = -FLT_MAX;
    sd.MaxLOD = FLT_MAX;
    sd.MaxAnisotropy = theApp.GetConfigI("MaxAnisotropy");
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = m_dev->CreateSamplerState(&sd, &m_palette_ss);

    if (FAILED(hr))
        return false;

    hr = m_dev->CreateSamplerState(&sd, &m_rt_ss);

    if (FAILED(hr))
        return false;

    // create layout

    VSSelector sel;
    VSConstantBuffer cb;

    SetupVS(sel, &cb);

    GSConstantBuffer gcb;

    SetupGS(GSSelector(1), &gcb);

    //

    return true;
}

void GSDeviceProxy::SetupVS(VSSelector sel, const VSConstantBuffer *cb)
{
    auto i = std::as_const(m_vs).find(sel);

    if (i == m_vs.end()) {
        std::string str[4];

        str[0] = format("%d", sel.bppz);
        str[1] = format("%d", sel.tme);
        str[2] = format("%d", sel.fst);
        str[3] = format("%d", sel.rtcopy);

        D3D_SHADER_MACRO macro[] =
            {
                {"VS_BPPZ", str[0].c_str()},
                {"VS_TME", str[1].c_str()},
                {"VS_FST", str[2].c_str()},
                {"VS_RTCOPY", str[3].c_str()},
                {NULL, NULL},
            };

        D3D11_INPUT_ELEMENT_DESC layout[] =
            {
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"POSITION", 0, DXGI_FORMAT_R16G16_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"POSITION", 1, DXGI_FORMAT_R32_UINT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 2, DXGI_FORMAT_R16G16_UINT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };

        GSVertexShader11Proxy vs;

        std::vector<char> shader;
        theApp.LoadResource(IDR_TFX_FX, shader);
        CompileShader(shader.data(), shader.size(), "tfx.fx", nullptr, "vs_main", macro, &vs.vs, layout, countof(layout), &vs.il);

        m_vs[sel] = vs;

        i = m_vs.find(sel);
    }

    if (m_vs_cb_cache.Update(cb)) {
        ID3D11DeviceContext *ctx = m_ctx;

        ctx->UpdateSubresource(m_vs_cb, 0, NULL, cb, 0, 0);
    }

    VSSetShader(i->second.vs, m_vs_cb);

    IASetInputLayout(i->second.il);
}

void GSDeviceProxy::SetupGS(GSSelector sel, const GSConstantBuffer *cb)
{
    CComPtr<ID3D11GeometryShader> gs;

    bool Unscale_GSShader = (sel.point == 1 || sel.line == 1) && UserHacks_unscale_pt_ln;
    if ((sel.prim > 0 && (sel.iip == 0 || sel.prim == 3)) || Unscale_GSShader) // geometry shader works in every case, but not needed
    {
        auto i = std::as_const(m_gs).find(sel);

        if (i != m_gs.end()) {
            gs = i->second;
        } else {
            std::string str[4];

            str[0] = format("%d", sel.iip);
            str[1] = format("%d", sel.prim);
            str[2] = format("%d", sel.point);
            str[3] = format("%d", sel.line);

            D3D_SHADER_MACRO macro[] =
                {
                    {"GS_IIP", str[0].c_str()},
                    {"GS_PRIM", str[1].c_str()},
                    {"GS_POINT", str[2].c_str()},
                    {"GS_LINE", str[3].c_str()},
                    {NULL, NULL},
                };

            std::vector<char> shader;
            theApp.LoadResource(IDR_TFX_FX, shader);
            CompileShader(shader.data(), shader.size(), "tfx.fx", nullptr, "gs_main", macro, &gs);

            m_gs[sel] = gs;
        }
    }


    if (m_gs_cb_cache.Update(cb)) {
        ID3D11DeviceContext *ctx = m_ctx;

        ctx->UpdateSubresource(m_gs_cb, 0, NULL, cb, 0, 0);
    }

    GSSetShader(gs, m_gs_cb);
}

void GSDeviceProxy::SetupPS(PSSelector sel, const PSConstantBuffer *cb, PSSamplerSelector ssel)
{
    auto i = std::as_const(m_ps).find(sel);

    if (i == m_ps.end()) {
        std::string str[21];

        str[0] = format("%d", sel.fst);
        str[1] = format("%d", sel.wms);
        str[2] = format("%d", sel.wmt);
        str[3] = format("%d", sel.fmt);
        str[4] = format("%d", sel.aem);
        str[5] = format("%d", sel.tfx);
        str[6] = format("%d", sel.tcc);
        str[7] = format("%d", sel.atst);
        str[8] = format("%d", sel.fog);
        str[9] = format("%d", sel.clr1);
        str[10] = format("%d", sel.fba);
        str[11] = format("%d", sel.aout);
        str[12] = format("%d", sel.ltf);
        str[13] = format("%d", sel.colclip);
        str[14] = format("%d", sel.date);
        str[15] = format("%d", sel.spritehack);
        str[16] = format("%d", sel.tcoffsethack);
        str[17] = format("%d", sel.point_sampler);
        str[18] = format("%d", sel.shuffle);
        str[19] = format("%d", sel.read_ba);
        str[20] = format("%d", sel.fmt >> 2);

        D3D_SHADER_MACRO macro[] =
            {
                {"PS_FST", str[0].c_str()},
                {"PS_WMS", str[1].c_str()},
                {"PS_WMT", str[2].c_str()},
                {"PS_FMT", str[3].c_str()},
                {"PS_AEM", str[4].c_str()},
                {"PS_TFX", str[5].c_str()},
                {"PS_TCC", str[6].c_str()},
                {"PS_ATST", str[7].c_str()},
                {"PS_FOG", str[8].c_str()},
                {"PS_CLR1", str[9].c_str()},
                {"PS_FBA", str[10].c_str()},
                {"PS_AOUT", str[11].c_str()},
                {"PS_LTF", str[12].c_str()},
                {"PS_COLCLIP", str[13].c_str()},
                {"PS_DATE", str[14].c_str()},
                {"PS_SPRITEHACK", str[15].c_str()},
                {"PS_TCOFFSETHACK", str[16].c_str()},
                {"PS_POINT_SAMPLER", str[17].c_str()},
                {"PS_SHUFFLE", str[18].c_str()},
                {"PS_READ_BA", str[19].c_str()},
                {"PS_PAL_FMT", str[20].c_str()},
                {NULL, NULL},
            };

        CComPtr<ID3D11PixelShader> ps;

        std::vector<char> shader;
        theApp.LoadResource(IDR_TFX_FX, shader);
        CompileShader(shader.data(), shader.size(), "tfx.fx", nullptr, "ps_main", macro, &ps);

        m_ps[sel] = ps;

        i = m_ps.find(sel);
    }

    if (m_ps_cb_cache.Update(cb)) {
        ID3D11DeviceContext *ctx = m_ctx;

        ctx->UpdateSubresource(m_ps_cb, 0, NULL, cb, 0, 0);
    }

    CComPtr<ID3D11SamplerState> ss0, ss1;

    if (sel.tfx != 4) {
        if (!(sel.fmt < 3 && sel.wms < 3 && sel.wmt < 3)) {
            ssel.ltf = 0;
        }

        auto i = std::as_const(m_ps_ss).find(ssel);

        if (i != m_ps_ss.end()) {
            ss0 = i->second;
        } else {
            D3D11_SAMPLER_DESC sd, af;

            memset(&sd, 0, sizeof(sd));

            af.Filter = theApp.GetConfigI("MaxAnisotropy") && !theApp.GetConfigB("paltex") ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sd.Filter = ssel.ltf ? af.Filter : D3D11_FILTER_MIN_MAG_MIP_POINT;

            sd.AddressU = ssel.tau ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV = ssel.tav ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MinLOD = -FLT_MAX;
            sd.MaxLOD = FLT_MAX;
            sd.MaxAnisotropy = theApp.GetConfigI("MaxAnisotropy");
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

            m_dev->CreateSamplerState(&sd, &ss0);

            m_ps_ss[ssel] = ss0;
        }

        if (sel.fmt >= 3) {
            ss1 = m_palette_ss;
        }
    }

    PSSetSamplerState(ss0, ss1, sel.date ? m_rt_ss : NULL);

    PSSetShader(i->second, m_ps_cb);
}

void GSDeviceProxy::SetupOM(OMDepthStencilSelector dssel, OMBlendSelector bsel, uint8 afix)
{
    auto i = std::as_const(m_om_dss).find(dssel);

    if (i == m_om_dss.end()) {
        D3D11_DEPTH_STENCIL_DESC dsd;

        memset(&dsd, 0, sizeof(dsd));

        if (dssel.date) {
            dsd.StencilEnable = true;
            dsd.StencilReadMask = 1;
            dsd.StencilWriteMask = 1;
            dsd.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
            dsd.FrontFace.StencilPassOp = dssel.alpha_stencil ? D3D11_STENCIL_OP_ZERO : D3D11_STENCIL_OP_KEEP;
            dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd.BackFace.StencilFunc = D3D11_COMPARISON_EQUAL;
            dsd.BackFace.StencilPassOp = dssel.alpha_stencil ? D3D11_STENCIL_OP_ZERO : D3D11_STENCIL_OP_KEEP;
            dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        }

        if (dssel.ztst != ZTST_ALWAYS || dssel.zwe) {
            static const D3D11_COMPARISON_FUNC ztst[] =
                {
                    D3D11_COMPARISON_NEVER,
                    D3D11_COMPARISON_ALWAYS,
                    D3D11_COMPARISON_GREATER_EQUAL,
                    D3D11_COMPARISON_GREATER};

            dsd.DepthEnable = true;
            dsd.DepthWriteMask = dssel.zwe ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsd.DepthFunc = ztst[dssel.ztst];
        }

        CComPtr<ID3D11DepthStencilState> dss;

        m_dev->CreateDepthStencilState(&dsd, &dss);

        m_om_dss[dssel] = dss;

        i = m_om_dss.find(dssel);
    }

    OMSetDepthStencilState(i->second, 1);

    auto j = std::as_const(m_om_bs).find(bsel);

    if (j == m_om_bs.end()) {
        D3D11_BLEND_DESC bd;

        memset(&bd, 0, sizeof(bd));

        bd.RenderTarget[0].BlendEnable = bsel.abe;

        if (bsel.abe) {
            int i = ((bsel.a * 3 + bsel.b) * 3 + bsel.c) * 3 + bsel.d;

            bd.RenderTarget[0].BlendOp = (D3D11_BLEND_OP)m_blendMapD3D9[i].op;
            bd.RenderTarget[0].SrcBlend = (D3D11_BLEND)m_blendMapD3D9[i].src;
            bd.RenderTarget[0].DestBlend = (D3D11_BLEND)m_blendMapD3D9[i].dst;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

            // SRC* -> SRC1*
            // Yes, this casting mess really is needed.  I want to go back to C

            if (bd.RenderTarget[0].SrcBlend >= 3 && bd.RenderTarget[0].SrcBlend <= 6) {
                bd.RenderTarget[0].SrcBlend = (D3D11_BLEND)((int)bd.RenderTarget[0].SrcBlend + 13);
            }

            if (bd.RenderTarget[0].DestBlend >= 3 && bd.RenderTarget[0].DestBlend <= 6) {
                bd.RenderTarget[0].DestBlend = (D3D11_BLEND)((int)bd.RenderTarget[0].DestBlend + 13);
            }

            // Not very good but I don't wanna write another 81 row table

            if (bsel.negative) {
                if (bd.RenderTarget[0].BlendOp == D3D11_BLEND_OP_ADD) {
                    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
                } else if (bd.RenderTarget[0].BlendOp == D3D11_BLEND_OP_REV_SUBTRACT) {
                    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                } else
                    ; // god knows, best just not to mess with it for now
            }

            if (m_blendMapD3D9[i].bogus == 1) {
                (bsel.a == 0 ? bd.RenderTarget[0].SrcBlend : bd.RenderTarget[0].DestBlend) = D3D11_BLEND_ONE;

                const std::string afixstr = format("%d >> 7", afix);
                const char *col[3] = {"Cs", "Cd", "0"};
                const char *alpha[3] = {"As", "Ad", afixstr.c_str()};

                printf("Impossible blend for D3D: (%s - %s) * %s + %s\n", col[bsel.a], col[bsel.b], alpha[bsel.c], col[bsel.d]);
            }
        }

        if (bsel.wr)
            bd.RenderTarget[0].RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_RED;
        if (bsel.wg)
            bd.RenderTarget[0].RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
        if (bsel.wb)
            bd.RenderTarget[0].RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
        if (bsel.wa)
            bd.RenderTarget[0].RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;

        CComPtr<ID3D11BlendState> bs;

        m_dev->CreateBlendState(&bd, &bs);

        m_om_bs[bsel] = bs;

        j = m_om_bs.find(bsel);
    }

    OMSetBlendState(j->second, (float)(int)afix / 0x80);
}



GSDeviceProxy::GSDeviceProxy()
{
    memset(&m_state, 0, sizeof(m_state));
    memset(&m_vs_cb_cache, 0, sizeof(m_vs_cb_cache));
    memset(&m_gs_cb_cache, 0, sizeof(m_gs_cb_cache));
    memset(&m_ps_cb_cache, 0, sizeof(m_ps_cb_cache));

    FXAA_Compiled = false;
    ExShader_Compiled = false;

    m_state.topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    m_state.bf = -1;

    m_mipmap = theApp.GetConfigI("mipmap");

    if (theApp.GetConfigB("UserHacks")) {
        UserHacks_unscale_pt_ln = theApp.GetConfigB("UserHacks_unscale_point_line");
    } else {
        UserHacks_unscale_pt_ln = false;
    }

    updateCallbackInner = nullptr;
}

GSDeviceProxy::~GSDeviceProxy()
{
}

bool GSDeviceProxy::Create(const std::shared_ptr<GSWnd> &wnd)
{
    if (!__super::Create(wnd)) {
        return false;
    }

    HRESULT hr = E_FAIL;

    DXGI_SWAP_CHAIN_DESC scd;
    D3D11_BUFFER_DESC bd;
    D3D11_SAMPLER_DESC sd;
    D3D11_DEPTH_STENCIL_DESC dsd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_BLEND_DESC bsd;

    CComPtr<IDXGIAdapter1> adapter;
    D3D_DRIVER_TYPE driver_type = D3D_DRIVER_TYPE_HARDWARE;

    std::string adapter_id = theApp.GetConfigS("Adapter");

    if (adapter_id == "default")
        ;
    else if (adapter_id == "ref") {
        driver_type = D3D_DRIVER_TYPE_REFERENCE;
    } else {
        CComPtr<IDXGIFactory1> dxgi_factory;
        CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&dxgi_factory);
        if (dxgi_factory)
            for (int i = 0;; i++) {
                CComPtr<IDXGIAdapter1> enum_adapter;
                if (S_OK != dxgi_factory->EnumAdapters1(i, &enum_adapter))
                    break;
                DXGI_ADAPTER_DESC1 desc;
                hr = enum_adapter->GetDesc1(&desc);
                if (S_OK == hr && GSAdapter(desc) == adapter_id) {
                    adapter = enum_adapter;
                    driver_type = D3D_DRIVER_TYPE_UNKNOWN;
                    break;
                }
            }
    }

    memset(&scd, 0, sizeof(scd));

    scd.BufferCount = 2;
    scd.BufferDesc.Width = 1;
    scd.BufferDesc.Height = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    //scd.BufferDesc.RefreshRate.Numerator = 60;
    //scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = (HWND)m_wnd->GetHandle();
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;

    // Always start in Windowed mode.  According to MS, DXGI just "prefers" this, and it's more or less
    // required if we want to add support for dual displays later on.  The fullscreen/exclusive flip
    // will be issued after all other initializations are complete.

    scd.Windowed = TRUE;

    // NOTE : D3D11_CREATE_DEVICE_SINGLETHREADED
    //   This flag is safe as long as the DXGI's internal message pump is disabled or is on the
    //   same thread as the GS window (which the emulator makes sure of, if it utilizes a
    //   multithreaded GS).  Setting the flag is a nice and easy 5% speedup on GS-intensive scenes.

    uint32 flags = D3D11_CREATE_DEVICE_SINGLETHREADED;

#ifdef DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL level;

    const D3D_FEATURE_LEVEL levels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

    hr = D3D11CreateDeviceAndSwapChain(adapter, driver_type, NULL, flags, levels, countof(levels), D3D11_SDK_VERSION, &scd, &m_swapchain, &m_dev, &level, &m_ctx);

    if (FAILED(hr))
        return false;

    if (!SetFeatureLevel(level, true)) {
        return false;
    }

    { // HACK: check nVIDIA
        bool nvidia_gpu = false;
        IDXGIDevice *dxd;

        if (SUCCEEDED(m_dev->QueryInterface(IID_PPV_ARGS(&dxd)))) {
            IDXGIAdapter *dxa;

            if (SUCCEEDED(dxd->GetAdapter(&dxa))) {
                DXGI_ADAPTER_DESC dxad;

                if (SUCCEEDED(dxa->GetDesc(&dxad)))
                    nvidia_gpu = dxad.VendorId == 0x10DE;

                dxa->Release();
            }
            dxd->Release();
        }

        bool native_resolution = theApp.GetConfigI("upscale_multiplier") == 1;
        bool spritehack_enabled = theApp.GetConfigB("UserHacks") && theApp.GetConfigI("UserHacks_SpriteHack");

        m_hack_topleft_offset = (!nvidia_gpu || native_resolution || spritehack_enabled) ? 0.0f : -0.01f;
    }

    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS options;

    hr = m_dev->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &options, sizeof(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS));

    // msaa

    for (uint32 i = 2; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i++) {
        uint32 quality[2] = {0, 0};

        if (SUCCEEDED(m_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, i, &quality[0])) && quality[0] > 0 && SUCCEEDED(m_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, i, &quality[1])) && quality[1] > 0) {
            m_msaa_desc.Count = i;
            m_msaa_desc.Quality = std::min<uint32>(quality[0] - 1, quality[1] - 1);

            if (i >= m_msaa)
                break;
        }
    }

    if (m_msaa_desc.Count == 1) {
        m_msaa = 0;
    }

    // convert

    D3D11_INPUT_ELEMENT_DESC il_convert[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

    std::vector<char> shader;
    theApp.LoadResource(IDR_CONVERT_FX, shader);
    CompileShader(shader.data(), shader.size(), "convert.fx", nullptr, "vs_main", nullptr, &m_convert.vs, il_convert, countof(il_convert), &m_convert.il);

    for (size_t i = 0; i < countof(m_convert.ps); i++) {
        CompileShader(shader.data(), shader.size(), "convert.fx", nullptr, format("ps_main%d", i).c_str(), nullptr, &m_convert.ps[i]);
    }

    memset(&dsd, 0, sizeof(dsd));

    dsd.DepthEnable = false;
    dsd.StencilEnable = false;

    hr = m_dev->CreateDepthStencilState(&dsd, &m_convert.dss);

    memset(&bsd, 0, sizeof(bsd));

    bsd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_dev->CreateBlendState(&bsd, &m_convert.bs);

    // merge

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(MergeConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_merge.cb);

    theApp.LoadResource(IDR_MERGE_FX, shader);
    for (size_t i = 0; i < countof(m_merge.ps); i++) {
        CompileShader(shader.data(), shader.size(), "merge.fx", nullptr, format("ps_main%d", i).c_str(), nullptr, &m_merge.ps[i]);
    }

    memset(&bsd, 0, sizeof(bsd));

    bsd.RenderTarget[0].BlendEnable = true;
    bsd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bsd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bsd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bsd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bsd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_dev->CreateBlendState(&bsd, &m_merge.bs);

    // interlace

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(InterlaceConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_interlace.cb);

    theApp.LoadResource(IDR_INTERLACE_FX, shader);
    for (size_t i = 0; i < countof(m_interlace.ps); i++) {
        CompileShader(shader.data(), shader.size(), "interlace.fx", nullptr, format("ps_main%d", i).c_str(), nullptr, &m_interlace.ps[i]);
    }

    // Shade Boos

    int ShadeBoost_Contrast = theApp.GetConfigI("ShadeBoost_Contrast");
    int ShadeBoost_Brightness = theApp.GetConfigI("ShadeBoost_Brightness");
    int ShadeBoost_Saturation = theApp.GetConfigI("ShadeBoost_Saturation");

    std::string str[3];

    str[0] = format("%d", ShadeBoost_Saturation);
    str[1] = format("%d", ShadeBoost_Brightness);
    str[2] = format("%d", ShadeBoost_Contrast);

    D3D_SHADER_MACRO macro[] =
        {
            {"SB_SATURATION", str[0].c_str()},
            {"SB_BRIGHTNESS", str[1].c_str()},
            {"SB_CONTRAST", str[2].c_str()},
            {NULL, NULL},
        };

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(ShadeBoostConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_shadeboost.cb);

    theApp.LoadResource(IDR_SHADEBOOST_FX, shader);
    CompileShader(shader.data(), shader.size(), "shadeboost.fx", nullptr, "ps_main", macro, &m_shadeboost.ps);

    // External fx shader

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(ExternalFXConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_shaderfx.cb);

    // Fxaa

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(FXAAConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_fxaa.cb);

    //

    memset(&rd, 0, sizeof(rd));

    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = false;
    rd.DepthBias = false;
    rd.DepthBiasClamp = 0;
    rd.SlopeScaledDepthBias = 0;
    rd.DepthClipEnable = false; // ???
    rd.ScissorEnable = true;
    rd.MultisampleEnable = true;
    rd.AntialiasedLineEnable = false;

    hr = m_dev->CreateRasterizerState(&rd, &m_rs);

    m_ctx->RSSetState(m_rs);

    //

    memset(&sd, 0, sizeof(sd));

    sd.Filter = theApp.GetConfigI("MaxAnisotropy") && !theApp.GetConfigB("paltex") ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MinLOD = -FLT_MAX;
    sd.MaxLOD = FLT_MAX;
    sd.MaxAnisotropy = theApp.GetConfigI("MaxAnisotropy");
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = m_dev->CreateSamplerState(&sd, &m_convert.ln);

    sd.Filter = theApp.GetConfigI("MaxAnisotropy") && !theApp.GetConfigB("paltex") ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_POINT;

    hr = m_dev->CreateSamplerState(&sd, &m_convert.pt);

    //

    Reset(1, 1);

    //

    CreateTextureFX();

    //

    memset(&dsd, 0, sizeof(dsd));

    dsd.DepthEnable = false;
    dsd.StencilEnable = true;
    dsd.StencilReadMask = 1;
    dsd.StencilWriteMask = 1;
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

    m_dev->CreateDepthStencilState(&dsd, &m_date.dss);

    D3D11_BLEND_DESC blend;

    memset(&blend, 0, sizeof(blend));

    m_dev->CreateBlendState(&blend, &m_date.bs);

    // Exclusive/Fullscreen flip, issued for legacy (managed) windows only.  GSopen2 style
    // emulators will issue the flip themselves later on.

    if (m_wnd->IsManaged()) {
        SetExclusive(!theApp.GetConfigB("windowed"));
    }

    return true;
}

bool GSDeviceProxy::Create(const std::shared_ptr<GSWnd> &wnd, void *sharedhandle, void *updateCallback, IUnknown **aPtrPtrUnkRenderingTexture)
{
    if (!__super::Create(wnd)) {
        return false;
    }

    HRESULT hr = E_FAIL;

    //DXGI_SWAP_CHAIN_DESC scd;
    D3D11_BUFFER_DESC bd;
    D3D11_SAMPLER_DESC sd;
    D3D11_DEPTH_STENCIL_DESC dsd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_BLEND_DESC bsd;

    CComPtr<IDXGIAdapter1> adapter;
    D3D_DRIVER_TYPE driver_type = D3D_DRIVER_TYPE_HARDWARE;

    std::string adapter_id = theApp.GetConfigS("Adapter");

    if (adapter_id == "default")
        ;
    else if (adapter_id == "ref") {
        driver_type = D3D_DRIVER_TYPE_REFERENCE;
    } else {
        CComPtr<IDXGIFactory1> dxgi_factory;
        CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&dxgi_factory);
        if (dxgi_factory)
            for (int i = 0;; i++) {
                CComPtr<IDXGIAdapter1> enum_adapter;
                if (S_OK != dxgi_factory->EnumAdapters1(i, &enum_adapter))
                    break;
                DXGI_ADAPTER_DESC1 desc;
                hr = enum_adapter->GetDesc1(&desc);
                if (S_OK == hr && GSAdapter(desc) == adapter_id) {
                    adapter = enum_adapter;
                    driver_type = D3D_DRIVER_TYPE_UNKNOWN;
                    break;
                }
            }
    }

    //memset(&scd, 0, sizeof(scd));

    //scd.BufferCount = 2;
    //scd.BufferDesc.Width = 1;
    //scd.BufferDesc.Height = 1;
    //scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ////scd.BufferDesc.RefreshRate.Numerator = 60;
    ////scd.BufferDesc.RefreshRate.Denominator = 1;
    //scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    //scd.OutputWindow = (HWND)m_wnd->GetHandle();
    //scd.SampleDesc.Count = 1;
    //scd.SampleDesc.Quality = 0;

    //// Always start in Windowed mode.  According to MS, DXGI just "prefers" this, and it's more or less
    //// required if we want to add support for dual displays later on.  The fullscreen/exclusive flip
    //// will be issued after all other initializations are complete.

    //scd.Windowed = TRUE;

    // NOTE : D3D11_CREATE_DEVICE_SINGLETHREADED
    //   This flag is safe as long as the DXGI's internal message pump is disabled or is on the
    //   same thread as the GS window (which the emulator makes sure of, if it utilizes a
    //   multithreaded GS).  Setting the flag is a nice and easy 5% speedup on GS-intensive scenes.

    uint32 flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; //D3D11_CREATE_DEVICE_SINGLETHREADED;

#ifdef DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL level;

    const D3D_FEATURE_LEVEL levels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

    //hr = D3D11CreateDeviceAndSwapChain(adapter, driver_type, NULL, flags, levels, countof(levels), D3D11_SDK_VERSION, &scd, &m_swapchain, &m_dev, &level, &m_ctx);
    hr = D3D11CreateDevice(adapter, driver_type, NULL, flags, levels, countof(levels), D3D11_SDK_VERSION, &m_dev, &level, &m_ctx);
	
    if (FAILED(hr))
        return false;

    if (!SetFeatureLevel(level, true)) {
        return false;
    }

    { // HACK: check nVIDIA
        bool nvidia_gpu = false;
        IDXGIDevice *dxd;

        if (SUCCEEDED(m_dev->QueryInterface(IID_PPV_ARGS(&dxd)))) {
            IDXGIAdapter *dxa;

            if (SUCCEEDED(dxd->GetAdapter(&dxa))) {
                DXGI_ADAPTER_DESC dxad;

                if (SUCCEEDED(dxa->GetDesc(&dxad)))
                    nvidia_gpu = dxad.VendorId == 0x10DE;

                dxa->Release();
            }
            dxd->Release();
        }

        bool native_resolution = theApp.GetConfigI("upscale_multiplier") == 1;
        bool spritehack_enabled = theApp.GetConfigB("UserHacks") && theApp.GetConfigI("UserHacks_SpriteHack");

        m_hack_topleft_offset = (!nvidia_gpu || native_resolution || spritehack_enabled) ? 0.0f : -0.01f;
    }

    // Create shared texture

    CComPtr<ID3D11Resource> l_Resource;

    hr = m_dev->OpenSharedResource(sharedhandle, IID_PPV_ARGS(&l_Resource));

    if (FAILED(hr))
        return false;

    hr = l_Resource->QueryInterface(IID_PPV_ARGS(&m_SharedTexture));

    if (FAILED(hr))
        return false;

    D3D11_TEXTURE2D_DESC l_Desc;

    m_SharedTexture->GetDesc(&l_Desc);

    m_RenderTargetTexture.Release();

    hr = m_dev->CreateTexture2D(&l_Desc, nullptr, &m_RenderTargetTexture);

    if (FAILED(hr))
        return false;

    m_SharedTexture->QueryInterface(aPtrPtrUnkRenderingTexture);

    updateCallbackInner = (UpdateCallback)updateCallback;

    if (!wnd->Create("GS", l_Desc.Width, l_Desc.Height)) {
        return -1;
    }


    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS options;

    hr = m_dev->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &options, sizeof(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS));

    // msaa

    for (uint32 i = 2; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; i++) {
        uint32 quality[2] = {0, 0};

        if (SUCCEEDED(m_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, i, &quality[0])) && quality[0] > 0 && SUCCEEDED(m_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, i, &quality[1])) && quality[1] > 0) {
            m_msaa_desc.Count = i;
            m_msaa_desc.Quality = std::min<uint32>(quality[0] - 1, quality[1] - 1);

            if (i >= m_msaa)
                break;
        }
    }

    if (m_msaa_desc.Count == 1) {
        m_msaa = 0;
    }

    // convert

    D3D11_INPUT_ELEMENT_DESC il_convert[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

    std::vector<char> shader;
    theApp.LoadResource(IDR_CONVERT_FX, shader);
    CompileShader(shader.data(), shader.size(), "convert.fx", nullptr, "vs_main", nullptr, &m_convert.vs, il_convert, countof(il_convert), &m_convert.il);

    for (size_t i = 0; i < countof(m_convert.ps); i++) {
        CompileShader(shader.data(), shader.size(), "convert.fx", nullptr, format("ps_main%d", i).c_str(), nullptr, &m_convert.ps[i]);
    }

    memset(&dsd, 0, sizeof(dsd));

    dsd.DepthEnable = false;
    dsd.StencilEnable = false;

    hr = m_dev->CreateDepthStencilState(&dsd, &m_convert.dss);

    memset(&bsd, 0, sizeof(bsd));

    bsd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_dev->CreateBlendState(&bsd, &m_convert.bs);

    // merge

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(MergeConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_merge.cb);

    theApp.LoadResource(IDR_MERGE_FX, shader);
    for (size_t i = 0; i < countof(m_merge.ps); i++) {
        CompileShader(shader.data(), shader.size(), "merge.fx", nullptr, format("ps_main%d", i).c_str(), nullptr, &m_merge.ps[i]);
    }

    memset(&bsd, 0, sizeof(bsd));

    bsd.RenderTarget[0].BlendEnable = true;
    bsd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bsd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bsd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bsd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bsd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = m_dev->CreateBlendState(&bsd, &m_merge.bs);

    // interlace

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(InterlaceConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_interlace.cb);

    theApp.LoadResource(IDR_INTERLACE_FX, shader);
    for (size_t i = 0; i < countof(m_interlace.ps); i++) {
        CompileShader(shader.data(), shader.size(), "interlace.fx", nullptr, format("ps_main%d", i).c_str(), nullptr, &m_interlace.ps[i]);
    }

    // Shade Boos

    int ShadeBoost_Contrast = theApp.GetConfigI("ShadeBoost_Contrast");
    int ShadeBoost_Brightness = theApp.GetConfigI("ShadeBoost_Brightness");
    int ShadeBoost_Saturation = theApp.GetConfigI("ShadeBoost_Saturation");

    std::string str[3];

    str[0] = format("%d", ShadeBoost_Saturation);
    str[1] = format("%d", ShadeBoost_Brightness);
    str[2] = format("%d", ShadeBoost_Contrast);

    D3D_SHADER_MACRO macro[] =
        {
            {"SB_SATURATION", str[0].c_str()},
            {"SB_BRIGHTNESS", str[1].c_str()},
            {"SB_CONTRAST", str[2].c_str()},
            {NULL, NULL},
        };

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(ShadeBoostConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_shadeboost.cb);

    theApp.LoadResource(IDR_SHADEBOOST_FX, shader);
    CompileShader(shader.data(), shader.size(), "shadeboost.fx", nullptr, "ps_main", macro, &m_shadeboost.ps);

    // External fx shader

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(ExternalFXConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_shaderfx.cb);

    // Fxaa

    memset(&bd, 0, sizeof(bd));

    bd.ByteWidth = sizeof(FXAAConstantBuffer);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_dev->CreateBuffer(&bd, NULL, &m_fxaa.cb);

    //

    memset(&rd, 0, sizeof(rd));

    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = false;
    rd.DepthBias = false;
    rd.DepthBiasClamp = 0;
    rd.SlopeScaledDepthBias = 0;
    rd.DepthClipEnable = false; // ???
    rd.ScissorEnable = true;
    rd.MultisampleEnable = true;
    rd.AntialiasedLineEnable = false;

    hr = m_dev->CreateRasterizerState(&rd, &m_rs);

    m_ctx->RSSetState(m_rs);

    //

    memset(&sd, 0, sizeof(sd));

    sd.Filter = theApp.GetConfigI("MaxAnisotropy") && !theApp.GetConfigB("paltex") ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MinLOD = -FLT_MAX;
    sd.MaxLOD = FLT_MAX;
    sd.MaxAnisotropy = theApp.GetConfigI("MaxAnisotropy");
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = m_dev->CreateSamplerState(&sd, &m_convert.ln);

    sd.Filter = theApp.GetConfigI("MaxAnisotropy") && !theApp.GetConfigB("paltex") ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_POINT;

    hr = m_dev->CreateSamplerState(&sd, &m_convert.pt);

    //

    Reset(1, 1);

    //

    CreateTextureFX();

    //

    memset(&dsd, 0, sizeof(dsd));

    dsd.DepthEnable = false;
    dsd.StencilEnable = true;
    dsd.StencilReadMask = 1;
    dsd.StencilWriteMask = 1;
    dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsd.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    dsd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;

    m_dev->CreateDepthStencilState(&dsd, &m_date.dss);

    D3D11_BLEND_DESC blend;

    memset(&blend, 0, sizeof(blend));

    m_dev->CreateBlendState(&blend, &m_date.bs);

    // Exclusive/Fullscreen flip, issued for legacy (managed) windows only.  GSopen2 style
    // emulators will issue the flip themselves later on.

    if (m_wnd->IsManaged()) {
        SetExclusive(!theApp.GetConfigB("windowed"));
    }

    return true;
}

bool GSDeviceProxy::Reset(int w, int h)
{

    if (!__super::Reset(w, h))
        return false;

    if (m_RenderTargetTexture)
        m_backbuffer = new GSTexture11(m_RenderTargetTexture);
    else
        return false;

    return true;

    return true;
}

void GSDeviceProxy::SetExclusive(bool isExcl)
{
    if (!m_swapchain)
        return;

    // TODO : Support for alternative display modes, by finishing this code below:
    //  Video mode info should be pulled form config/ini.

    /*DXGI_MODE_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.RefreshRate = 0;		// must be zero for best results.

	m_swapchain->ResizeTarget(&desc);
	*/

    HRESULT hr = m_swapchain->SetFullscreenState(isExcl, NULL);

    if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
        fprintf(stderr, "(GSdx10) SetExclusive(%s) failed; request unavailable.", isExcl ? "true" : "false");
    }
}

void GSDeviceProxy::SetVSync(int vsync)
{
    m_vsync = vsync ? 1 : 0;
}

void GSDeviceProxy::Flip()
{
    m_ctx->Flush();

    m_ctx->CopyResource(m_SharedTexture, m_RenderTargetTexture);

    if (updateCallbackInner != nullptr)
        updateCallbackInner();
}

void GSDeviceProxy::DrawPrimitive()
{
    m_ctx->Draw(m_vertex.count, m_vertex.start);
}

void GSDeviceProxy::DrawIndexedPrimitive()
{
    m_ctx->DrawIndexed(m_index.count, m_index.start, m_vertex.start);
}

void GSDeviceProxy::DrawIndexedPrimitive(int offset, int count)
{
    ASSERT(offset + count <= (int)m_index.count);

    m_ctx->DrawIndexed(count, m_index.start + offset, m_vertex.start);
}

void GSDeviceProxy::Dispatch(uint32 x, uint32 y, uint32 z)
{
    m_ctx->Dispatch(x, y, z);
}

void GSDeviceProxy::ClearRenderTarget(GSTexture *t, const GSVector4 &c)
{
    if (!t)
        return;
    m_ctx->ClearRenderTargetView(*(GSTexture11 *)t, c.v);
}

void GSDeviceProxy::ClearRenderTarget(GSTexture *t, uint32 c)
{
    if (!t)
        return;
    GSVector4 color = GSVector4::rgba32(c) * (1.0f / 255);

    m_ctx->ClearRenderTargetView(*(GSTexture11 *)t, color.v);
}

void GSDeviceProxy::ClearDepth(GSTexture *t)
{
    if (!t)
        return;
    m_ctx->ClearDepthStencilView(*(GSTexture11 *)t, D3D11_CLEAR_DEPTH, 0.0f, 0);
}

void GSDeviceProxy::ClearStencil(GSTexture *t, uint8 c)
{
    if (!t)
        return;
    m_ctx->ClearDepthStencilView(*(GSTexture11 *)t, D3D11_CLEAR_STENCIL, 0, c);
}

GSTexture *GSDeviceProxy::CreateSurface(int type, int w, int h, bool msaa, int format)
{
    HRESULT hr;

    D3D11_TEXTURE2D_DESC desc;

    memset(&desc, 0, sizeof(desc));

    desc.Width = w;
    desc.Height = h;
    desc.Format = (DXGI_FORMAT)format;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;

    if (msaa) {
        desc.SampleDesc = m_msaa_desc;
    }

    // mipmap = m_mipmap > 1 || m_filter != TriFiltering::None;
    bool mipmap = m_mipmap > 1;
    int layers = mipmap && format == DXGI_FORMAT_R8G8B8A8_UNORM ? (int)log2(std::max(w, h)) : 1;

    switch (type) {
        case GSTexture::RenderTarget:
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            break;
        case GSTexture::DepthStencil:
            desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            break;
        case GSTexture::Texture:
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MipLevels = layers;
            break;
        case GSTexture::Offscreen:
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            break;
    }

    GSTexture11 *t = NULL;

    CComPtr<ID3D11Texture2D> texture;

    hr = m_dev->CreateTexture2D(&desc, NULL, &texture);

    if (SUCCEEDED(hr)) {
        t = new GSTexture11(texture);

        switch (type) {
            case GSTexture::RenderTarget:
                ClearRenderTarget(t, 0);
                break;
            case GSTexture::DepthStencil:
                ClearDepth(t);
                break;
        }
    } else {
        throw std::bad_alloc();
    }

    return t;
}

GSTexture *GSDeviceProxy::CreateRenderTarget(int w, int h, bool msaa, int format)
{
    return __super::CreateRenderTarget(w, h, msaa, format ? format : DXGI_FORMAT_R8G8B8A8_UNORM);
}

GSTexture *GSDeviceProxy::CreateDepthStencil(int w, int h, bool msaa, int format)
{
    return __super::CreateDepthStencil(w, h, msaa, format ? format : DXGI_FORMAT_D32_FLOAT_S8X24_UINT); // DXGI_FORMAT_R32G8X24_TYPELESS
}

GSTexture *GSDeviceProxy::CreateTexture(int w, int h, int format)
{
    return __super::CreateTexture(w, h, format ? format : DXGI_FORMAT_R8G8B8A8_UNORM);
}

GSTexture *GSDeviceProxy::CreateOffscreen(int w, int h, int format)
{
    return __super::CreateOffscreen(w, h, format ? format : DXGI_FORMAT_R8G8B8A8_UNORM);
}

GSTexture *GSDeviceProxy::Resolve(GSTexture *t)
{
    ASSERT(t != NULL && t->IsMSAA());

    if (GSTexture *dst = CreateRenderTarget(t->GetWidth(), t->GetHeight(), false, t->GetFormat())) {
        dst->SetScale(t->GetScale());

        m_ctx->ResolveSubresource(*(GSTexture11 *)dst, 0, *(GSTexture11 *)t, 0, (DXGI_FORMAT)t->GetFormat());

        return dst;
    }

    return NULL;
}

GSTexture *GSDeviceProxy::CopyOffscreen(GSTexture *src, const GSVector4 &sRect, int w, int h, int format, int ps_shader)
{
    GSTexture *dst = NULL;

    if (format == 0) {
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    if (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_R16_UINT) {
        ASSERT(0);

        return false;
    }

    if (GSTexture *rt = CreateRenderTarget(w, h, false, format)) {
        GSVector4 dRect(0, 0, w, h);

        if (GSTexture *src2 = src->IsMSAA() ? Resolve(src) : src) {
            StretchRect(src2, sRect, rt, dRect, m_convert.ps[format == DXGI_FORMAT_R16_UINT ? 1 : 0], NULL);

            if (src2 != src)
                Recycle(src2);
        }

        dst = CreateOffscreen(w, h, format);

        if (dst) {
            m_ctx->CopyResource(*(GSTexture11 *)dst, *(GSTexture11 *)rt);
        }

        Recycle(rt);
    }

    return dst;
}

void GSDeviceProxy::CopyRect(GSTexture *sTex, GSTexture *dTex, const GSVector4i &r)
{
    if (!sTex || !dTex) {
        ASSERT(0);
        return;
    }

    D3D11_BOX box = {(UINT)r.left, (UINT)r.top, 0U, (UINT)r.right, (UINT)r.bottom, 1U};

    m_ctx->CopySubresourceRegion(*(GSTexture11 *)dTex, 0, 0, 0, 0, *(GSTexture11 *)sTex, 0, &box);
}

void GSDeviceProxy::StretchRect(GSTexture *sTex, const GSVector4 &sRect, GSTexture *dTex, const GSVector4 &dRect, int shader, bool linear)
{
    StretchRect(sTex, sRect, dTex, dRect, m_convert.ps[shader], NULL, linear);
}

void GSDeviceProxy::StretchRect(GSTexture *sTex, const GSVector4 &sRect, GSTexture *dTex, const GSVector4 &dRect, ID3D11PixelShader *ps, ID3D11Buffer *ps_cb, bool linear)
{
    StretchRect(sTex, sRect, dTex, dRect, ps, ps_cb, m_convert.bs, linear);
}

void GSDeviceProxy::StretchRect(GSTexture *sTex, const GSVector4 &sRect, GSTexture *dTex, const GSVector4 &dRect, ID3D11PixelShader *ps, ID3D11Buffer *ps_cb, ID3D11BlendState *bs, bool linear)
{
    if (!sTex || !dTex) {
        ASSERT(0);
        return;
    }

    BeginScene();

    GSVector2i ds = dTex->GetSize();

    // om

    OMSetDepthStencilState(m_convert.dss, 0);
    OMSetBlendState(bs, 0);
    OMSetRenderTargets(dTex, NULL);

    // ia

    float left = dRect.x * 2 / ds.x - 1.0f;
    float top = 1.0f - dRect.y * 2 / ds.y;
    float right = dRect.z * 2 / ds.x - 1.0f;
    float bottom = 1.0f - dRect.w * 2 / ds.y;

    GSVertexPT1 vertices[] =
        {
            {GSVector4(left, top, 0.5f, 1.0f), GSVector2(sRect.x, sRect.y)},
            {GSVector4(right, top, 0.5f, 1.0f), GSVector2(sRect.z, sRect.y)},
            {GSVector4(left, bottom, 0.5f, 1.0f), GSVector2(sRect.x, sRect.w)},
            {GSVector4(right, bottom, 0.5f, 1.0f), GSVector2(sRect.z, sRect.w)},
        };



    IASetVertexBuffer(vertices, sizeof(vertices[0]), countof(vertices));
    IASetInputLayout(m_convert.il);
    IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // vs

    VSSetShader(m_convert.vs, NULL);


    // gs
    /* NVIDIA HACK!!!!
	Not sure why, but having the Geometry shader disabled causes the strange stretching in recent drivers*/

    GSSelector sel;
    //Don't use shading for stretching, we're just passing through - Note: With Win10 it seems to cause other bugs when shading is off if any of the coords is greater than 0
    //I really don't know whats going on there, but this seems to resolve it mostly (if not all, not tester a lot of games, only BIOS, FFXII and VP2)
    //sel.iip = (sRect.y > 0.0f || sRect.w > 0.0f) ? 1 : 0;
    //sel.prim = 2; //Triangle Strip
    //SetupGS(sel);

    GSSetShader(NULL, NULL);

    /*END OF HACK*/

    //

    // ps

    PSSetShaderResources(sTex, NULL);
    PSSetSamplerState(linear ? m_convert.ln : m_convert.pt, NULL);
    PSSetShader(ps, ps_cb);

    //

    DrawPrimitive();

    //

    EndScene();

    PSSetShaderResources(NULL, NULL);
}

void GSDeviceProxy::DoMerge(GSTexture *sTex[3], GSVector4 *sRect, GSTexture *dTex, GSVector4 *dRect, const GSRegPMODE &PMODE, const GSRegEXTBUF &EXTBUF, const GSVector4 &c)
{
    bool slbg = PMODE.SLBG;
    bool mmod = PMODE.MMOD;

    ClearRenderTarget(dTex, c);

    if (sTex[1] && !slbg) {
        StretchRect(sTex[1], sRect[1], dTex, dRect[1], m_merge.ps[0], NULL, true);
    }

    if (sTex[0]) {
        m_ctx->UpdateSubresource(m_merge.cb, 0, NULL, &c, 0, 0);

        StretchRect(sTex[0], sRect[0], dTex, dRect[0], m_merge.ps[mmod ? 1 : 0], m_merge.cb, m_merge.bs, true);
    }
}

void GSDeviceProxy::DoInterlace(GSTexture *sTex, GSTexture *dTex, int shader, bool linear, float yoffset)
{
    GSVector4 s = GSVector4(dTex->GetSize());

    GSVector4 sRect(0, 0, 1, 1);
    GSVector4 dRect(0.0f, yoffset, s.x, s.y + yoffset);

    InterlaceConstantBuffer cb;

    cb.ZrH = GSVector2(0, 1.0f / s.y);
    cb.hH = s.y / 2;

    m_ctx->UpdateSubresource(m_interlace.cb, 0, NULL, &cb, 0, 0);

    StretchRect(sTex, sRect, dTex, dRect, m_interlace.ps[shader], m_interlace.cb, linear);
}

//Included an init function for this also. Just to be safe.
void GSDeviceProxy::InitExternalFX()
{
    if (!ExShader_Compiled) {
        try {
            std::string config_name(theApp.GetConfigS("shaderfx_conf"));
            std::ifstream fconfig(config_name);
            std::stringstream shader;
            if (fconfig.good())
                shader << fconfig.rdbuf() << "\n";
            else
                fprintf(stderr, "GSdx: External shader config '%s' not loaded.\n", config_name.c_str());

            std::string shader_name(theApp.GetConfigS("shaderfx_glsl"));
            std::ifstream fshader(shader_name);
            if (fshader.good()) {
                shader << fshader.rdbuf();
                CompileShader(shader.str().c_str(), shader.str().length(), shader_name.c_str(), D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_main", nullptr, &m_shaderfx.ps);
            } else {
                fprintf(stderr, "GSdx: External shader '%s' not loaded and will be disabled!\n", shader_name.c_str());
            }
        } catch (GSDXRecoverableError) {
            printf("GSdx: failed to compile external post-processing shader. \n");
        }
        ExShader_Compiled = true;
    }
}

void GSDeviceProxy::DoExternalFX(GSTexture *sTex, GSTexture *dTex)
{
    GSVector2i s = dTex->GetSize();

    GSVector4 sRect(0, 0, 1, 1);
    GSVector4 dRect(0, 0, s.x, s.y);

    ExternalFXConstantBuffer cb;

    InitExternalFX();

    cb.xyFrame = GSVector2((float)s.x, (float)s.y);
    cb.rcpFrame = GSVector4(1.0f / (float)s.x, 1.0f / (float)s.y, 0.0f, 0.0f);
    cb.rcpFrameOpt = GSVector4::zero();

    m_ctx->UpdateSubresource(m_shaderfx.cb, 0, NULL, &cb, 0, 0);

    StretchRect(sTex, sRect, dTex, dRect, m_shaderfx.ps, m_shaderfx.cb, true);
}

// This shouldn't be necessary, we have some bug corrupting memory
// and for some reason isolating this code makes the plugin not crash
void GSDeviceProxy::InitFXAA()
{
    if (!FXAA_Compiled) {
        try {
            std::vector<char> shader;
            theApp.LoadResource(IDR_FXAA_FX, shader);
            CompileShader(shader.data(), shader.size(), "fxaa.fx", nullptr, "ps_main", nullptr, &m_fxaa.ps);
        } catch (GSDXRecoverableError) {
            printf("GSdx: failed to compile fxaa shader.\n");
        }
        FXAA_Compiled = true;
    }
}

void GSDeviceProxy::DoFXAA(GSTexture *sTex, GSTexture *dTex)
{
    GSVector2i s = dTex->GetSize();

    GSVector4 sRect(0, 0, 1, 1);
    GSVector4 dRect(0, 0, s.x, s.y);

    FXAAConstantBuffer cb;

    InitFXAA();

    cb.rcpFrame = GSVector4(1.0f / s.x, 1.0f / s.y, 0.0f, 0.0f);
    cb.rcpFrameOpt = GSVector4::zero();

    m_ctx->UpdateSubresource(m_fxaa.cb, 0, NULL, &cb, 0, 0);

    StretchRect(sTex, sRect, dTex, dRect, m_fxaa.ps, m_fxaa.cb, true);

    //sTex->Save("c:\\temp1\\1.bmp");
    //dTex->Save("c:\\temp1\\2.bmp");
}

void GSDeviceProxy::DoShadeBoost(GSTexture *sTex, GSTexture *dTex)
{
    GSVector2i s = dTex->GetSize();

    GSVector4 sRect(0, 0, 1, 1);
    GSVector4 dRect(0, 0, s.x, s.y);

    ShadeBoostConstantBuffer cb;

    cb.rcpFrame = GSVector4(1.0f / s.x, 1.0f / s.y, 0.0f, 0.0f);
    cb.rcpFrameOpt = GSVector4::zero();

    m_ctx->UpdateSubresource(m_shadeboost.cb, 0, NULL, &cb, 0, 0);

    StretchRect(sTex, sRect, dTex, dRect, m_shadeboost.ps, m_shadeboost.cb, true);
}

void GSDeviceProxy::SetupDATE(GSTexture *rt, GSTexture *ds, const GSVertexPT1 *vertices, bool datm)
{
    // sfex3 (after the capcom logo), vf4 (first menu fading in), ffxii shadows, rumble roses shadows, persona4 shadows

    BeginScene();

    ClearStencil(ds, 0);

    // om

    OMSetDepthStencilState(m_date.dss, 1);
    OMSetBlendState(m_date.bs, 0);
    OMSetRenderTargets(NULL, ds);

    // ia

    IASetVertexBuffer(vertices, sizeof(vertices[0]), 4);
    IASetInputLayout(m_convert.il);
    IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // vs

    VSSetShader(m_convert.vs, NULL);

    // gs

    GSSetShader(NULL, NULL);

    // ps

    GSTexture *rt2 = rt->IsMSAA() ? Resolve(rt) : rt;

    PSSetShaderResources(rt2, NULL);
    PSSetSamplerState(m_convert.pt, NULL);
    PSSetShader(m_convert.ps[datm ? 2 : 3], NULL);

    //

    DrawPrimitive();

    //

    EndScene();

    if (rt2 != rt)
        Recycle(rt2);
}

void GSDeviceProxy::IASetVertexBuffer(const void *vertex, size_t stride, size_t count)
{
    void *ptr = NULL;

    if (IAMapVertexBuffer(&ptr, stride, count)) {
        GSVector4i::storent(ptr, vertex, count * stride);

        IAUnmapVertexBuffer();
    }
}

bool GSDeviceProxy::IAMapVertexBuffer(void **vertex, size_t stride, size_t count)
{
    ASSERT(m_vertex.count == 0);

    if (count * stride > m_vertex.limit * m_vertex.stride) {
        m_vb_old = m_vb;
        m_vb = NULL;

        m_vertex.start = 0;
        m_vertex.limit = std::max<int>(count * 3 / 2, 11000);
    }

    if (m_vb == NULL) {
        D3D11_BUFFER_DESC bd;

        memset(&bd, 0, sizeof(bd));

        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = m_vertex.limit * stride;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr;

        hr = m_dev->CreateBuffer(&bd, NULL, &m_vb);

        if (FAILED(hr))
            return false;
    }

    D3D11_MAP type = D3D11_MAP_WRITE_NO_OVERWRITE;

    if (m_vertex.start + count > m_vertex.limit || stride != m_vertex.stride) {
        m_vertex.start = 0;

        type = D3D11_MAP_WRITE_DISCARD;
    }

    D3D11_MAPPED_SUBRESOURCE m;

    if (FAILED(m_ctx->Map(m_vb, 0, type, 0, &m))) {
        return false;
    }

    *vertex = (uint8 *)m.pData + m_vertex.start * stride;

    m_vertex.count = count;
    m_vertex.stride = stride;

    return true;
}

void GSDeviceProxy::IAUnmapVertexBuffer()
{
    m_ctx->Unmap(m_vb, 0);

    IASetVertexBuffer(m_vb, m_vertex.stride);
}

void GSDeviceProxy::IASetVertexBuffer(ID3D11Buffer *vb, size_t stride)
{
    if (m_state.vb != vb || m_state.vb_stride != stride) {
        m_state.vb = vb;
        m_state.vb_stride = stride;

        uint32 stride2 = stride;
        uint32 offset = 0;

        m_ctx->IASetVertexBuffers(0, 1, &vb, &stride2, &offset);
    }
}

void GSDeviceProxy::IASetIndexBuffer(const void *index, size_t count)
{
    ASSERT(m_index.count == 0);

    if (count > m_index.limit) {
        m_ib_old = m_ib;
        m_ib = NULL;

        m_index.start = 0;
        m_index.limit = std::max<int>(count * 3 / 2, 11000);
    }

    if (m_ib == NULL) {
        D3D11_BUFFER_DESC bd;

        memset(&bd, 0, sizeof(bd));

        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = m_index.limit * sizeof(uint32);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr;

        hr = m_dev->CreateBuffer(&bd, NULL, &m_ib);

        if (FAILED(hr))
            return;
    }

    D3D11_MAP type = D3D11_MAP_WRITE_NO_OVERWRITE;

    if (m_index.start + count > m_index.limit) {
        m_index.start = 0;

        type = D3D11_MAP_WRITE_DISCARD;
    }

    D3D11_MAPPED_SUBRESOURCE m;

    if (SUCCEEDED(m_ctx->Map(m_ib, 0, type, 0, &m))) {
        memcpy((uint8 *)m.pData + m_index.start * sizeof(uint32), index, count * sizeof(uint32));

        m_ctx->Unmap(m_ib, 0);
    }

    m_index.count = count;

    IASetIndexBuffer(m_ib);
}

void GSDeviceProxy::IASetIndexBuffer(ID3D11Buffer *ib)
{
    if (m_state.ib != ib) {
        m_state.ib = ib;

        m_ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
    }
}

void GSDeviceProxy::IASetInputLayout(ID3D11InputLayout *layout)
{
    if (m_state.layout != layout) {
        m_state.layout = layout;

        m_ctx->IASetInputLayout(layout);
    }
}

void GSDeviceProxy::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology)
{
    if (m_state.topology != topology) {
        m_state.topology = topology;

        m_ctx->IASetPrimitiveTopology(topology);
    }
}

void GSDeviceProxy::VSSetShader(ID3D11VertexShader *vs, ID3D11Buffer *vs_cb)
{
    if (m_state.vs != vs) {
        m_state.vs = vs;

        m_ctx->VSSetShader(vs, NULL, 0);
    }

    if (m_state.vs_cb != vs_cb) {
        m_state.vs_cb = vs_cb;

        m_ctx->VSSetConstantBuffers(0, 1, &vs_cb);
    }
}

void GSDeviceProxy::GSSetShader(ID3D11GeometryShader *gs, ID3D11Buffer *gs_cb)
{
    if (m_state.gs != gs) {
        m_state.gs = gs;

        m_ctx->GSSetShader(gs, NULL, 0);
    }

    if (m_state.gs_cb != gs_cb) {
        m_state.gs_cb = gs_cb;

        m_ctx->GSSetConstantBuffers(0, 1, &gs_cb);
    }
}

void GSDeviceProxy::PSSetShaderResources(GSTexture *sr0, GSTexture *sr1)
{
    PSSetShaderResource(0, sr0);
    PSSetShaderResource(1, sr1);

    for (size_t i = 2; i < countof(m_state.ps_srv); i++) {
        PSSetShaderResource(i, NULL);
    }
}

void GSDeviceProxy::PSSetShaderResource(int i, GSTexture *sr)
{
    ID3D11ShaderResourceView *srv = NULL;

    if (sr)
        srv = *(GSTexture11 *)sr;

    PSSetShaderResourceView(i, srv);
}

void GSDeviceProxy::PSSetShaderResourceView(int i, ID3D11ShaderResourceView *srv)
{
    ASSERT(i < countof(m_state.ps_srv));

    if (m_state.ps_srv[i] != srv) {
        m_state.ps_srv[i] = srv;

        m_srv_changed = true;
    }
}

void GSDeviceProxy::PSSetSamplerState(ID3D11SamplerState *ss0, ID3D11SamplerState *ss1, ID3D11SamplerState *ss2)
{
    if (m_state.ps_ss[0] != ss0 || m_state.ps_ss[1] != ss1 || m_state.ps_ss[2] != ss2) {
        m_state.ps_ss[0] = ss0;
        m_state.ps_ss[1] = ss1;
        m_state.ps_ss[2] = ss2;

        m_ss_changed = true;
    }
}

void GSDeviceProxy::PSSetShader(ID3D11PixelShader *ps, ID3D11Buffer *ps_cb)
{
    if (m_state.ps != ps) {
        m_state.ps = ps;

        m_ctx->PSSetShader(ps, NULL, 0);
    }

    if (m_srv_changed) {
        m_ctx->PSSetShaderResources(0, countof(m_state.ps_srv), m_state.ps_srv);

        m_srv_changed = false;
    }

    if (m_ss_changed) {
        m_ctx->PSSetSamplers(0, countof(m_state.ps_ss), m_state.ps_ss);

        m_ss_changed = false;
    }

    if (m_state.ps_cb != ps_cb) {
        m_state.ps_cb = ps_cb;

        m_ctx->PSSetConstantBuffers(0, 1, &ps_cb);
    }
}

void GSDeviceProxy::OMSetDepthStencilState(ID3D11DepthStencilState *dss, uint8 sref)
{
    if (m_state.dss != dss || m_state.sref != sref) {
        m_state.dss = dss;
        m_state.sref = sref;

        m_ctx->OMSetDepthStencilState(dss, sref);
    }
}

void GSDeviceProxy::OMSetBlendState(ID3D11BlendState *bs, float bf)
{
    if (m_state.bs != bs || m_state.bf != bf) {
        m_state.bs = bs;
        m_state.bf = bf;

        float BlendFactor[] = {bf, bf, bf, 0};

        m_ctx->OMSetBlendState(bs, BlendFactor, 0xffffffff);
    }
}

void GSDeviceProxy::OMSetRenderTargets(GSTexture *rt, GSTexture *ds, const GSVector4i *scissor)
{
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;

    if (!rt && !ds)
        throw GSDXRecoverableError();

    if (rt)
        rtv = *(GSTexture11 *)rt;
    if (ds)
        dsv = *(GSTexture11 *)ds;

    if (m_state.rtv != rtv || m_state.dsv != dsv) {
        m_state.rtv = rtv;
        m_state.dsv = dsv;

        m_ctx->OMSetRenderTargets(1, &rtv, dsv);
    }

    GSVector2i size = rt ? rt->GetSize() : ds->GetSize();
    if (m_state.viewport != size) {
        m_state.viewport = size;

        D3D11_VIEWPORT vp;
        memset(&vp, 0, sizeof(vp));

        vp.TopLeftX = m_hack_topleft_offset;
        vp.TopLeftY = m_hack_topleft_offset;
        vp.Width = (float)size.x;
        vp.Height = (float)size.y;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        m_ctx->RSSetViewports(1, &vp);
    }

    GSVector4i r = scissor ? *scissor : GSVector4i(size).zwxy();

    if (!m_state.scissor.eq(r)) {
        m_state.scissor = r;

        m_ctx->RSSetScissorRects(1, r);
    }
}

void GSDeviceProxy::OMSetRenderTargets(const GSVector2i &rtsize, int count, ID3D11UnorderedAccessView **uav, uint32 *counters, const GSVector4i *scissor)
{
    m_ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, NULL, NULL, 0, count, uav, counters);

    m_state.rtv = NULL;
    m_state.dsv = NULL;

    if (m_state.viewport != rtsize) {
        m_state.viewport = rtsize;

        D3D11_VIEWPORT vp;

        memset(&vp, 0, sizeof(vp));

        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        vp.Width = (float)rtsize.x;
        vp.Height = (float)rtsize.y;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        m_ctx->RSSetViewports(1, &vp);
    }

    GSVector4i r = scissor ? *scissor : GSVector4i(rtsize).zwxy();

    if (!m_state.scissor.eq(r)) {
        m_state.scissor = r;

        m_ctx->RSSetScissorRects(1, r);
    }
}

void GSDeviceProxy::CompileShader(const char *source, size_t size, const char *fn, ID3DInclude *include, const char *entry, D3D_SHADER_MACRO *macro, ID3D11VertexShader **vs, D3D11_INPUT_ELEMENT_DESC *layout, int count, ID3D11InputLayout **il)
{
    HRESULT hr;

    std::vector<D3D_SHADER_MACRO> m;

    PrepareShaderMacro(m, macro);

    CComPtr<ID3DBlob> shader, error;

    hr = s_pD3DCompile(source, size, fn, &m[0], s_old_d3d_compiler_dll ? nullptr : include, entry, m_shader.vs.c_str(), 0, 0, &shader, &error);

    if (error) {
        printf("%s\n", (const char *)error->GetBufferPointer());
    }

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }

    hr = m_dev->CreateVertexShader((void *)shader->GetBufferPointer(), shader->GetBufferSize(), NULL, vs);

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }

    hr = m_dev->CreateInputLayout(layout, count, shader->GetBufferPointer(), shader->GetBufferSize(), il);

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }
}

void GSDeviceProxy::CompileShader(const char *source, size_t size, const char *fn, ID3DInclude *include, const char *entry, D3D_SHADER_MACRO *macro, ID3D11GeometryShader **gs)
{
    HRESULT hr;

    std::vector<D3D_SHADER_MACRO> m;

    PrepareShaderMacro(m, macro);

    CComPtr<ID3DBlob> shader, error;

    hr = s_pD3DCompile(source, size, fn, &m[0], s_old_d3d_compiler_dll ? nullptr : include, entry, m_shader.gs.c_str(), 0, 0, &shader, &error);

    if (error) {
        printf("%s\n", (const char *)error->GetBufferPointer());
    }

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }

    hr = m_dev->CreateGeometryShader((void *)shader->GetBufferPointer(), shader->GetBufferSize(), NULL, gs);

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }
}

void GSDeviceProxy::CompileShader(const char *source, size_t size, const char *fn, ID3DInclude *include, const char *entry, D3D_SHADER_MACRO *macro, ID3D11GeometryShader **gs, D3D11_SO_DECLARATION_ENTRY *layout, int count)
{
    HRESULT hr;

    std::vector<D3D_SHADER_MACRO> m;

    PrepareShaderMacro(m, macro);

    CComPtr<ID3DBlob> shader, error;

    hr = s_pD3DCompile(source, size, fn, &m[0], s_old_d3d_compiler_dll ? nullptr : include, entry, m_shader.gs.c_str(), 0, 0, &shader, &error);

    if (error) {
        printf("%s\n", (const char *)error->GetBufferPointer());
    }

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }

    hr = m_dev->CreateGeometryShaderWithStreamOutput((void *)shader->GetBufferPointer(), shader->GetBufferSize(), layout, count, NULL, 0, D3D11_SO_NO_RASTERIZED_STREAM, NULL, gs);

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }
}

void GSDeviceProxy::CompileShader(const char *source, size_t size, const char *fn, ID3DInclude *include, const char *entry, D3D_SHADER_MACRO *macro, ID3D11PixelShader **ps)
{
    HRESULT hr;

    std::vector<D3D_SHADER_MACRO> m;

    PrepareShaderMacro(m, macro);

    CComPtr<ID3DBlob> shader, error;

    hr = s_pD3DCompile(source, size, fn, &m[0], s_old_d3d_compiler_dll ? nullptr : include, entry, m_shader.ps.c_str(), 0, 0, &shader, &error);

    if (error) {
        printf("%s\n", (const char *)error->GetBufferPointer());
    }

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }

    hr = m_dev->CreatePixelShader((void *)shader->GetBufferPointer(), shader->GetBufferSize(), NULL, ps);

    if (FAILED(hr)) {
        throw GSDXRecoverableError();
    }
}
