//***************************************************************************************
// i4CastleApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;
 
    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class i4CastleApp : public D3DApp
{
public:
    i4CastleApp(HINSTANCE hInstance);
    i4CastleApp(const i4CastleApp& rhs) = delete;
    i4CastleApp& operator=(const i4CastleApp& rhs) = delete;
    ~i4CastleApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	//void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

	Camera mCamera;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        i4CastleApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

i4CastleApp::i4CastleApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

i4CastleApp::~i4CastleApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool i4CastleApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);
 
	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void i4CastleApp::OnResize()
{
    D3DApp::OnResize();

	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void i4CastleApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	//UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
}

void i4CastleApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
	// set as a root descriptor.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
	mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void i4CastleApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void i4CastleApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void i4CastleApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

float currentAngle = 0.0f;
 
void i4CastleApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();
	//const float maxRotation = 0.05f;
	const float XMConvertToRadians(90.0f);
	const float speed = 1.5f;

	if(GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f*dt);

	if(GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f*dt);

	if(GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f*dt);

	if(GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f*dt);

	if (GetAsyncKeyState('E') & 0x8000) {
		currentAngle = speed*dt;
	
		//if (currentAngle < maxRotation)
		//{
			mCamera.Roll(currentAngle);
		//}
	}
		
	if (GetAsyncKeyState('Q') & 0x8000) {
		currentAngle = speed*dt;
		//if (currentAngle < maxRotation)
		//{
			mCamera.Roll(-currentAngle);
		//}
	}	

	mCamera.UpdateViewMatrix();
}


//void i4CastleApp::UpdateCamera(const GameTimer& gt)
//{
//	// Convert Spherical to Cartesian coordinates.
//	mEyePos.x = mRadius * sinf(mPhi)*cosf(mTheta);
//	mEyePos.z = mRadius * sinf(mPhi)*sinf(mTheta);
//	mEyePos.y = mRadius * cosf(mPhi);
//
//	// Build the view matrix.
//	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
//	XMVECTOR target = XMVectorZero();
//	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
//
//	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
//	XMStoreFloat4x4(&mView, view);
//}
 
void i4CastleApp::AnimateMaterials(const GameTimer& gt)
{
	
}

void i4CastleApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void i4CastleApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void i4CastleApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.2f, 0.2f, 0.2f };
	//mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 1.0f, 1.0f, 1.0f };
	mMainPassCB.Lights[1].Position = { 0.0f, 3.0f, -7.8f };
	mMainPassCB.Lights[2].Strength = { 1.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[2].Position = { 4.0f, 6.0f, 0.0f };
	mMainPassCB.Lights[3].Strength = { 0.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[3].Position = { -4.0f, 6.0f, 0.0f };
	mMainPassCB.Lights[4].Strength = { 0.0f, 0.0f, 1.0f };
	mMainPassCB.Lights[4].Position = { 4.0f, 6.0f, 8.0f };
	mMainPassCB.Lights[5].Strength = { 1.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[5].Position = { -4.0f, 6.0f, 8.0f };
	mMainPassCB.Lights[6].Strength = { 1.0f, 1.0f, 1.0f };
	mMainPassCB.Lights[6].Position = { 0.0f, 10.0f, 6.0f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void i4CastleApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"../../Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"../../Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"../../Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	auto crateTex = std::make_unique<Texture>();
	crateTex->Name = "crateTex";
	crateTex->Filename = L"../../Textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), crateTex->Filename.c_str(),
		crateTex->Resource, crateTex->UploadHeap));

	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[tileTex->Name] = std::move(tileTex);
	mTextures[crateTex->Name] = std::move(crateTex);
}

void i4CastleApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);


	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void i4CastleApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 4;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto crateTex = mTextures["crateTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = crateTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = crateTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(crateTex.Get(), &srvDesc, hDescriptor);
}

void i4CastleApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void i4CastleApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.f, 1.f);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.5f, 1.5f, 1.5f, 3);
	GeometryGenerator::MeshData octahedron = geoGen.CreateOctahedron(0.5f);
	GeometryGenerator::MeshData triangularPrism = geoGen.CreateTriangularPrism(1.f, 1.f, 1.f, 3);
	GeometryGenerator::MeshData hexagon = geoGen.CreateHexagon(1.5f, 1.5f, 3);
	GeometryGenerator::MeshData octagon = geoGen.CreateOctagon(1.5f, 1.5f, 3);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.f, 1.f, 0.f, 0.f, 1.f, 3);
	GeometryGenerator::MeshData container = geoGen.CreateHexagonContainer(1.f, 1.f, 3);
	GeometryGenerator::MeshData star = geoGen.CreateCandy(1.f, 1.f, 3);


	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT diamondVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT wedgeVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT octahedronVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT triPrismVertexOffset = octahedronVertexOffset + (UINT)octahedron.Vertices.size();
	UINT hexagonVertexOffset = triPrismVertexOffset + (UINT)triangularPrism.Vertices.size();
	UINT octagonVertexOffset = hexagonVertexOffset + (UINT)hexagon.Vertices.size();
	UINT coneVertexOffset = octagonVertexOffset + (UINT)octagon.Vertices.size();
	UINT pyramidVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT containerVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT starVertexOffset = containerVertexOffset + (UINT)container.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT diamondIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT wedgeIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT octahedronIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT triPrismIndexOffset = octahedronIndexOffset + (UINT)octahedron.Indices32.size();
	UINT hexagonIndexOffset = triPrismIndexOffset + (UINT)triangularPrism.Indices32.size();
	UINT octagonIndexOffset = hexagonIndexOffset + (UINT)hexagon.Indices32.size();
	UINT coneIndexOffset = octagonIndexOffset + (UINT)octagon.Indices32.size();
	UINT pyramidIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT containerIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT starIndexOffset = containerIndexOffset + (UINT)container.Indices32.size();


	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry octahedronSubmesh;
	octahedronSubmesh.IndexCount = (UINT)octahedron.Indices32.size();
	octahedronSubmesh.StartIndexLocation = octahedronIndexOffset;
	octahedronSubmesh.BaseVertexLocation = octahedronVertexOffset;

	SubmeshGeometry triPrismSubmesh;
	triPrismSubmesh.IndexCount = (UINT)triangularPrism.Indices32.size();
	triPrismSubmesh.StartIndexLocation = triPrismIndexOffset;
	triPrismSubmesh.BaseVertexLocation = triPrismVertexOffset;

	SubmeshGeometry hexagonSubmesh;
	hexagonSubmesh.IndexCount = (UINT)hexagon.Indices32.size();
	hexagonSubmesh.StartIndexLocation = hexagonIndexOffset;
	hexagonSubmesh.BaseVertexLocation = hexagonVertexOffset;

	SubmeshGeometry octagonSubmesh;
	octagonSubmesh.IndexCount = (UINT)octagon.Indices32.size();
	octagonSubmesh.StartIndexLocation = octagonIndexOffset;
	octagonSubmesh.BaseVertexLocation = octagonVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry containerSubmesh;
	containerSubmesh.IndexCount = (UINT)container.Indices32.size();
	containerSubmesh.StartIndexLocation = containerIndexOffset;
	containerSubmesh.BaseVertexLocation = containerVertexOffset;

	SubmeshGeometry starSubmesh;
	starSubmesh.IndexCount = (UINT)star.Indices32.size();
	starSubmesh.StartIndexLocation = starIndexOffset;
	starSubmesh.BaseVertexLocation = starVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		diamond.Vertices.size() +
		wedge.Vertices.size() +
		octahedron.Vertices.size() +
		triangularPrism.Vertices.size() +
		hexagon.Vertices.size() +
		octagon.Vertices.size() +
		cone.Vertices.size() +
		pyramid.Vertices.size() +
		container.Vertices.size() +
		star.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
		vertices[k].TexC = wedge.Vertices[i].TexC;
	}

	for (size_t i = 0; i < octahedron.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = octahedron.Vertices[i].Position;
		vertices[k].Normal = octahedron.Vertices[i].Normal;
		vertices[k].TexC = octahedron.Vertices[i].TexC;
	}

	for (size_t i = 0; i < triangularPrism.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = triangularPrism.Vertices[i].Position;
		vertices[k].Normal = triangularPrism.Vertices[i].Normal;
		vertices[k].TexC = triangularPrism.Vertices[i].TexC;
	}

	for (size_t i = 0; i < hexagon.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = hexagon.Vertices[i].Position;
		vertices[k].Normal = hexagon.Vertices[i].Normal;
		vertices[k].TexC = hexagon.Vertices[i].TexC;
	}

	for (size_t i = 0; i < octagon.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = octagon.Vertices[i].Position;
		vertices[k].Normal = octagon.Vertices[i].Normal;
		vertices[k].TexC = octagon.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < container.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = container.Vertices[i].Position;
		vertices[k].Normal = container.Vertices[i].Normal;
		vertices[k].TexC = container.Vertices[i].TexC;
	}

	for (size_t i = 0; i < star.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = star.Vertices[i].Position;
		vertices[k].Normal = star.Vertices[i].Normal;
		vertices[k].TexC = star.Vertices[i].TexC;
	}


	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(octahedron.GetIndices16()), std::end(octahedron.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangularPrism.GetIndices16()), std::end(triangularPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(hexagon.GetIndices16()), std::end(hexagon.GetIndices16()));
	indices.insert(indices.end(), std::begin(octagon.GetIndices16()), std::end(octagon.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(container.GetIndices16()), std::end(container.GetIndices16()));
	indices.insert(indices.end(), std::begin(star.GetIndices16()), std::end(star.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["octahedron"] = octahedronSubmesh;
	geo->DrawArgs["triangularPrism"] = triPrismSubmesh;
	geo->DrawArgs["hexagon"] = hexagonSubmesh;
	geo->DrawArgs["octagon"] = octagonSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["container"] = containerSubmesh;
	geo->DrawArgs["star"] = starSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}


void i4CastleApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
}

void i4CastleApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void i4CastleApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    stone0->Roughness = 0.3f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;

	auto crate0 = std::make_unique<Material>();
	crate0->Name = "crate0";
	crate0->MatCBIndex = 3;
	crate0->DiffuseSrvHeapIndex = 3;
	crate0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    crate0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    crate0->Roughness = 0.2f;

	auto diamondMat = std::make_unique<Material>();
	diamondMat->Name = "diaMat";
	diamondMat->MatCBIndex = 4;
	diamondMat->DiffuseSrvHeapIndex = 4;
	diamondMat->DiffuseAlbedo = XMFLOAT4(0.f, 0.f, 1.f, 1.f);
	diamondMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	diamondMat->Roughness = 0.3f;
	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["crate0"] = std::move(crate0);
	//mMaterials["diaMat"] = std::move(crate0);
}


void i4CastleApp::BuildRenderItems()
{

	auto FountainBaseCylinderRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FountainBaseCylinderRitem->World, XMMatrixScaling(4.3f, .3f, 4.3f)*XMMatrixTranslation(0.f, 0.3f, -8.f));
	XMStoreFloat4x4(&FountainBaseCylinderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	FountainBaseCylinderRitem->ObjCBIndex = 0;
	FountainBaseCylinderRitem->Mat = mMaterials["bricks0"].get();
	FountainBaseCylinderRitem->Geo = mGeometries["shapeGeo"].get();
	FountainBaseCylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FountainBaseCylinderRitem->IndexCount = FountainBaseCylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	FountainBaseCylinderRitem->StartIndexLocation = FountainBaseCylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	FountainBaseCylinderRitem->BaseVertexLocation = FountainBaseCylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FountainBaseCylinderRitem));

	auto containerRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&containerRitem->World, XMMatrixScaling(1.3f, 1.f, 1.3f)*XMMatrixTranslation(0.f, 1.3f, -8.f));
	XMStoreFloat4x4(&containerRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	containerRitem->ObjCBIndex = 1;
	containerRitem->Mat = mMaterials["bricks0"].get();
	containerRitem->Geo = mGeometries["shapeGeo"].get();
	containerRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	containerRitem->IndexCount = containerRitem->Geo->DrawArgs["container"].IndexCount;
	containerRitem->StartIndexLocation = containerRitem->Geo->DrawArgs["container"].StartIndexLocation;
	containerRitem->BaseVertexLocation = containerRitem->Geo->DrawArgs["container"].BaseVertexLocation;
	mAllRitems.push_back(std::move(containerRitem));

	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(1.f, 1.5f, 1.f)*XMMatrixTranslation(-3.5f, .5f, -8.f));
	XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidRitem->ObjCBIndex = 2;
	pyramidRitem->Mat = mMaterials["stone0"].get();
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));

	auto pyramidRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem2->World, XMMatrixScaling(1.f, 1.5f, 1.f)*XMMatrixTranslation(3.5f, .5f, -8.f));
	XMStoreFloat4x4(&pyramidRitem2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidRitem2->ObjCBIndex = 3;
	pyramidRitem2->Mat = mMaterials["stone0"].get();
	pyramidRitem2->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem2->IndexCount = pyramidRitem2->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem2->StartIndexLocation = pyramidRitem2->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem2->BaseVertexLocation = pyramidRitem2->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem2));

	auto coneRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(3.f, 2.f, 3.f)*XMMatrixTranslation(0.0f, 7.5f, 6.0f));
	XMStoreFloat4x4(&coneRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneRitem->ObjCBIndex = 4;
	coneRitem->Mat = mMaterials["stone0"].get();
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem));

	auto cylinderRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderRitem->World, XMMatrixScaling(5.f, 1.f, 5.f)*XMMatrixTranslation(0.0f, 5.f, 6.0f));
	XMStoreFloat4x4(&cylinderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderRitem->ObjCBIndex = 5;
	cylinderRitem->Mat = mMaterials["stone0"].get();
	cylinderRitem->Geo = mGeometries["shapeGeo"].get();
	cylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderRitem));

	auto HexagonRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&HexagonRitem->World, XMMatrixScaling(4.5f, 2.0f, 4.5f)*XMMatrixTranslation(0.0f, 2.0f, 6.0f));
	XMStoreFloat4x4(&HexagonRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	HexagonRitem->ObjCBIndex = 6;
	HexagonRitem->Mat = mMaterials["stone0"].get();
	HexagonRitem->Geo = mGeometries["shapeGeo"].get();
	HexagonRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	HexagonRitem->IndexCount = HexagonRitem->Geo->DrawArgs["hexagon"].IndexCount;
	HexagonRitem->StartIndexLocation = HexagonRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
	HexagonRitem->BaseVertexLocation = HexagonRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;
	mAllRitems.push_back(std::move(HexagonRitem));

	auto triPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triPrismRitem->World, XMMatrixScaling(1.5f, 1.5f, 2.5f)*XMMatrixTranslation(0.0f, 0.5f, -2.5f));
	XMStoreFloat4x4(&triPrismRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triPrismRitem->ObjCBIndex = 7;
	triPrismRitem->Mat = mMaterials["tile0"].get();
	triPrismRitem->Geo = mGeometries["shapeGeo"].get();
	triPrismRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triPrismRitem->IndexCount = triPrismRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
	triPrismRitem->StartIndexLocation = triPrismRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	triPrismRitem->BaseVertexLocation = triPrismRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triPrismRitem));

	auto leftDoorRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftDoorRitem->World, XMMatrixScaling(.5f, 2.0f, .7f)*XMMatrixRotationX(XMConvertToRadians(-90))*XMMatrixRotationY(XMConvertToRadians(-30))*XMMatrixTranslation(-1.7f, 0.25f, -12.0f));
	XMStoreFloat4x4(&leftDoorRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftDoorRitem->ObjCBIndex = 8;
	leftDoorRitem->Mat = mMaterials["tile0"].get();
	leftDoorRitem->Geo = mGeometries["shapeGeo"].get();
	leftDoorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftDoorRitem->IndexCount = leftDoorRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
	leftDoorRitem->StartIndexLocation = leftDoorRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	leftDoorRitem->BaseVertexLocation = leftDoorRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftDoorRitem));

	auto rightDoorRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightDoorRitem->World, XMMatrixScaling(.5f, 2.0f, .7f)*XMMatrixRotationX(XMConvertToRadians(-90))*XMMatrixRotationY(XMConvertToRadians(60))*XMMatrixTranslation(1.5f, 0.25f, -12.0f));
	XMStoreFloat4x4(&rightDoorRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightDoorRitem->ObjCBIndex = 9;
	rightDoorRitem->Mat = mMaterials["tile0"].get();
	rightDoorRitem->Geo = mGeometries["shapeGeo"].get();
	rightDoorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightDoorRitem->IndexCount = rightDoorRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
	rightDoorRitem->StartIndexLocation = rightDoorRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	rightDoorRitem->BaseVertexLocation = rightDoorRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightDoorRitem));

	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(.7f, .5f, .7f)*XMMatrixTranslation(0.0f, 2.f, -8.0f));
	XMStoreFloat4x4(&diamondRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamondRitem->ObjCBIndex = 10;
	diamondRitem->Mat = mMaterials["stone0"].get();
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(4.5f, 2.0f, 4.5f)*XMMatrixTranslation(0.0f, 0.5f, 6.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = 11;
	boxRitem->Mat = mMaterials["crate0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 12;
	gridRitem->Mat = mMaterials["stone0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	auto WedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&WedgeRitem->World, XMMatrixScaling(.3f, .4f, 2.5f)*XMMatrixRotationY(XMConvertToRadians(-90))*XMMatrixTranslation(0.0f, .35f, 2.5f));
	XMStoreFloat4x4(&WedgeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	WedgeRitem->ObjCBIndex = 13;
	WedgeRitem->Mat = mMaterials["stone0"].get();
	WedgeRitem->Geo = mGeometries["shapeGeo"].get();
	WedgeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	WedgeRitem->IndexCount = WedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	WedgeRitem->StartIndexLocation = WedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	WedgeRitem->BaseVertexLocation = WedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(WedgeRitem));

	auto octahedronRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&octahedronRitem1->World, XMMatrixScaling(1.f, 1.f, 1.f)* XMMatrixTranslation(3.5f, 2.f, -8.f));
	octahedronRitem1->TexTransform = MathHelper::Identity4x4();
	octahedronRitem1->ObjCBIndex = 14;
	octahedronRitem1->Mat = mMaterials["tile0"].get();
	octahedronRitem1->Geo = mGeometries["shapeGeo"].get();
	octahedronRitem1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	octahedronRitem1->IndexCount = octahedronRitem1->Geo->DrawArgs["octahedron"].IndexCount;
	octahedronRitem1->StartIndexLocation = octahedronRitem1->Geo->DrawArgs["octahedron"].StartIndexLocation;
	octahedronRitem1->BaseVertexLocation = octahedronRitem1->Geo->DrawArgs["octahedron"].BaseVertexLocation;
	mAllRitems.push_back(std::move(octahedronRitem1));

	auto octahedronRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&octahedronRitem->World, XMMatrixScaling(1.f, 1.f, 1.f)* XMMatrixTranslation(-3.5f, 2.f, -8.f));
	octahedronRitem->TexTransform = MathHelper::Identity4x4();
	octahedronRitem->ObjCBIndex = 15;
	octahedronRitem->Mat = mMaterials["tile0"].get();
	octahedronRitem->Geo = mGeometries["shapeGeo"].get();
	octahedronRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	octahedronRitem->IndexCount = octahedronRitem->Geo->DrawArgs["octahedron"].IndexCount;
	octahedronRitem->StartIndexLocation = octahedronRitem->Geo->DrawArgs["octahedron"].StartIndexLocation;
	octahedronRitem->BaseVertexLocation = octahedronRitem->Geo->DrawArgs["octahedron"].BaseVertexLocation;
	mAllRitems.push_back(std::move(octahedronRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 3.0f, 1.0f);
	XMMATRIX sphereTransform = XMMatrixScaling(1.4f, 1.4f, 1.4f);
	UINT objCBIndex = 16;
	for (int i = 0; i < 2; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-3.0f, 2.f, 1.5f + i * 8.9f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+3.0f, 2.f, 1.5f + i * 8.9f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-3.0f, 5.f, 1.5f + i * 8.9f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+3.0f, 5.f, 1.5f + i * 8.9f);

		XMStoreFloat4x4(&leftCylRitem->World, brickTexTransform * rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["stone0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["octagon"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["octagon"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["octagon"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, brickTexTransform * leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["stone0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["octagon"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["octagon"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["octagon"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, sphereTransform*leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["stone0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, sphereTransform*rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["stone0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	XMMATRIX hexTransform = XMMatrixScaling(.5f, 1.2f, .5f);
	XMMATRIX coneTransform = XMMatrixScaling(.7f, .7f, .7f);
	objCBIndex = 24;
	for (int i = 0; i < 2; ++i)
	{
		auto leftHexRitem = std::make_unique<RenderItem>();
		auto righHexRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftHexWorld = XMMatrixTranslation(-7.0f, .6f, .5f + i * 12.f);
		XMMATRIX rightHexWorld = XMMatrixTranslation(+7.0f, .6f, .5f + i * 12.f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-7.0f, 1.6f, .5f + i * 12.f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+7.0f, 1.6f, .5f + i * 12.f);

		XMStoreFloat4x4(&leftHexRitem->World, hexTransform*leftHexWorld);
		XMStoreFloat4x4(&leftHexRitem->TexTransform, brickTexTransform);
		leftHexRitem->ObjCBIndex = objCBIndex++;
		leftHexRitem->Mat = mMaterials["crate0"].get();
		leftHexRitem->Geo = mGeometries["shapeGeo"].get();
		leftHexRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftHexRitem->IndexCount = leftHexRitem->Geo->DrawArgs["hexagon"].IndexCount;
		leftHexRitem->StartIndexLocation = leftHexRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
		leftHexRitem->BaseVertexLocation = leftHexRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;

		XMStoreFloat4x4(&righHexRitem->World, hexTransform*rightHexWorld);
		XMStoreFloat4x4(&righHexRitem->TexTransform, brickTexTransform);
		righHexRitem->ObjCBIndex = objCBIndex++;
		righHexRitem->Mat = mMaterials["crate0"].get();
		righHexRitem->Geo = mGeometries["shapeGeo"].get();
		righHexRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		righHexRitem->IndexCount = righHexRitem->Geo->DrawArgs["hexagon"].IndexCount;
		righHexRitem->StartIndexLocation = righHexRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
		righHexRitem->BaseVertexLocation = righHexRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, coneTransform*leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["crate0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["cone"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["cone"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, coneTransform*rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["crate0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["cone"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["cone"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftHexRitem));
		mAllRitems.push_back(std::move(righHexRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	auto leftMainWedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftMainWedgeRitem->World, XMMatrixScaling(.3f, .4f, 4.f)*XMMatrixTranslation(-3.65f, .35f, 6.f));
	XMStoreFloat4x4(&leftMainWedgeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftMainWedgeRitem->ObjCBIndex = 32;
	leftMainWedgeRitem->Mat = mMaterials["crate0"].get();
	leftMainWedgeRitem->Geo = mGeometries["shapeGeo"].get();
	leftMainWedgeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftMainWedgeRitem->IndexCount = leftMainWedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	leftMainWedgeRitem->StartIndexLocation = leftMainWedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	leftMainWedgeRitem->BaseVertexLocation = leftMainWedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftMainWedgeRitem));

	auto rightMainWedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightMainWedgeRitem->World, XMMatrixScaling(.3f, .4f, 4.f)*XMMatrixRotationY(XMConvertToRadians(180))*XMMatrixTranslation(3.65f, .35f, 6.f));
	XMStoreFloat4x4(&rightMainWedgeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightMainWedgeRitem->ObjCBIndex = 33;
	rightMainWedgeRitem->Mat = mMaterials["crate0"].get();
	rightMainWedgeRitem->Geo = mGeometries["shapeGeo"].get();
	rightMainWedgeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightMainWedgeRitem->IndexCount = rightMainWedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	rightMainWedgeRitem->StartIndexLocation = rightMainWedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	rightMainWedgeRitem->BaseVertexLocation = rightMainWedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightMainWedgeRitem));

	auto backMainWedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backMainWedgeRitem->World, XMMatrixScaling(.3f, .4f, 2.5f)*XMMatrixRotationY(XMConvertToRadians(90))*XMMatrixTranslation(0.0f, .35f, 9.6f));
	XMStoreFloat4x4(&backMainWedgeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backMainWedgeRitem->ObjCBIndex = 34;
	backMainWedgeRitem->Mat = mMaterials["crate0"].get();
	backMainWedgeRitem->Geo = mGeometries["shapeGeo"].get();
	backMainWedgeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backMainWedgeRitem->IndexCount = backMainWedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	backMainWedgeRitem->StartIndexLocation = backMainWedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	backMainWedgeRitem->BaseVertexLocation = backMainWedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backMainWedgeRitem));

	auto stickRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&stickRitem->World, XMMatrixScaling(.2f, 1.f, .2f)*XMMatrixTranslation(0.f, 8.3f, 6.f));
	XMStoreFloat4x4(&stickRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	stickRitem->ObjCBIndex = 35;
	stickRitem->Mat = mMaterials["crate0"].get();
	stickRitem->Geo = mGeometries["shapeGeo"].get();
	stickRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	stickRitem->IndexCount = stickRitem->Geo->DrawArgs["cylinder"].IndexCount;
	stickRitem->StartIndexLocation = stickRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	stickRitem->BaseVertexLocation = stickRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(stickRitem));

	auto starRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&starRitem->World, XMMatrixScaling(.6f, 1.f, .6f)*XMMatrixTranslation(0.f, 9.5f, 6.f));
	XMStoreFloat4x4(&starRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	starRitem->ObjCBIndex = 36;
	starRitem->Mat = mMaterials["crate0"].get();
	starRitem->Geo = mGeometries["shapeGeo"].get();
	starRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	starRitem->IndexCount = starRitem->Geo->DrawArgs["star"].IndexCount;
	starRitem->StartIndexLocation = starRitem->Geo->DrawArgs["star"].StartIndexLocation;
	starRitem->BaseVertexLocation = starRitem->Geo->DrawArgs["star"].BaseVertexLocation;
	mAllRitems.push_back(std::move(starRitem));

	auto wallsInBackRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallsInBackRitem->World, XMMatrixScaling(.2f, 2.6f, 8.f)*XMMatrixTranslation(-7.0f, 0.5f, 6.5f));
	XMStoreFloat4x4(&wallsInBackRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallsInBackRitem->ObjCBIndex = 37;
	wallsInBackRitem->Mat = mMaterials["crate0"].get();
	wallsInBackRitem->Geo = mGeometries["shapeGeo"].get();
	wallsInBackRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallsInBackRitem->IndexCount = wallsInBackRitem->Geo->DrawArgs["box"].IndexCount;
	wallsInBackRitem->StartIndexLocation = wallsInBackRitem->Geo->DrawArgs["box"].StartIndexLocation;
	wallsInBackRitem->BaseVertexLocation = wallsInBackRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallsInBackRitem));

	auto wallsInBackRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallsInBackRitem2->World, XMMatrixScaling(.2f, 2.6f, 9.f)*XMMatrixRotationY(XMConvertToRadians(90))*XMMatrixTranslation(0.0f, 0.5f, 12.5f));
	XMStoreFloat4x4(&wallsInBackRitem2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallsInBackRitem2->ObjCBIndex = 38;
	wallsInBackRitem2->Mat = mMaterials["crate0"].get();
	wallsInBackRitem2->Geo = mGeometries["shapeGeo"].get();
	wallsInBackRitem2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallsInBackRitem2->IndexCount = wallsInBackRitem2->Geo->DrawArgs["box"].IndexCount;
	wallsInBackRitem2->StartIndexLocation = wallsInBackRitem2->Geo->DrawArgs["box"].StartIndexLocation;
	wallsInBackRitem2->BaseVertexLocation = wallsInBackRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallsInBackRitem2));

	auto wallsInBackRitem3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallsInBackRitem3->World, XMMatrixScaling(.2f, 2.6f, 8.f)*XMMatrixTranslation(7.0f, 0.5f, 6.5f));
	XMStoreFloat4x4(&wallsInBackRitem3->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	wallsInBackRitem3->ObjCBIndex = 39;
	wallsInBackRitem3->Mat = mMaterials["crate0"].get();
	wallsInBackRitem3->Geo = mGeometries["shapeGeo"].get();
	wallsInBackRitem3->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallsInBackRitem3->IndexCount = wallsInBackRitem3->Geo->DrawArgs["box"].IndexCount;
	wallsInBackRitem3->StartIndexLocation = wallsInBackRitem3->Geo->DrawArgs["box"].StartIndexLocation;
	wallsInBackRitem3->BaseVertexLocation = wallsInBackRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallsInBackRitem3));

	objCBIndex = 40;
	for (int i = 0; i < 2; ++i) {
		auto wallsInMidRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallsInMidRitem->World, XMMatrixScaling(.2f, 2.6f, 3.f)*XMMatrixRotationY(XMConvertToRadians(90))*XMMatrixTranslation(-5.f + 10.f*i, 0.5f, .5f));
		XMStoreFloat4x4(&wallsInMidRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		wallsInMidRitem->ObjCBIndex = objCBIndex++;
		wallsInMidRitem->Mat = mMaterials["crate0"].get();
		wallsInMidRitem->Geo = mGeometries["shapeGeo"].get();
		wallsInMidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallsInMidRitem->IndexCount = wallsInMidRitem->Geo->DrawArgs["box"].IndexCount;
		wallsInMidRitem->StartIndexLocation = wallsInMidRitem->Geo->DrawArgs["box"].StartIndexLocation;
		wallsInMidRitem->BaseVertexLocation = wallsInMidRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallsInMidRitem));
	}

	objCBIndex = 42;
	for (int i = 0; i < 2; ++i) {
		auto wallsInMidRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallsInMidRitem->World, XMMatrixScaling(.2f, 2.6f, 2.f)*XMMatrixRotationY(XMConvertToRadians(90))*XMMatrixTranslation(-4.f + 8.f*i, 0.5f, -5.5f));
		XMStoreFloat4x4(&wallsInMidRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		wallsInMidRitem->ObjCBIndex = objCBIndex++;
		wallsInMidRitem->Mat = mMaterials["crate0"].get();
		wallsInMidRitem->Geo = mGeometries["shapeGeo"].get();
		wallsInMidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallsInMidRitem->IndexCount = wallsInMidRitem->Geo->DrawArgs["box"].IndexCount;
		wallsInMidRitem->StartIndexLocation = wallsInMidRitem->Geo->DrawArgs["box"].StartIndexLocation;
		wallsInMidRitem->BaseVertexLocation = wallsInMidRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallsInMidRitem));
	}

	objCBIndex = 44;
	for (int i = 0; i < 2; ++i) {
		auto wallsInMidRitem2 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallsInMidRitem2->World, XMMatrixScaling(.2f, 2.6f, 4.f)*XMMatrixTranslation(-5.35f + 10.7f*i, 0.5f, -8.5f));
		XMStoreFloat4x4(&wallsInMidRitem2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		wallsInMidRitem2->ObjCBIndex = objCBIndex++;
		wallsInMidRitem2->Mat = mMaterials["crate0"].get();
		wallsInMidRitem2->Geo = mGeometries["shapeGeo"].get();
		wallsInMidRitem2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallsInMidRitem2->IndexCount = wallsInMidRitem2->Geo->DrawArgs["box"].IndexCount;
		wallsInMidRitem2->StartIndexLocation = wallsInMidRitem2->Geo->DrawArgs["box"].StartIndexLocation;
		wallsInMidRitem2->BaseVertexLocation = wallsInMidRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallsInMidRitem2));
	}

	objCBIndex = 46;
	for (int i = 0; i < 2; ++i) {
		auto frontWallsRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&frontWallsRitem->World, XMMatrixScaling(.2f, 2.6f, 2.f)*XMMatrixRotationY(XMConvertToRadians(90))*XMMatrixTranslation(-4.f + 8.f*i, 0.5f, -11.5f));
		XMStoreFloat4x4(&frontWallsRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		frontWallsRitem->ObjCBIndex = objCBIndex++;
		frontWallsRitem->Mat = mMaterials["crate0"].get();
		frontWallsRitem->Geo = mGeometries["shapeGeo"].get();
		frontWallsRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		frontWallsRitem->IndexCount = frontWallsRitem->Geo->DrawArgs["box"].IndexCount;
		frontWallsRitem->StartIndexLocation = frontWallsRitem->Geo->DrawArgs["box"].StartIndexLocation;
		frontWallsRitem->BaseVertexLocation = frontWallsRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(frontWallsRitem));
	}

	objCBIndex = 48;
	for (int i = 0; i < 2; ++i) {
		auto CorridorWallsRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&CorridorWallsRitem->World, XMMatrixScaling(.2f, 2.6f, 4.2f)*XMMatrixTranslation(-2.7f + 5.4f*i, 0.5f, -2.5f));
		XMStoreFloat4x4(&CorridorWallsRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		CorridorWallsRitem->ObjCBIndex = objCBIndex++;
		CorridorWallsRitem->Mat = mMaterials["crate0"].get();
		CorridorWallsRitem->Geo = mGeometries["shapeGeo"].get();
		CorridorWallsRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		CorridorWallsRitem->IndexCount = CorridorWallsRitem->Geo->DrawArgs["box"].IndexCount;
		CorridorWallsRitem->StartIndexLocation = CorridorWallsRitem->Geo->DrawArgs["box"].StartIndexLocation;
		CorridorWallsRitem->BaseVertexLocation = CorridorWallsRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(CorridorWallsRitem));
	}


	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}


void i4CastleApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;

		// CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		// tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> i4CastleApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

