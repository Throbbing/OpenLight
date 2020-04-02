#include"CubeRenderer.h"
#include "Utility.h"
#include "TextureManager.h"
#include <chrono>
using namespace DirectX;
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif


int  gMouseX = 0;
int  gMouseY = 0;
int  gLastMouseX = 0;
int  gLastMouseY = 0;
bool gMouseDown = false;
bool gMouseMove = false;

CubeRenderer::CubeRenderer()
{
	mCamera = new RoamCamera(
		XMFLOAT3(0, 0, 1),
		XMFLOAT3(0, 1, 0),
		XMFLOAT3(0, 5, -40),
		0.1f,
		2000.f,
		AppConfig::ClientWidth,
		AppConfig::ClientHeight);
}

CubeRenderer::~CubeRenderer()
{
	if (mCamera)
		delete mCamera;
}



void CubeRenderer::Init(HWND hWnd)
{
	mTimer.reset();
	Renderer::Init(hWnd);

	// ���� InputLayout
	D3D12_INPUT_ELEMENT_DESC inputDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// ���ļ��м���ģ��
	mCubeMesh = ObjMeshLoader::loadObjMeshFromFile("F:\\OpenLight\\Cube.obj");
	UINT modelSize =
		mCubeMesh->submeshs[0].vertices.size() * sizeof(StandardVertex) +
		mCubeMesh->submeshs[0].indices.size() * sizeof(UINT);
	// ���� Upload Heap
	{
		D3D12_HEAP_DESC uploadDesc = {};
		uploadDesc.SizeInBytes = PAD((1<<20), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		uploadDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		uploadDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		uploadDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		uploadDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		uploadDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

		ThrowIfFailed(mDevice->CreateHeap(&uploadDesc, IID_PPV_ARGS(&mUploadHeap)));

	}

	// ���� VB IB CB
	{
		UINT verticesSizeInBytes = mCubeMesh->submeshs[0].vertices.size() * sizeof(StandardVertex);
		UINT indicesSizeInBytes = mCubeMesh->submeshs[0].indices.size() * sizeof(UINT);
		UINT offset = 0;
		ThrowIfFailed(mDevice->CreatePlacedResource(
			mUploadHeap.Get(),
			offset,
			&CD3DX12_RESOURCE_DESC::Buffer(verticesSizeInBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mCubeMeshVB)));

		mCubeMeshVBView.BufferLocation = mCubeMeshVB->GetGPUVirtualAddress();
		mCubeMeshVBView.StrideInBytes = sizeof(StandardVertex);
		mCubeMeshVBView.SizeInBytes = verticesSizeInBytes;

		void* p = nullptr;
		CD3DX12_RANGE readRange(0, 0);
		ThrowIfFailed(mCubeMeshVB->Map(0, &readRange, &p));
		std::memcpy(p, &mCubeMesh->submeshs[0].vertices[0], verticesSizeInBytes);
		mCubeMeshVB->Unmap(0,nullptr);

		offset = PAD(offset + verticesSizeInBytes, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
		ThrowIfFailed(mDevice->CreatePlacedResource(
			mUploadHeap.Get(),
			offset,
			&CD3DX12_RESOURCE_DESC::Buffer(indicesSizeInBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mCubeMeshIB)));

		mCubeMeshIBView.BufferLocation = mCubeMeshIB->GetGPUVirtualAddress();
		mCubeMeshIBView.SizeInBytes = indicesSizeInBytes;
		mCubeMeshIBView.Format = DXGI_FORMAT_R32_UINT;

		ThrowIfFailed(mCubeMeshIB->Map(0, &readRange, &p));
		std::memcpy(p, &mCubeMesh->submeshs[0].indices[0], indicesSizeInBytes);
		mCubeMeshIB->Unmap(0, nullptr);

		offset = PAD(offset + indicesSizeInBytes, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		ThrowIfFailed(mDevice->CreatePlacedResource(
			mUploadHeap.Get(),
			offset,
			&CD3DX12_RESOURCE_DESC::Buffer(PAD(sizeof(CBTrans),256)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mCBTrans)));

		ThrowIfFailed(mCBTrans->Map(0, nullptr, reinterpret_cast<void**>(&mCBTransGPUPtr)));
	}

	


	// ���� Shader
	WRL::ComPtr<ID3DBlob> vs = nullptr;
	WRL::ComPtr<ID3DBlob> ps = nullptr;

#if defined(_DEBUG)		
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif 

	ThrowIfFailed(D3DCompileFromFile(L"F:\\OpenLight\\Shader\\TextureCubeVS.hlsl",
		nullptr, D3D_HLSL_DEFUALT_INCLUDE, "VSMain", "vs_5_0", compileFlags, 0, &vs, nullptr));


	ThrowIfFailed(D3DCompileFromFile(L"F:\\OpenLight\\Shader\\TextureCubePS.hlsl",
		nullptr, D3D_HLSL_DEFUALT_INCLUDE, "PSMain", "ps_5_0", compileFlags, 0, &ps, nullptr));


	// ������ǩ������
	CD3DX12_ROOT_PARAMETER1 rootParameters[3];
	CD3DX12_DESCRIPTOR_RANGE1 descRange[3];
	descRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	descRange[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0);
	descRange[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	rootParameters[0].InitAsDescriptorTable(1, &descRange[0], D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[1].InitAsDescriptorTable(1, &descRange[1], D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[2].InitAsDescriptorTable(1, &descRange[2], D3D12_SHADER_VISIBILITY_VERTEX);
	// ��ǩ������
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters,
		0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	// ������ǩ��
	WRL::ComPtr<ID3DBlob> pSignatureBlob;
	WRL::ComPtr<ID3DBlob> pErrorBlob;
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_1,
		&pSignatureBlob,
		&pErrorBlob));
	ThrowIfFailed(mDevice->CreateRootSignature(0,
		pSignatureBlob->GetBufferPointer(),
		pSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));

	// ���� PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS.pShaderBytecode = vs->GetBufferPointer();
	psoDesc.VS.BytecodeLength = vs->GetBufferSize();
	psoDesc.PS.pShaderBytecode = ps->GetBufferPointer();
	psoDesc.PS.BytecodeLength = ps->GetBufferSize();
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSORGBA32)));

	mViewport = { 0.0f, 0.0f
		, static_cast<float>(AppConfig::ClientWidth), static_cast<float>(AppConfig::ClientHeight), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
	mScissorRect = { 0, 0
		, static_cast<LONG>(AppConfig::ClientWidth), static_cast<LONG>(AppConfig::ClientHeight) };

	// ���� SRV CBV Heap
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 2;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSRVCBVHeap)));

	// ���� Sampler Heap
	D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
	samplerHeapDesc.NumDescriptors = mSamplerCount;
	samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(mDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&mSamplerHeap)));



	// ���� Texture Resource
	auto commandAllocator = mCommandAllocator[0];
	ThrowIfFailed(commandAllocator->Reset());
	ThrowIfFailed(mCommandList->Reset(commandAllocator.Get(), nullptr));
	mTexture = TextureMgr::LoadTexture2DFromFile("F:\\OpenLight\\timg.jpg",
		mDevice,
		mCommandList,
		mCommandQueue);
	//	ThrowIfFailed(mCommandList->Close());

	// ���� Texture SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		mDevice->CreateShaderResourceView(mTexture.Get(), &srvDesc, mSRVCBVHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// ���� CBV
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = mCBTrans->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = PAD(sizeof(CBTrans),256);
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSRVCBVHeap->GetCPUDescriptorHandleForHeapStart(),
			1,
			mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

		mDevice->CreateConstantBufferView(&cbvDesc, handle);
	}


	// ���� Sampler
	auto samplerDescriptorOffset = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	CD3DX12_CPU_DESCRIPTOR_HANDLE samplerHandle(mSamplerHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SAMPLER_DESC samplerDesc = {  };
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.MinLOD = 0.f;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// Sampler 1
	samplerDesc.BorderColor[0] = 1.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 1.0f;
	samplerDesc.BorderColor[3] = 1.0f;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	mDevice->CreateSampler(&samplerDesc, samplerHandle);

	// Sampler 2
	samplerHandle.Offset(samplerDescriptorOffset);
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	mDevice->CreateSampler(&samplerDesc, samplerHandle);

	// Sampler 3
	samplerHandle.Offset(samplerDescriptorOffset);
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	mDevice->CreateSampler(&samplerDesc, samplerHandle);


	InitPostprocess();

}

void CubeRenderer::Render()
{
	Update();

	auto commandAllocator = mCommandAllocator[mCurrentBackBufferIndex];
	auto backBuffer = mBackBuffer[mCurrentBackBufferIndex];
	
	commandAllocator->Reset();
	mCommandList->Reset(commandAllocator.Get(), nullptr);

	// �л� Scene Color ״̬	
	if(mGammaCorrect)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = mSceneColorBuffer[mCurrentBackBufferIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		mCommandList->ResourceBarrier(1, &barrier);
	}



	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	if (mGammaCorrect)
		mCommandList->SetPipelineState(mPSORGBA32.Get());
	else
		mCommandList->SetPipelineState(mPSO.Get());
	ID3D12DescriptorHeap* ppHeaps[] = { mSRVCBVHeap.Get(),mSamplerHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// ����SRV
	mCommandList->SetGraphicsRootDescriptorTable(0, mSRVCBVHeap->GetGPUDescriptorHandleForHeapStart());
	
	// ���� Sampler
	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerHandle(mSamplerHeap->GetGPUDescriptorHandleForHeapStart(),
		mSamplerIndex,
		mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));
	mCommandList->SetGraphicsRootDescriptorTable(1, samplerHandle);
	
	// ���� CBV
	mCommandList->SetGraphicsRootDescriptorTable(2,
		CD3DX12_GPU_DESCRIPTOR_HANDLE(mSRVCBVHeap->GetGPUDescriptorHandleForHeapStart(),
			1,
			mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)));



	mCommandList->RSSetViewports(1, &mViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// �л� ��RTV ��״̬ 
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = backBuffer.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &barrier);


	
	FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
	D3D12_CPU_DESCRIPTOR_HANDLE rtv;
	if (mGammaCorrect)
	{
		rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSceneColorRTVHeap->GetCPUDescriptorHandleForHeapStart(),
			mCurrentBackBufferIndex,
			mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
	}
	else
	{
		rtv = (mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		rtv.ptr += mCurrentBackBufferIndex * mRTVDescriptorSize;
	}
	//������ȾĿ��
	mCommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->IASetVertexBuffers(0, 1, &mCubeMeshVBView);
	mCommandList->IASetIndexBuffer(&mCubeMeshIBView);
	//Draw Call������
	mCommandList->DrawIndexedInstanced(mCubeMesh->submeshs[0].indices.size(), 1, 0, 0, 0);

	// ����к���
	if (mGammaCorrect)
	{
		// �л� Scene Color
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = mSceneColorBuffer[mCurrentBackBufferIndex].Get();
		barrier.Transition.StateBefore =  D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter =D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		mCommandList->ResourceBarrier(1, &barrier);

		mCommandList->SetGraphicsRootSignature(mPostprocessSignature.Get());
		mCommandList->SetPipelineState(mPostprocessPSO.Get());
		ID3D12DescriptorHeap* ppHeaps[] = { mSceneColorSRVHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		mCommandList->SetGraphicsRootDescriptorTable(0, 
			CD3DX12_GPU_DESCRIPTOR_HANDLE(mSceneColorSRVHeap->GetGPUDescriptorHandleForHeapStart(),
				mCurrentBackBufferIndex,
				mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)));
		rtv = (mRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		rtv.ptr += mCurrentBackBufferIndex * mRTVDescriptorSize;
		mCommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

		mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->IASetVertexBuffers(0, 1, &mQuadVBView);
		mCommandList->IASetIndexBuffer(&mQuadIBView);
		//Draw Call������
		mCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	}


	// Present
	barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = backBuffer.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &barrier);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* const commandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	UINT syncInterval = AppConfig::VSync ? 1 : 0;
	UINT presentFlags = AppConfig::TearingSupported && !AppConfig::VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	ThrowIfFailed(mSwapChain->Present(syncInterval, presentFlags));

	mFrameFenceValues[mCurrentBackBufferIndex] = Signal(mCommandQueue, mFence, mFenceValue);

	mCurrentBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
	WaitForFenceValue(mFence.Get(), mFrameFenceValues[mCurrentBackBufferIndex], mFenceEvent);
}

void CubeRenderer::Update()
{
	FPS();
	if (GetAsyncKeyState('K') & 0x8000)
	{
		mSamplerIndex = (++mSamplerIndex) % mSamplerCount;
		Sleep(100);
	}
	mTimer.tick();

	if (GetAsyncKeyState('P') & 0x8000)
	{
		mGammaCorrect = !mGammaCorrect;
		Sleep(100);
	}

	// �������
	static float speed = 20.f;
	float dt = mTimer.deltaTime();
	if (GetAsyncKeyState('W') & 0x8000)
		mCamera->walk(speed*dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera->walk(-speed * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera->strafe(-speed * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera->strafe(speed*dt);

	if (GetAsyncKeyState('Q') & 0x8000)
		mCamera->yaw(+5.f * dt);
	if (GetAsyncKeyState('E') & 0x8000)
		mCamera->yaw(-5.f * dt);


	mCamera->updateMatrix();

	XMMATRIX vp = mCamera->getViewProj();
	XMMATRIX world = XMMatrixIdentity();

	XMStoreFloat4x4(&mCBTransGPUPtr->wvp, XMMatrixTranspose(vp));
	XMStoreFloat4x4(&mCBTransGPUPtr->world, XMMatrixTranspose(world));
	XMStoreFloat4x4(&mCBTransGPUPtr->invTranspose, XMMatrixTranspose(world));
}

struct QuadVertex
{
	XMFLOAT3 positionL;
	XMFLOAT2 texcoord;
};

void CubeRenderer::InitPostprocess()
{
	// ���� InputLayout
	D3D12_INPUT_ELEMENT_DESC inputDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

#if defined(_DEBUG)		
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif 
	WRL::ComPtr<ID3DBlob> vs;
	WRL::ComPtr<ID3DBlob> ps;

	ThrowIfFailed(D3DCompileFromFile(L"F:\\OpenLight\\Shader\\PostprocessVS.hlsl",
		nullptr, D3D_HLSL_DEFUALT_INCLUDE, "PostprocessVSMain", "vs_5_0", compileFlags, 0, &vs, nullptr));
	ThrowIfFailed(D3DCompileFromFile(L"F:\\OpenLight\\Shader\\PostprocessPS.hlsl",
		nullptr, D3D_HLSL_DEFUALT_INCLUDE, "GammaCorrectPSMain", "ps_5_0", compileFlags, 0, &ps, nullptr));

	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	CD3DX12_DESCRIPTOR_RANGE1 descRange[1];
	descRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	rootParameters[0].InitAsDescriptorTable(1, &descRange[0], D3D12_SHADER_VISIBILITY_PIXEL);
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.MinLOD = 0.f;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.Init_1_1(1, rootParameters,
		1, &samplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


	// ������ǩ��
	WRL::ComPtr<ID3DBlob> pSignatureBlob;
	WRL::ComPtr<ID3DBlob> pErrorBlob;
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_1,
		&pSignatureBlob,
		&pErrorBlob));
	ThrowIfFailed(mDevice->CreateRootSignature(0,
		pSignatureBlob->GetBufferPointer(),
		pSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&mPostprocessSignature)));

	// ���� PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputDesc, _countof(inputDesc) };
	psoDesc.pRootSignature = mPostprocessSignature.Get();
	psoDesc.VS.pShaderBytecode = vs->GetBufferPointer();
	psoDesc.VS.BytecodeLength = vs->GetBufferSize();
	psoDesc.PS.pShaderBytecode = ps->GetBufferPointer();
	psoDesc.PS.BytecodeLength = ps->GetBufferSize();
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
	psoDesc.BlendState.IndependentBlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPostprocessPSO)));

	
	std::vector<QuadVertex> vertices(4);
	std::vector<UINT>		indices;

	vertices[0].positionL = XMFLOAT3(-1.0f, -1.0f, 0.0f);
	vertices[1].positionL = XMFLOAT3(-1.0f, +1.0f, 0.0f);
	vertices[2].positionL = XMFLOAT3(+1.0f, +1.0f, 0.0f);
	vertices[3].positionL = XMFLOAT3(+1.0f, -1.0f, 0.0f);
	// Store far plane frustum corner indices in normal.x slot.

	vertices[0].texcoord = XMFLOAT2(0.0f, 1.0f);
	vertices[1].texcoord = XMFLOAT2(0.0f, 0.0f);
	vertices[2].texcoord = XMFLOAT2(1.0f, 0.0f);
	vertices[3].texcoord = XMFLOAT2(1.0f, 1.0f);
	indices = { 0,1,2,0,2,3 };

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(QuadVertex) * vertices.size()),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mQuadVB)));

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * indices.size()),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mQuadIB)));

	{
		void *p;
		ThrowIfFailed(mQuadVB->Map(0, nullptr, &p));
		std::memcpy(p, &vertices[0], sizeof(QuadVertex) * vertices.size());
		mQuadVB->Unmap(0,nullptr);
	
		ThrowIfFailed(mQuadIB->Map(0, nullptr, &p));
		std::memcpy(p, &indices[0], sizeof(UINT) * indices.size());
		mQuadIB->Unmap(0, nullptr);
	}
	mQuadVBView.BufferLocation = mQuadVB->GetGPUVirtualAddress();
	mQuadVBView.StrideInBytes = sizeof(QuadVertex);
	mQuadVBView.SizeInBytes = sizeof(QuadVertex) * vertices.size();

	mQuadIBView.BufferLocation = mQuadIB->GetGPUVirtualAddress();
	mQuadIBView.Format = DXGI_FORMAT_R32_UINT;
	mQuadIBView.SizeInBytes = sizeof(UINT) * indices.size();

	FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
	D3D12_CLEAR_VALUE d3dClearValue;
	d3dClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	std::memcpy(&d3dClearValue.Color, clearColor, sizeof(FLOAT) * 4);
	for (int i = 0; i < AppConfig::NumFrames; ++i)
	{
		ThrowIfFailed(mDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32G32B32A32_FLOAT, 
				AppConfig::ClientWidth, 
				AppConfig::ClientHeight,
				1,1,1,0,
				D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			&d3dClearValue,
			IID_PPV_ARGS(&mSceneColorBuffer[i])));
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = AppConfig::NumFrames;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heapDesc.NodeMask = 0;
		ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSceneColorRTVHeap)));
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSceneColorSRVHeap)));


		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {  };
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		rtvDesc.Texture2D.PlaneSlice = 0;
		for (int i = 0; i < AppConfig::NumFrames; ++i)
		{
			auto srvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSceneColorSRVHeap->GetCPUDescriptorHandleForHeapStart(), i,
				mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
			mDevice->CreateShaderResourceView(mSceneColorBuffer[i].Get(), &srvDesc, srvHandle);
			
			auto rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSceneColorRTVHeap->GetCPUDescriptorHandleForHeapStart(), i,
				mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
			mDevice->CreateRenderTargetView(mSceneColorBuffer[i].Get(), &rtvDesc, rtvHandle);
		}
	}




	

}

void CubeRenderer::FPS()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.

	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;

	// Compute averages over one second period.
	if ((mTimer.totalTime() - timeElapsed) >= 1.0f)
	{
		float fps = (float)frameCnt; // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

		wstring fpsStr = to_wstring(fps);
		wstring mspfStr = to_wstring(mspf);

		wstring windowText = 
			L"    fps: " + fpsStr +
			L"   mspf: " + mspfStr;

		SetWindowTextW(mHWND, windowText.c_str());

		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}