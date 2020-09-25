#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "SimpleCamera.h"
#include "BezierMaths.h"

#include <vector>

using namespace DirectX;

using Microsoft::WRL::ComPtr;

class BezierMS : public DXSample
{
public:
    BezierMS(UINT width, UINT height, std::wstring name);

    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnDestroy();
    virtual void OnKeyDown(UINT8 key);
    virtual void OnKeyUp(UINT8 key);

private:
    static const UINT FrameCount = 2;

    _declspec(align(256u)) struct SceneConstantBuffer
    {
        XMFLOAT4X4 World;
        XMFLOAT4X4 WorldView;
        XMFLOAT4X4 WorldViewProj;
        unsigned int NumPatches;
        unsigned int NumTesselationRowsPerPatch;
        unsigned int NumTrianglesPerPatch;
    };

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_depthStencil;
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12PipelineState> m_pipelineStateWireFrame;
    ComPtr<ID3D12Resource> m_constantBuffer;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;

    ComPtr<ID3D12GraphicsCommandList6> m_commandList;
    SceneConstantBuffer m_constantBufferData;
    UINT8* m_cbvDataBegin;

    StepTimer m_timer;
    SimpleCamera m_camera;
    
    // Synchronization objects.
    UINT m_frameIndex;
    UINT m_frameCounter;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FrameCount];
    
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    std::vector<TriangularBezierPatch::ControlPoint> m_vertices;
    
    bool m_wireFrameToggle = false;
    std::pair<unsigned int, unsigned int> m_tessellationFactors = { 2, 32 };
    float m_adaptiveTessellationRange = 8.f;
    BezierShape m_shape;

    void LoadPipeline();
    void LoadAssets();
    void LoadGeometry();
    void PopulateCommandList();
    void MoveToNextFrame();
    void WaitForGpu();

private:
    static const wchar_t* ampShaderFilename;
    static const wchar_t* meshShaderFilename;
    static const wchar_t* pixelShaderFilename;
};