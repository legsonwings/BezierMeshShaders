#include "stdafx.h"
#include "BezierMS.h"
#include "BezierFileIO.h"

#include <cstddef>
#include <cmath>
#include <iterator>

const wchar_t* BezierMS::ampShaderFilename = L"BezierAS.cso";
const wchar_t* BezierMS::meshShaderFilename = L"BezierMS.cso";
const wchar_t* BezierMS::pixelShaderFilename = L"BezierPS.cso";

using namespace std;
using namespace  DirectX::SimpleMath;

BezierMS::BezierMS(UINT width, UINT height, std::wstring name)
    : DXSample(width, height, name)
    , m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
    , m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height))
    , m_rtvDescriptorSize(0)
    , m_dsvDescriptorSize(0)
    , m_constantBufferData{}
    , m_cbvDataBegin(nullptr)
    , m_frameIndex(0)
    , m_frameCounter(0)
    , m_fenceEvent{}
    , m_fenceValues{}
{ }

void BezierMS::OnInit()
{
    m_camera.Init({ 0, 0, 10.f });

    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void BezierMS::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_5 };
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
        || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5))
    {
        OutputDebugStringA("ERROR: Shader Model 6.5 is not supported\n");
        throw std::exception("Shader Model 6.5 is not supported");
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 features = {};
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features)))
        || (features.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED))
    {
        OutputDebugStringA("ERROR: Mesh Shaders aren't supported!\n");
        throw std::exception("Mesh Shaders aren't supported!");
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }

    // Create the depth stencil view.
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS(&m_depthStencil)
        ));

        NAME_D3D12_OBJECT(m_depthStencil);

        m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Create the constant buffer.
    {
        const UINT64 constantBufferSize = sizeof(SceneConstantBuffer) * FrameCount;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = constantBufferSize;

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_cbvDataBegin)));
    }
}

// Load the sample assets.
void BezierMS::LoadAssets()
{
    // Create the pipeline state, which includes compiling and loading shaders.
    {
        struct 
        { 
            std::byte* data; 
            uint32_t size; 
        } ampShader, meshShader, pixelShader;

        
        ReadDataFromFile(GetAssetFullPath(ampShaderFilename).c_str(), &ampShader.data, &ampShader.size);
        ReadDataFromFile(GetAssetFullPath(meshShaderFilename).c_str(), &meshShader.data, &meshShader.size);
        ReadDataFromFile(GetAssetFullPath(pixelShaderFilename).c_str(), &pixelShader.data, &pixelShader.size);
        
        // Pull root signature from the precompiled mesh shader.
        ThrowIfFailed(m_device->CreateRootSignature(0, meshShader.data, meshShader.size, IID_PPV_ARGS(&m_rootSignature)));

        D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature        = m_rootSignature.Get();
        psoDesc.AS                    = { ampShader.data, ampShader.size };
        psoDesc.MS                    = { meshShader.data, meshShader.size };
        psoDesc.PS                    = { pixelShader.data, pixelShader.size };
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = m_renderTargets[0]->GetDesc().Format;
        psoDesc.DSVFormat             = m_depthStencil->GetDesc().Format;
        psoDesc.RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);    // CW front; cull back
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);         // Opaque
        psoDesc.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // Less-equal depth test w/ writes; no stencil
        psoDesc.SampleMask            = UINT_MAX;
        psoDesc.SampleDesc            = DefaultSampleDesc();

        auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
        streamDesc.pPipelineStateSubobjectStream = &psoStream;
        streamDesc.SizeInBytes                   = sizeof(psoStream);

        ThrowIfFailed(m_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_pipelineState)));

        // Setup the Wireframe pso
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        auto psoStreamWire = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

        streamDesc.pPipelineStateSubobjectStream = &psoStreamWire;
        streamDesc.SizeInBytes = sizeof(psoStreamWire);

        ThrowIfFailed(m_device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_pipelineStateWireFrame)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state
    // We have nothing to record
    // The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    // Record initial commands for resource uploads
    LoadGeometry();
}

void BezierMS::LoadGeometry()
{
    auto& topRightFrontOctant = m_shape.Patches[0];

    topRightFrontOctant = BezierFileIO::ReadFromFile<decltype(m_shape)::GetDegree()>(GetAssetFullPath(L"..\\..\\scene\\TopRightFront.bez"));

    size_t const vbSizeInBytes = _countof(topRightFrontOctant.ControlPoints) * sizeof(BezierMaths::ControlPoint);
    m_vertices.reserve(vbSizeInBytes);
    std::copy(std::begin(m_shape.Patches[0].ControlPoints), std::end(m_shape.Patches[0].ControlPoints), back_inserter(m_vertices));

    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSizeInBytes);
    
    // Are the resource states indicative of how resources will be used by GPU?
    // Create vertex buffer on the default heap
    auto defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeapDesc, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_vertexBuffer.ReleaseAndGetAddressOf())));

   m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
   m_vertexBufferView.SizeInBytes = static_cast<UINT>(vbSizeInBytes);
   m_vertexBufferView.StrideInBytes = sizeof(BezierMaths::ControlPoint);

   // Create vertex resource on the upload heap
   ComPtr<ID3D12Resource> vertexBufferUpload;
   auto uploadHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
   ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapDesc, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(vertexBufferUpload.GetAddressOf())));
   
   {
       uint8_t* vbStartUpload = nullptr;

       // We do not intend to read from this resource on the CPU.
       vertexBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&vbStartUpload));

       // Copy vertex data to upload heap
       memcpy(vbStartUpload, m_vertices.data(), vbSizeInBytes);
       
       vertexBufferUpload->Unmap(0, nullptr);
   }
   
   ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr));

   // Copy vertex data from upload heap to default heap
   m_commandList->CopyResource(m_vertexBuffer.Get(), vertexBufferUpload.Get());
   m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

   ThrowIfFailed(m_commandList->Close());

   ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
   m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

   // Create synchronization objects and wait until assets have been uploaded to the GPU.
   {
       ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
       m_fenceValues[m_frameIndex]++;

       // Create an event handle to use for frame synchronization.
       m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
       if (m_fenceEvent == nullptr)
       {
           ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
       }

       // Wait for the command list to execute; we are reusing the same command 
       // list in our main loop but for now, we just want to wait for setup to 
       // complete before continuing.
       WaitForGpu();
   }
}

// Update frame-based values.
void BezierMS::OnUpdate()
{
    m_timer.Tick(NULL);

    if (m_frameCounter++ % 30 == 0)
    {
        // Update window text with FPS value.
        wchar_t fps[64];
        swprintf_s(fps, L"%ufps", m_timer.GetFramesPerSecond());
        SetCustomWindowText(fps);
    }

    m_camera.Update(static_cast<float>(m_timer.GetElapsedSeconds()));

    XMMATRIX world = XMMATRIX(g_XMIdentityR0, g_XMIdentityR1, g_XMIdentityR2, g_XMIdentityR3);
    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX proj = m_camera.GetProjectionMatrix(XM_PI / 3.0f, m_aspectRatio);

    XMVECTOR outTrans, outRotQuat, outScale;
    XMMatrixDecompose(&outScale, &outRotQuat, &outTrans, m_camera.GetViewMatrix());
    float normTessFactor = 1.f - min(1.f, Vector3::DistanceSquared(m_shape.GetCenter(), Vector3(outTrans)) / (m_adaptiveTessellationRange * m_adaptiveTessellationRange));
    unsigned int tessellationFactor = m_tessellationFactors.first + static_cast<unsigned int>(normTessFactor * (m_tessellationFactors.second - m_tessellationFactors.first));

    XMStoreFloat4x4(&m_constantBufferData.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&m_constantBufferData.WorldView, XMMatrixTranspose(world * view));
    XMStoreFloat4x4(&m_constantBufferData.WorldViewProj, XMMatrixTranspose(world * view * proj));
    m_constantBufferData.NumPatches = m_shape.GetNumPatchs();
    m_constantBufferData.NumTesselationRowsPerPatch = tessellationFactor;
    m_constantBufferData.NumTrianglesPerPatch = m_constantBufferData.NumTesselationRowsPerPatch * m_constantBufferData.NumTesselationRowsPerPatch;

    memcpy(m_cbvDataBegin + sizeof(SceneConstantBuffer) * m_frameIndex, &m_constantBufferData, sizeof(m_constantBufferData));
}

// Render the scene. 
void BezierMS::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    MoveToNextFrame();
}

void BezierMS::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_fenceEvent);
}

void BezierMS::OnKeyDown(UINT8 key)
{
    m_camera.OnKeyDown(key);
}

void BezierMS::OnKeyUp(UINT8 key)
{
    if (key == 'T')
    {
        m_wireFrameToggle = !m_wireFrameToggle;
    }
        
    m_camera.OnKeyUp(key);
}

void BezierMS::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_wireFrameToggle ? m_pipelineStateWireFrame.Get() : m_pipelineState.Get()));

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + sizeof(SceneConstantBuffer) * m_frameIndex);
    m_commandList->SetGraphicsRootShaderResourceView(1, m_vertexBuffer->GetGPUVirtualAddress());

    // Assert based on max verts our shaders are designed to process

    m_commandList->DispatchMesh(1, 1, 1);

    // Indicate that the back buffer will now be used to present.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void BezierMS::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void BezierMS::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}
