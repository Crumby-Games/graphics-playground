#include "scenegraph.h"

#include "tangent_calculator.hpp"
#include "scene_utils.hpp"

#include "gltf_utils.hpp"
#include "utils.hpp"
#include "log.hpp"
#include "Scene.h"

#include "DX11Renderer.h"
#include "structures.h"


#include <cassert>
#include <array>
#include <vector>

#define UNUSED_COLOR XMFLOAT4(1.f, 0.f, 1.f, 1.f)
#define STRIP_BREAK static_cast<uint32_t>(-1)

#include <wchar.h>

// debug: redirecting cout to string
// TODO: Move to Utils
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iostream>
struct cout_redirect {
    cout_redirect(std::streambuf * new_buffer) :
        old(std::cout.rdbuf(new_buffer))
    {}

    ~cout_redirect() {
        std::cout.rdbuf(old);
    }

private:
    std::streambuf * old;
};

typedef D3D11_INPUT_ELEMENT_DESC InputElmDesc;
#define AUTO_ALIGN      D3D11_APPEND_ALIGNED_ELEMENT
#define VERTEX_DATA     D3D11_INPUT_PER_VERTEX_DATA
const std::vector<InputElmDesc> sVertexLayoutDesc =
{
    InputElmDesc{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, AUTO_ALIGN, VERTEX_DATA, 0 },
    InputElmDesc{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, AUTO_ALIGN, VERTEX_DATA, 0 },
    InputElmDesc{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, AUTO_ALIGN, VERTEX_DATA, 0 },
    InputElmDesc{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, AUTO_ALIGN, VERTEX_DATA, 0 },
};

struct CbScene
{
    XMMATRIX ViewMtrx;
    XMFLOAT4 CameraPos;
    XMMATRIX ProjectionMtrx;
};

struct CbFrame
{
    XMFLOAT4 AmbientLightLuminance;

    XMFLOAT4 DirectLightDirs[DIRECT_LIGHTS_MAX_COUNT];
    XMFLOAT4 DirectLightLuminances[DIRECT_LIGHTS_MAX_COUNT];

    XMFLOAT4 PointLightPositions[POINT_LIGHTS_MAX_COUNT];
    XMFLOAT4 PointLightIntensities[POINT_LIGHTS_MAX_COUNT];

    int32_t  DirectLightsCount; // at the end to avoid 16-byte packing issues
    int32_t  PointLightsCount;  // at the end to avoid 16-byte packing issues
    int32_t  dummy_padding[2];  // padding to 16 bytes multiple
};

struct CbSceneNode
{
    XMMATRIX WorldMtrx;
    XMFLOAT4 MeshColor; // May be eventually replaced by the emmisive component of the standard surface shader
};

struct CbScenePrimitive
{
    // Metallness
    XMFLOAT4 BaseColorFactor;
    XMFLOAT4 MetallicRoughnessFactor;

    // Specularity
    XMFLOAT4 DiffuseColorFactor;
    XMFLOAT4 SpecularFactor;

    // Both workflows
    float    NormalTexScale;
    float    OcclusionTexStrength;
    float    padding[2];  // padding to 16 bytes
    XMFLOAT4 EmissionFactor;
};

SceneGraph::SceneGraph(const SceneId sceneId) :
    mSceneId(sceneId)
{
}

SceneGraph::~SceneGraph()
{
    Destroy();
}

SceneNode* SceneGraph::CreateRootNode()
{
    // Add a new root node to our vector of root nodes
    mRootNodes.emplace_back(true); // 'true' means this is a root node
    // Return a pointer to the new node we just created
    return &mRootNodes.back();
}



bool SceneGraph::Init(IRenderingContext &ctx)
{
    if (!ctx.IsValid())
        return false;

    return true;
}


bool SceneGraph::LoadExternal(IRenderingContext &ctx, const std::wstring &filePath)
{
    const auto fileExt = Utils::GetFilePathExt(filePath);
    if ((fileExt.compare(L"glb") == 0) ||
        (fileExt.compare(L"gltf") == 0))
    {
        return LoadGLTF(ctx, filePath);
    }
    else
    {
        Log::Error(L"The scene file has an unsupported file format extension (%s)!", fileExt.c_str());
        return false;
    }
}



const tinygltf::Accessor& GetPrimitiveAttrAccessor(bool &accessorLoaded,
                                                   const tinygltf::Model &model,
                                                   const std::map<std::string, int> &attributes,
                                                   const int primitiveIdx,
                                                   bool requiredData,
                                                   const std::string &attrName,
                                                   const std::wstring &logPrefix)
{
    static tinygltf::Accessor dummyAccessor;

    const auto attrIt = attributes.find(attrName);
    if (attrIt == attributes.end())
    {
        Log::Write(requiredData ? Log::eError : Log::eDebug,
                   L"%sNo %s attribute present in primitive %d!",
                   logPrefix.c_str(),
                   Utils::StringToWstring(attrName).c_str(),
                   primitiveIdx);
        accessorLoaded = false;
        return dummyAccessor;
    }

    const auto accessorIdx = attrIt->second;
    if ((accessorIdx < 0) || (accessorIdx >= model.accessors.size()))
    {
        Log::Error(L"%sInvalid %s accessor index (%d/%d)!",
                   logPrefix.c_str(),
                   Utils::StringToWstring(attrName).c_str(),
                   accessorIdx,
                   model.accessors.size());
        accessorLoaded = false;
        return dummyAccessor;
    }

    accessorLoaded = true;
    return model.accessors[accessorIdx];
}

template <typename ComponentType,
          size_t ComponentCount,
          typename TDataConsumer>
bool IterateGltfAccesorData(const tinygltf::Model &model,
                            const tinygltf::Accessor &accessor,
                            TDataConsumer DataConsumer,
                            const wchar_t *logPrefix,
                            const wchar_t *logDataName)
{
    Log::Debug(L"%s%s accesor \"%s\": view %d, offset %d, type %s<%s>, count %d",
               logPrefix,
               logDataName,
               Utils::StringToWstring(accessor.name).c_str(),
               accessor.bufferView,
               accessor.byteOffset,
               GltfUtils::TypeToWstring(accessor.type).c_str(),
               GltfUtils::ComponentTypeToWstring(accessor.componentType).c_str(),
               accessor.count);

    // Buffer view

    const auto bufferViewIdx = accessor.bufferView;

    if ((bufferViewIdx < 0) || (bufferViewIdx >= model.bufferViews.size()))
    {
        Log::Error(L"%sInvalid %s view buffer index (%d/%d)!",
                   logPrefix, logDataName, bufferViewIdx, model.bufferViews.size());
        return false;
    }

    const auto &bufferView = model.bufferViews[bufferViewIdx];

    //Log::Debug(L"%s%s buffer view %d \"%s\": buffer %d, offset %d, length %d, stride %d, target %s",
    //           logPrefix,
    //           logDataName,
    //           bufferViewIdx,
    //           Utils::StringToWstring(bufferView.name).c_str(),
    //           bufferView.buffer,
    //           bufferView.byteOffset,
    //           bufferView.byteLength,
    //           bufferView.byteStride,
    //           GltfUtils::TargetToWstring(bufferView.target).c_str());

    // Buffer

    const auto bufferIdx = bufferView.buffer;

    if ((bufferIdx < 0) || (bufferIdx >= model.buffers.size()))
    {
        Log::Error(L"%sInvalid %s buffer index (%d/%d)!",
                   logPrefix, logDataName, bufferIdx, model.buffers.size());
        return false;
    }

    const auto &buffer = model.buffers[bufferIdx];

    const auto byteEnd = bufferView.byteOffset + bufferView.byteLength;
    if (byteEnd > buffer.data.size())
    {
        Log::Error(L"%sAccessing data chunk outside %s buffer %d!",
                   logPrefix, logDataName, bufferIdx);
        return false;
    }

    //Log::Debug(L"%s%s buffer %d \"%s\": data %x, size %d, uri \"%s\"",
    //           logPrefix,
    //           logDataName,
    //           bufferIdx,
    //           Utils::StringToWstring(buffer.name).c_str(),
    //           buffer.data.data(),
    //           buffer.data.size(),
    //           Utils::StringToWstring(buffer.uri).c_str());

    // Data

    const auto componentSize = sizeof(ComponentType);
    const auto typeSize = ComponentCount * componentSize;
    const auto stride = bufferView.byteStride;
    const auto typeOffset = (stride == 0) ? typeSize : stride;

    auto ptr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    int idx = 0;
    for (; idx < accessor.count; ++idx, ptr += typeOffset)
        DataConsumer(idx, ptr);

    return true;
}

// Helper function to simplify calls
void PrintDebug(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
}

void printMeshNames(tinygltf::Model model)
{
    std::stringstream ss;
    // 1. List Meshes
    ss << "--- Listing Meshes ---";
    PrintDebug(ss.str());
    ss.str(""); // Clear the stream

    for (size_t i = 0; i < model.meshes.size(); ++i) {
        const tinygltf::Mesh& mesh = model.meshes[i];
        ss << "Mesh " << i << ": " << (mesh.name.empty() ? "[unnamed]" : mesh.name);
        PrintDebug(ss.str());
        ss.str("");
    }
}

void printSkinAndBones(tinygltf::Model model)
{
    std::stringstream ss;

    // 2. List Skins and Bones (Joints)
    ss << "\n--- Listing Skins & Bones ---";
    PrintDebug(ss.str());
    ss.str("");

    for (size_t i = 0; i < model.skins.size(); ++i) {
        const tinygltf::Skin& skin = model.skins[i];
        ss << "Skin " << i << ": " << (skin.name.empty() ? "[unnamed]" : skin.name);
        PrintDebug(ss.str());
        ss.str("");

        ss << "  Bones (Joints):";
        PrintDebug(ss.str());
        ss.str("");
        for (size_t j = 0; j < skin.joints.size(); ++j) {
            int joint_node_idx = skin.joints[j];
            const tinygltf::Node& joint_node = model.nodes[joint_node_idx];
            ss << "    Bone Index " << j << " -> Node " << joint_node_idx
                << " (" << (joint_node.name.empty() ? "[unnamed]" : joint_node.name) << ")";
            PrintDebug(ss.str());
            ss.str("");
        }
    }
}

void printWeightsToBones(tinygltf::Model model)
{
    std::stringstream ss;
    ss << "\n--- Listing Vertex Skinning Attributes ---";
    PrintDebug(ss.str());
    ss.str("");

    for (size_t i = 0; i < model.meshes.size(); ++i) {
        const tinygltf::Mesh& mesh = model.meshes[i];
        ss << "Inspecting Mesh " << i << " (" << (mesh.name.empty() ? "[unnamed]" : mesh.name) << ")";
        PrintDebug(ss.str());
        ss.str("");

        for (size_t j = 0; j < mesh.primitives.size(); ++j) {
            const tinygltf::Primitive& primitive = mesh.primitives[j];
            if (primitive.attributes.count("JOINTS_0") && primitive.attributes.count("WEIGHTS_0")) {
                const tinygltf::Accessor& joint_accessor = model.accessors[primitive.attributes.at("JOINTS_0")];
                const tinygltf::Accessor& weight_accessor = model.accessors[primitive.attributes.at("WEIGHTS_0")];
                ss << "  Primitive " << j << " is skinned. Found " << joint_accessor.count << " vertex skinning entries.";
                PrintDebug(ss.str());
                ss.str("");
            }
            else {
                ss << "  Primitive " << j << " is not skinned.";
                PrintDebug(ss.str());
                ss.str("");
            }
        }
    }
}

void printAnimations(tinygltf::Model model)
{
    std::stringstream ss;
    ss << "\n--- Listing Animations ---";
    PrintDebug(ss.str());
    ss.str("");

    for (size_t i = 0; i < model.animations.size(); ++i) {
        const tinygltf::Animation& anim = model.animations[i];
        ss << "Animation " << i << ": " << (anim.name.empty() ? "[unnamed]" : anim.name);
        PrintDebug(ss.str());
        ss.str("");

        for (size_t j = 0; j < anim.channels.size(); ++j) {
            const tinygltf::AnimationChannel& channel = anim.channels[j];
            const tinygltf::AnimationSampler& sampler = anim.samplers[channel.sampler];
            const tinygltf::Node& target_node = model.nodes[channel.target_node];

            ss << "  Channel " << j << " targets Node " << channel.target_node
                << " (" << (target_node.name.empty() ? "[unnamed]" : target_node.name) << ") -> Path: " << channel.target_path;
            PrintDebug(ss.str());
            ss.str("");

            const tinygltf::Accessor& input_accessor = model.accessors[sampler.input];
            const tinygltf::Accessor& output_accessor = model.accessors[sampler.output];

            ss << "    - Keyframes: " << input_accessor.count
                << " | Interpolation: " << sampler.interpolation;
            PrintDebug(ss.str());
            ss.str("");
        }
    }
}

void SceneGraph::AnimateFrame(IRenderingContext& ctx)
{
    if (!ctx.IsValid())
        return;

    // Scene geometry
    for (auto& node : mRootNodes)
        node.Animate(ctx);
}

bool SceneGraph::LoadSphere(IRenderingContext& ctx)
{
    mRootNodes.clear();
    mRootNodes.reserve(1);
   
    SceneNode sceneNode(true);
    bool ok = sceneNode.LoadSphere(ctx);
    mRootNodes.push_back(std::move(sceneNode));

    return ok;
}

bool SceneGraph::LoadGLTF(IRenderingContext &ctx,
                     const std::wstring &filePath)
{
    using namespace std;

    Log::Debug(L"");
    const std::wstring logPrefix = L"LoadGLTF: ";

    tinygltf::Model model;
    if (!GltfUtils::LoadModel(model, filePath))
        return false;

   if (!LoadSceneFromGltf(ctx, model, logPrefix))
        return false;

   // SetupDefaultLights();

    Log::Debug(L"");

    return true;
}

bool SceneGraph::LoadGLTFWithSkeleton(IRenderingContext& ctx,
    const std::wstring& filePath)
{
    using namespace std;

    Log::Debug(L"");
    const std::wstring logPrefix = L"LoadGLTF: ";

    tinygltf::Model model;
    if (!GltfUtils::LoadModel(model, filePath))
        return false;

    if (!LoadSceneFromGltfWithSkeleton(ctx, model, logPrefix))
        return false;

//    SetupDefaultLights();

    Log::Debug(L"");

    return true;
}

bool SceneGraph::LoadSceneFromGltf(IRenderingContext &ctx,
                              const tinygltf::Model &model,
                              const std::wstring &logPrefix)
{
    // Choose one scene
    if (model.scenes.size() < 1)
    {
        Log::Error(L"%sNo scenes present in the model!", logPrefix.c_str());
        return false;
    }
    if (model.scenes.size() > 1)
        Log::Warning(L"%sMore scenes present in the model. Loading just the first one.", logPrefix.c_str());
    const auto &scene = model.scenes[0];

    Log::Debug(L"%sScene 0 \"%s\": %d root node(s)",
               logPrefix.c_str(),
               Utils::StringToWstring(scene.name).c_str(),
               scene.nodes.size());

    // Nodes hierarchy
    mRootNodes.clear();
    mRootNodes.reserve(scene.nodes.size());
    for (const auto nodeIdx : scene.nodes)
    {
        SceneNode sceneNode(true);
        if (!LoadSceneNodeFromGLTF(ctx, sceneNode, model, nodeIdx, logPrefix + L"   "))
            return false;
        mRootNodes.push_back(std::move(sceneNode));
    }

    return true;
}

bool SceneGraph::LoadSceneFromGltfWithSkeleton(IRenderingContext& ctx,
    const tinygltf::Model& model,
    const std::wstring& logPrefix)
{
    // Choose one scene
    if (model.scenes.size() < 1)
    {
        Log::Error(L"%sNo scenes present in the model!", logPrefix.c_str());
        return false;
    }
    if (model.scenes.size() > 1)
        Log::Warning(L"%sMore scenes present in the model. Loading just the first one.", logPrefix.c_str());
    const auto& scene = model.scenes[0];

    Log::Debug(L"%sScene 0 \"%s\": %d root node(s)",
        logPrefix.c_str(),
        Utils::StringToWstring(scene.name).c_str(),
        scene.nodes.size());

    // Nodes hierarchy
    mRootNodes.clear();
    mRootNodes.reserve(scene.nodes.size());
    for (const auto nodeIdx : scene.nodes)
    {
        SceneNode sceneNode(true);

        printMeshNames(model);
        printSkinAndBones(model);
        printWeightsToBones(model);
        printAnimations(model);

        sceneNode.m_skeleton.LoadFromGltf(model);
        sceneNode.m_skeleton.Update(0);
        
        if (!LoadSceneNodeFromGLTF(ctx, sceneNode, model, nodeIdx, logPrefix + L"   "))
            return false;
        //mRootNodes.push_back(std::move(sceneNode));
        mRootNodes.push_back(sceneNode);
    }

    return true;
}


bool SceneGraph::LoadSceneNodeFromGLTF(IRenderingContext &ctx,
                                  SceneNode &sceneNode,
                                  const tinygltf::Model &model,
                                  int nodeIdx,
                                  const std::wstring &logPrefix)
{
    if (nodeIdx >= model.nodes.size())
    {
        Log::Error(L"%sInvalid node index (%d/%d)!", logPrefix.c_str(), nodeIdx, model.nodes.size());
        return false;
    }

    const auto &node = model.nodes[nodeIdx];

    // Node itself
    if (!sceneNode.LoadFromGLTF(ctx, model, node, nodeIdx, logPrefix))
        return false;

    // Children
    sceneNode.mChildren.clear();
    sceneNode.mChildren.reserve(node.children.size());
    const std::wstring &childLogPrefix = logPrefix + L"   ";
    for (const auto childIdx : node.children)
    {
        if ((childIdx < 0) || (childIdx >= model.nodes.size()))
        {
            Log::Error(L"%sInvalid child node index (%d/%d)!", childLogPrefix.c_str(), childIdx, model.nodes.size());
            return false;
        }

        SceneNode childNode;
        if (!LoadSceneNodeFromGLTF(ctx, childNode, model, childIdx, childLogPrefix))
            return false;
        sceneNode.mChildren.push_back(std::move(childNode));
    }

    return true;
}

void SceneGraph::Destroy()
{
    Utils::ReleaseAndMakeNull(mVertexShader);

    Utils::ReleaseAndMakeNull(mVertexLayout);

    Utils::ReleaseAndMakeNull(mCbScene);
    Utils::ReleaseAndMakeNull(mCbFrame);
    Utils::ReleaseAndMakeNull(mCbSceneNode);
    Utils::ReleaseAndMakeNull(mCbScenePrimitive);

    Utils::ReleaseAndMakeNull(mSamplerLinear);

    mRootNodes.clear();
}

void SceneGraph::AddScaleToRoots(double scale)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddScale(scale);
}


void SceneGraph::AddScaleToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddScale(vec);
}


void SceneGraph::AddRotationQuaternionToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddRotationQuaternion(vec);
}


void SceneGraph::AddTranslationToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddTranslation(vec);
}


void SceneGraph::AddMatrixToRoots(const std::vector<double> &vec)
{
    for (auto &rootNode : mRootNodes)
        rootNode.AddMatrix(vec);
}

void SceneGraph::AddMatrixToRoots(const XMMATRIX& mat)
{
    for (auto& rootNode : mRootNodes)
        rootNode.AddMatrix(mat);
}

void SceneGraph::SetMatrixToRoots(const XMMATRIX& mat)
{
    for (auto& rootNode : mRootNodes)
        rootNode.SetMatrix(mat);
}

void SceneGraph::RenderFrame(IRenderingContext& ctx, const float deltaTime)
{
    if (!ctx.IsValid())
        return;

    ConstantBuffer* data = &ctx.getDXRenderer()->m_ConstantBufferData;
    data->mView = XMMatrixTranspose(ctx.getDXRenderer()->m_pScene->m_pCamera->getViewMatrix());
    data->mProjection = XMMatrixTranspose(XMLoadFloat4x4(&ctx.getDXRenderer()->m_matProjection));

    // Scene geometry
    for (auto& node : mRootNodes)
        RenderNode(ctx, node, XMMatrixIdentity(), deltaTime);

}


void SceneGraph::RenderNode(IRenderingContext &ctx,
                       SceneNode &node,
                       const XMMATRIX &parentWorldMtrx,
                        const float deltaTime)
{
    if (!ctx.IsValid())
        return;

    XMMATRIX matWorld = XMLoadFloat4x4(&node.mWorldMtrx);

    XMMATRIX world = matWorld * parentWorldMtrx;
    ConstantBuffer* data = &ctx.getDXRenderer()->m_ConstantBufferData;
    if (node.m_skeleton.IsLoaded())
    {
        if (node.m_skeleton.CurrentAnimation() == nullptr)
            node.m_skeleton.PlayAnimation((unsigned int)0);
        node.m_skeleton.Update(deltaTime);
        data->bone_count = node.m_skeleton.GetBoneCount();
        node.m_skeleton.GetSkinningMatrices(data->boneTransforms, max_bones);
    }

    // Draw current node
    for (auto &primitive : node.mPrimitives)
    {
        // update the per-node constant buffer
        auto immCtx = ctx.GetImmediateContext();
        
        // store world and the view / projection in a constant buffer for the vertex shader to use
        data->mWorld = DirectX::XMMatrixTranspose(world);
        ctx.GetImmediateContext()->UpdateSubresource(ctx.getDXRenderer()->m_pScene->m_pConstantBuffer.Get(), 0, nullptr, data, 0, 0);

        // Render a cube
        ctx.GetImmediateContext()->VSSetShader(ctx.getDXRenderer()->m_pVertexShader.Get(), nullptr, 0);
        ctx.GetImmediateContext()->VSSetConstantBuffers(0, 1, ctx.getDXRenderer()->m_pScene->m_pConstantBuffer.GetAddressOf());

        primitive.DrawGeometry(ctx, ctx.getDXRenderer()->m_pVertexLayout.Get());
    }

    // Children
    for (auto &child : node.mChildren)
        RenderNode(ctx, child, world, deltaTime);
}

ScenePrimitive::ScenePrimitive()
{}

ScenePrimitive::ScenePrimitive(const ScenePrimitive &src) :
    mVertices(src.mVertices),
    mIndices(src.mIndices),
    mTopology(src.mTopology),
    mIsTangentPresent(src.mIsTangentPresent),
    mVertexBuffer(src.mVertexBuffer),
    mIndexBuffer(src.mIndexBuffer),
    mMaterialIdx(src.mMaterialIdx)
{
    // We are creating new references of device resources
    Utils::SafeAddRef(mVertexBuffer);
    Utils::SafeAddRef(mIndexBuffer);
}

ScenePrimitive::ScenePrimitive(ScenePrimitive &&src) :
    mVertices(std::move(src.mVertices)),
    mIndices(std::move(src.mIndices)),
    mIsTangentPresent(Utils::Exchange(src.mIsTangentPresent, false)),
    mTopology(Utils::Exchange(src.mTopology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)),
    mVertexBuffer(Utils::Exchange(src.mVertexBuffer, nullptr)),
    mIndexBuffer(Utils::Exchange(src.mIndexBuffer, nullptr)),
    mMaterialIdx(Utils::Exchange(src.mMaterialIdx, -1))
{}

ScenePrimitive& ScenePrimitive::operator =(const ScenePrimitive &src)
{
    mVertices = src.mVertices;
    mIndices = src.mIndices;
    mIsTangentPresent = src.mIsTangentPresent;
    mTopology = src.mTopology;
    mVertexBuffer = src.mVertexBuffer;
    mIndexBuffer = src.mIndexBuffer;

    // We are creating new references of device resources
    Utils::SafeAddRef(mVertexBuffer);
    Utils::SafeAddRef(mIndexBuffer);

    mMaterialIdx = src.mMaterialIdx;

    return *this;
}

ScenePrimitive& ScenePrimitive::operator =(ScenePrimitive &&src)
{
    mVertices = std::move(src.mVertices);
    mIndices = std::move(src.mIndices);
    mIsTangentPresent = Utils::Exchange(src.mIsTangentPresent, false);
    mTopology = Utils::Exchange(src.mTopology, D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
    mVertexBuffer = Utils::Exchange(src.mVertexBuffer, nullptr);
    mIndexBuffer = Utils::Exchange(src.mIndexBuffer, nullptr);

    mMaterialIdx = Utils::Exchange(src.mMaterialIdx, -1);

    return *this;
}

ScenePrimitive::~ScenePrimitive()
{
    Destroy();
}


bool ScenePrimitive::CreateQuad(IRenderingContext & ctx)
{
    if (!GenerateQuadGeometry())
        return false;
    if (!CreateDeviceBuffers(ctx))
        return false;

    return true;
}


bool ScenePrimitive::CreateCube(IRenderingContext & ctx)
{
    if (!GenerateCubeGeometry())
        return false;
    if (!CreateDeviceBuffers(ctx))
        return false;

    return true;
}


bool ScenePrimitive::CreateOctahedron(IRenderingContext & ctx)
{
    if (!GenerateOctahedronGeometry())
        return false;
    if (!CreateDeviceBuffers(ctx))
        return false;

    return true;
}


bool ScenePrimitive::CreateSphere(IRenderingContext & ctx,
                                  const WORD vertSegmCount,
                                  const WORD stripCount)
{
    if (!GenerateSphereGeometry(vertSegmCount, stripCount))
        return false;
    if (!CreateDeviceBuffers(ctx))
        return false;

    return true;
}


bool ScenePrimitive::GenerateQuadGeometry()
{
    mVertices =
    {
        SceneVertex{ XMFLOAT3(-1.0f, 0.0f, -1.0f),  XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3( 1.0f, 0.0f, -1.0f),  XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3( 1.0f, 0.0f,  1.0f),  XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(-1.0f, 0.0f,  1.0f),  XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },
    };
    
    mIndices =
    {
        3, 1, 0,
        2, 1, 3,
    };

    mTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    CalculateTangentsIfNeeded();

    return true;
}


bool ScenePrimitive::GenerateCubeGeometry()
{
    mVertices =
    {
        // Up
        SceneVertex{ XMFLOAT3(-1.0f, 1.0f, -1.0f),  XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, 1.0f, -1.0f),   XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, 1.0f,  1.0f),   XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(-1.0f, 1.0f,  1.0f),  XMFLOAT3(0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },

        // Down
        SceneVertex{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, -1.0f, -1.0f),  XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, -1.0f,  1.0f),  XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },

        // Side 1
        SceneVertex{ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },

        // Side 3
        SceneVertex{ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },

        // Side 2
        SceneVertex{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, -1.0f, -1.0f),  XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f,  1.0f, -1.0f),  XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },

        // Side 4
        SceneVertex{ XMFLOAT3(-1.0f, -1.0f, 1.0f),  XMFLOAT3(0.0f, 0.0f, 1.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f, -1.0f, 1.0f),   XMFLOAT3(0.0f, 0.0f, 1.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 0.0f) },
        SceneVertex{ XMFLOAT3(1.0f,  1.0f, 1.0f),   XMFLOAT3(0.0f, 0.0f, 1.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
        SceneVertex{ XMFLOAT3(-1.0f,  1.0f, 1.0f),  XMFLOAT3(0.0f, 0.0f, 1.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 1.0f) },
    };

    mIndices =
    {
        // Up
        3, 1, 0,
        2, 1, 3,

        // Down
        6, 4, 5,
        7, 4, 6,

        // Side 1
        11, 9, 8,
        10, 9, 11,

        // Side 3
        14, 12, 13,
        15, 12, 14,

        // Side 2
        19, 17, 16,
        18, 17, 19,

        // Side 4
        22, 20, 21,
        23, 20, 22
    };

    mTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    CalculateTangentsIfNeeded();

    return true;
}


bool ScenePrimitive::GenerateOctahedronGeometry()
{
    mVertices =
    {
        // Noth pole
        SceneVertex{ XMFLOAT3( 0.0f, 1.0f, 0.0f),  XMFLOAT3( 0.0f, 1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.0f, 0.0f) },

        // Points on equator
        SceneVertex{ XMFLOAT3( 1.0f, 0.0f, 0.0f),  XMFLOAT3( 1.0f, 0.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.00f, 0.5f) },
        SceneVertex{ XMFLOAT3( 0.0f, 0.0f, 1.0f),  XMFLOAT3( 0.0f, 0.0f, 1.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.25f, 0.5f) },
        SceneVertex{ XMFLOAT3(-1.0f, 0.0f, 0.0f),  XMFLOAT3(-1.0f, 0.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.50f, 0.5f) },
        SceneVertex{ XMFLOAT3( 0.0f, 0.0f,-1.0f),  XMFLOAT3( 0.0f, 0.0f,-1.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(0.75f, 0.5f) },

        // South pole
        SceneVertex{ XMFLOAT3( 0.0f,-1.0f, 0.0f),  XMFLOAT3( 0.0f,-1.0f, 0.0f),  XMFLOAT4(0,0,0,0), XMFLOAT2(1.0f, 1.0f) },
    };

    mIndices =
    {
        // Band ++
        0, 2, 1,
        1, 2, 5,

        // Band -+
        0, 3, 2,
        2, 3, 5,

        // Band --
        0, 4, 3,
        3, 4, 5,

        // Band +-
        0, 1, 4,
        4, 1, 5,
    };

    mTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    CalculateTangentsIfNeeded();

    return true;
}


bool ScenePrimitive::GenerateSphereGeometry(const WORD vertSegmCount, const WORD stripCount)
{
    if (vertSegmCount < 2)
    {
        Log::Error(L"Spherical stripe must have at least 2 vertical segments");
        return false;
    }
    if (stripCount < 3)
    {
        Log::Error(L"Sphere must have at least 3 stripes");
        return false;
    }

    const WORD horzLineCount = vertSegmCount - 1;
    const WORD vertexCountPerStrip = 2 /*poles*/ + horzLineCount;
    const WORD vertexCount = (stripCount + 1) * vertexCountPerStrip;
    const WORD indexCount  = stripCount * (2 /*poles*/ + 2 * horzLineCount + 1 /*strip restart*/);

    // Vertices
    mVertices.reserve(vertexCount);
    const float stripSizeAng = XM_2PI / stripCount;
    const float stripSizeRel =    1.f / stripCount;
    const float vertSegmSizeAng = XM_PI / vertSegmCount;
    const float vertSegmSizeRel =   1.f / vertSegmCount;
    for (WORD strip = 0; strip <= stripCount; strip++) // first and last vertices need to be replicated due to texture stitching
    {
        // Inner segments
        const float phi = strip * stripSizeAng;
        const float xBase = cos(phi);
        const float zBase = sin(phi);
        const float uLine = strip * stripSizeRel * 1.000001f;
        for (WORD line = 0; line < horzLineCount; line++)
        {
            const float theta = (line + 1) * vertSegmSizeAng;
            const float ringRadius = sin(theta);
            const float y = cos(theta);
            const XMFLOAT3 pt(xBase * ringRadius, y, zBase * ringRadius);
            const float v = (line + 1) * vertSegmSizeRel;
            mVertices.push_back(SceneVertex{ pt, pt,  XMFLOAT4(0,0,0,0), XMFLOAT2(uLine, v) }); // position==normal
        }

        // Poles
        const XMFLOAT3 northPole(0.0f,  1.0f, 0.0f);
        const XMFLOAT3 southPole(0.0f, -1.0f, 0.0f);
        const float uPole = uLine + stripSizeRel / 2;
        mVertices.push_back(SceneVertex{ northPole,  northPole,  XMFLOAT4(0,0,0,0), XMFLOAT2(uPole, 0.0f) }); // position==normal
        mVertices.push_back(SceneVertex{ southPole,  southPole,  XMFLOAT4(0,0,0,0), XMFLOAT2(uPole, 1.0f) }); // position==normal
    }

    assert(mVertices.size() == vertexCount);

    // Indices
    mIndices.reserve(indexCount);
    for (WORD strip = 0; strip < stripCount; strip++)
    {
        const WORD idxOffset = strip * vertexCountPerStrip;
        mIndices.push_back(idxOffset + vertexCountPerStrip - 2); // north pole
        for (WORD line = 0; line < horzLineCount; line++)
        {
            mIndices.push_back((idxOffset + line + vertexCountPerStrip) % vertexCount); // next strip, same line
            mIndices.push_back( idxOffset + line);
        }
        mIndices.push_back(idxOffset + vertexCountPerStrip - 1); // south pole
        mIndices.push_back(STRIP_BREAK);
    }

    assert(mIndices.size() == indexCount);

    mTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    //mTopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; // debug

    CalculateTangentsIfNeeded();

    Log::Debug(L"ScenePrimitive::GenerateSphereGeometry: "
               L"%d segments, %d strips => %d triangles, %d vertices, %d indices",
               vertSegmCount, stripCount,
               stripCount * (2 * horzLineCount),
               vertexCount, indexCount);

    return true;
}


bool ScenePrimitive::LoadFromGLTF(IRenderingContext & ctx,
                                  const tinygltf::Model &model,
                                  const tinygltf::Mesh &mesh,
                                  const int primitiveIdx,
                                  const std::wstring &logPrefix)
{
    if (!LoadDataFromGLTF(model, mesh, primitiveIdx, logPrefix))
        return false;
    if (!CreateDeviceBuffers(ctx))
        return false;

    return true;
}


bool ScenePrimitive::LoadDataFromGLTF(const tinygltf::Model &model,
                                      const tinygltf::Mesh &mesh,
                                      const int primitiveIdx,
                                      const std::wstring &logPrefix)
{
    bool success = false;
    const auto &primitive = mesh.primitives[primitiveIdx];
    const auto &attrs = primitive.attributes;
    const auto subItemsLogPrefix = logPrefix + L"   ";
    const auto dataConsumerLogPrefix = subItemsLogPrefix + L"   ";

    Log::Debug(L"%sPrimitive %d/%d: mode %s, attributes [%s], indices %d, material %d",
               logPrefix.c_str(),
               primitiveIdx,
               mesh.primitives.size(),
               GltfUtils::ModeToWstring(primitive.mode).c_str(),
               GltfUtils::StringIntMapToWstring(primitive.attributes).c_str(),
               primitive.indices,
               primitive.material);

    // Positions

    auto &posAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                 true, "POSITION", subItemsLogPrefix.c_str());
    if (!success)
        return false;

    if ((posAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
        (posAccessor.type != TINYGLTF_TYPE_VEC3))
    {
        Log::Error(L"%sUnsupported POSITION data type!", subItemsLogPrefix.c_str());
        return false;
    }

    mVertices.clear();
    mVertices.reserve(posAccessor.count);
    if (mVertices.capacity() < posAccessor.count)
    {
        Log::Error(L"%sUnable to allocate %d vertices!", subItemsLogPrefix.c_str(), posAccessor.count);
        mVertices.clear();
        return false;
    }

    auto PositionDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
    {
        auto pos = *reinterpret_cast<const XMFLOAT3*>(ptr);

        itemIdx; // unused param
        //Log::Debug(L"%s%d: pos [%.4f, %.4f, %.4f]",
        //           dataConsumerLogPrefix.c_str(),
        //           itemIdx,
        //           pos.x, pos.y, pos.z);

        mVertices.push_back(SceneVertex{ XMFLOAT3(pos.x, pos.y, pos.z),
                                         XMFLOAT3(0.0f, 0.0f, 1.0f), // TODO: Leave invalid?
                                         XMFLOAT4(1.0f, 0.5f, 0.0f, 1.0f),  // debug; TODO: Leave invalid?
                                         XMFLOAT2(0.0f, 0.0f) });
    };

    if (!IterateGltfAccesorData<float, 3>(model,
                                          posAccessor,
                                          PositionDataConsumer,
                                          subItemsLogPrefix.c_str(),
                                          L"Position"))
        return false;

    // Normals
    auto &normalAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                    false, "NORMAL", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((normalAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (normalAccessor.type != TINYGLTF_TYPE_VEC3))
        {
            Log::Error(L"%sUnsupported NORMAL data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (normalAccessor.count != posAccessor.count)
        {
            Log::Error(L"%sNormals count (%d) is different from position count (%d)!",
                       subItemsLogPrefix.c_str(), normalAccessor.count, posAccessor.count);
            return false;
        }

        auto NormalDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
        {
            auto normal = *reinterpret_cast<const XMFLOAT3*>(ptr);

            //Log::Debug(L"%s%d: normal [%.4f, %.4f, %.4f]",
            //           dataConsumerLogPrefix.c_str(),
            //           itemIdx, normal.x, normal.y, normal.z);

            mVertices[itemIdx].Normal = normal;
        };

        if (!IterateGltfAccesorData<float, 3>(model,
                                              normalAccessor,
                                              NormalDataConsumer,
                                              subItemsLogPrefix.c_str(),
                                              L"Normal"))
            return false;
    }
    //else
    //{
    //    // No normals provided
    //    // TODO: Generate?
    //}

    // Tangents
    auto &tangentAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                     false, "TANGENT", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((tangentAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (tangentAccessor.type != TINYGLTF_TYPE_VEC4))
        {
            Log::Error(L"%sUnsupported TANGENT data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (tangentAccessor.count != posAccessor.count)
        {
            Log::Error(L"%sTangents count (%d) is different from position count (%d)!",
                       subItemsLogPrefix.c_str(), tangentAccessor.count, posAccessor.count);
            return false;
        }

        auto TangentDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
        {
            auto tangent = *reinterpret_cast<const XMFLOAT4*>(ptr);

            //Log::Debug(L"%s%d: tangent [%7.4f, %7.4f, %7.4f] * %.1f",
            //           dataConsumerLogPrefix.c_str(), itemIdx,
            //           tangent.x, tangent.y, tangent.z, tangent.w);

            if ((tangent.w != 1.f) && (tangent.w != -1.f))
                Log::Warning(L"%s%d: tangent w component (handedness) is not equal to 1 or -1 but to %7.4f",
                           dataConsumerLogPrefix.c_str(), itemIdx, tangent.w);

            mVertices[itemIdx].Tangent = tangent;
        };

        if (!IterateGltfAccesorData<float, 4>(model,
                                              tangentAccessor,
                                              TangentDataConsumer,
                                              subItemsLogPrefix.c_str(),
                                              L"Tangent"))
            return false;

        mIsTangentPresent = true;
    }
    else
    {
        Log::Debug(L"%sTangents are not present", subItemsLogPrefix.c_str());
    }

    // Texture coordinates
    auto &texCoord0Accessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
                                                       false, "TEXCOORD_0", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((texCoord0Accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (texCoord0Accessor.type != TINYGLTF_TYPE_VEC2))
        {
            Log::Error(L"%sUnsupported TEXCOORD_0 data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (texCoord0Accessor.count != posAccessor.count)
        {
            Log::Error(L"%sTexture coords count (%d) is different from position count (%d)!",
                       subItemsLogPrefix.c_str(), texCoord0Accessor.count, posAccessor.count);
            return false;
        }

        auto TexCoord0DataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char *ptr)
        {
            auto texCoord0 = *reinterpret_cast<const XMFLOAT2*>(ptr);

            //Log::Debug(L"%s%d: texCoord0 [%.1f, %.1f]",
            //           dataConsumerLogPrefix.c_str(), itemIdx, texCoord0.x, texCoord0.y);

            mVertices[itemIdx].Tex = texCoord0;
        };

        if (!IterateGltfAccesorData<float, 2>(model,
                                              texCoord0Accessor,
                                              TexCoord0DataConsumer,
                                              subItemsLogPrefix.c_str(),
                                              L"Texture coordinates"))
            return false;
    }

    // DW CODE - load joints (bones)

    struct USHORT4 {
        uint16_t x, y, z, w;
    };

    // Joints / Bones
    auto& jointAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
        false, "JOINTS_0", subItemsLogPrefix.c_str());
    if (success)
    {
        // --- 1. Updated Check: Allow both USHORT and UBYTE ---
        if ((jointAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) &&
            (jointAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE))
        {
            Log::Error(L"%sUnsupported JOINT component type! Must be UNSIGNED_SHORT or UNSIGNED_BYTE.", subItemsLogPrefix.c_str());
            return false;
        }

        if (jointAccessor.type != TINYGLTF_TYPE_VEC4)
        {
            Log::Error(L"%sUnsupported JOINT type! Must be VEC4.", subItemsLogPrefix.c_str());
            return false;
        }

        if (jointAccessor.count != posAccessor.count)
        {
            Log::Error(L"%sJoint count (%d) is different from position count (%d)!",
                subItemsLogPrefix.c_str(), jointAccessor.count, posAccessor.count);
            return false;
        }

        // --- 2. Branch based on the component type ---
        if (jointAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
        {
            // --- Path for UNSIGNED_SHORT (your original logic) ---
            auto JointDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
                {
                    // USHORT is typically a typedef for unsigned short.
                    // This assumes USHORT4 is a struct of 4 unsigned shorts.
                    auto& raw_joints = *reinterpret_cast<const USHORT4*>(ptr);

                    mVertices[itemIdx].Joints = DirectX::XMUINT4(
                        raw_joints.x,
                        raw_joints.y,
                        raw_joints.z,
                        raw_joints.w
                    );
                };

            if (!IterateGltfAccesorData<unsigned short, 4>(model,
                jointAccessor,
                JointDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Joint"))
                return false;
        }
        else // The type must be UNSIGNED_BYTE due to our check above
        {
            // --- NEW Path for UNSIGNED_BYTE ---
            auto JointDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
                {
                    // Cast the pointer to an array of 4 bytes.
                    const uint8_t* joints = reinterpret_cast<const uint8_t*>(ptr);

                    // The values are loaded directly.
                    mVertices[itemIdx].Joints = DirectX::XMUINT4(
                        joints[0],
                        joints[1],
                        joints[2],
                        joints[3]
                    );
                };

            // Note the different template argument here for the iterator!
            if (!IterateGltfAccesorData<unsigned char, 4>(model,
                jointAccessor,
                JointDataConsumer,
                subItemsLogPrefix.c_str(),
                L"Joint"))
                return false;
        }
    }


    // Bone weights
    auto& weightsAccessor = GetPrimitiveAttrAccessor(success, model, attrs, primitiveIdx,
        false, "WEIGHTS_0", subItemsLogPrefix.c_str());
    if (success)
    {
        if ((weightsAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) ||
            (weightsAccessor.type != TINYGLTF_TYPE_VEC4))
        {
            Log::Error(L"%sUnsupported JOUNT data type!", subItemsLogPrefix.c_str());
            return false;
        }

        if (weightsAccessor.count != posAccessor.count)
        {
            Log::Error(L"%sJoint count (%d) is different from position count (%d)!",
                subItemsLogPrefix.c_str(), weightsAccessor.count, posAccessor.count);
            return false;
        }

        auto WeightDataConsumer = [this, &dataConsumerLogPrefix](int itemIdx, const unsigned char* ptr)
            {
                auto weights = *reinterpret_cast<const XMFLOAT4*>(ptr);

                //Log::Debug(L"%s%d: normal [%.4f, %.4f, %.4f]",
                //           dataConsumerLogPrefix.c_str(),
                //           itemIdx, normal.x, normal.y, normal.z);

                mVertices[itemIdx].Weights = weights;
            };

        if (!IterateGltfAccesorData<float, 4>(model,
            weightsAccessor,
            WeightDataConsumer,
            subItemsLogPrefix.c_str(),
            L"Weights"))
            return false;
    }

    // Indices

    const auto indicesAccessorIdx = primitive.indices;
    if (indicesAccessorIdx >= model.accessors.size())
    {
        Log::Error(L"%sInvalid indices accessor index (%d/%d)!",
                   subItemsLogPrefix.c_str(), indicesAccessorIdx, model.accessors.size());
        return false;
    }
    if (indicesAccessorIdx < 0)
    {
        Log::Error(L"%sNon-indexed geometry is not supported!", subItemsLogPrefix.c_str());
        return false;
    }

    const auto &indicesAccessor = model.accessors[indicesAccessorIdx];

    if (indicesAccessor.type != TINYGLTF_TYPE_SCALAR)
    {
        Log::Error(L"%sUnsupported indices data type (must be scalar)!", subItemsLogPrefix.c_str());
        return false;
    }
    if ((indicesAccessor.componentType < TINYGLTF_COMPONENT_TYPE_BYTE) ||
        (indicesAccessor.componentType > TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
    {
        Log::Error(L"%sUnsupported indices data component type (%d)!",
                   subItemsLogPrefix.c_str(), indicesAccessor.componentType);
        return false;
    }

    mIndices.clear();
    mIndices.reserve(indicesAccessor.count);
    if (mIndices.capacity() < indicesAccessor.count)
    {
        Log::Error(L"%sUnable to allocate %d indices!", subItemsLogPrefix.c_str(), indicesAccessor.count);
        return false;
    }

    const auto indicesComponentType = indicesAccessor.componentType;
    auto IndexDataConsumer =
        [this, &dataConsumerLogPrefix, indicesComponentType]
        (int itemIdx, const unsigned char *ptr)
    {
        switch (indicesComponentType)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:              mIndices.push_back(*reinterpret_cast<const int8_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:     mIndices.push_back(*reinterpret_cast<const uint8_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_SHORT:             mIndices.push_back(*reinterpret_cast<const int16_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:    mIndices.push_back(*reinterpret_cast<const uint16_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_INT:               mIndices.push_back(*reinterpret_cast<const int32_t*>(ptr)); break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:      mIndices.push_back(*reinterpret_cast<const uint32_t*>(ptr)); break;
        }

        // debug
        itemIdx; // unused param
        //Log::Debug(L"%s%d: %d",
        //           dataConsumerLogPrefix.c_str(),
        //           itemIdx,
        //           mIndices.back());
    };

    // TODO: Wrap into IterateGltfAccesorData(componentType, ...)? std::forward()?
    switch (indicesComponentType)
    {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
        IterateGltfAccesorData<const int8_t, 1>(model,
                                                indicesAccessor,
                                                IndexDataConsumer,
                                                subItemsLogPrefix.c_str(),
                                                L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        IterateGltfAccesorData<uint8_t, 1>(model,
                                           indicesAccessor,
                                           IndexDataConsumer,
                                           subItemsLogPrefix.c_str(),
                                           L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
        IterateGltfAccesorData<int16_t, 1>(model,
                                           indicesAccessor,
                                           IndexDataConsumer,
                                           subItemsLogPrefix.c_str(),
                                           L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        IterateGltfAccesorData<uint16_t, 1>(model,
                                            indicesAccessor,
                                            IndexDataConsumer,
                                            subItemsLogPrefix.c_str(),
                                            L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_INT:
        IterateGltfAccesorData<int32_t, 1>(model,
                                           indicesAccessor,
                                           IndexDataConsumer,
                                           subItemsLogPrefix.c_str(),
                                           L"Indices");
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        IterateGltfAccesorData<uint32_t, 1>(model,
                                            indicesAccessor,
                                            IndexDataConsumer,
                                            subItemsLogPrefix.c_str(),
                                            L"Indices");
        break;
    }
    if (mIndices.size() != indicesAccessor.count)
    {
        Log::Error(L"%sFailed to load indices (loaded %d instead of %d))!",
                   subItemsLogPrefix.c_str(), mIndices.size(), indicesAccessor.count);
        return false;
    }

    // DX primitive topology
    mTopology = GltfUtils::ModeToTopology(primitive.mode);
    if (mTopology == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED)
    {
        Log::Error(L"%sUnsupported primitive topology!", subItemsLogPrefix.c_str());
        return false;
    }

    // Material
    const auto matIdx = primitive.material;
    if (matIdx >= 0)
    {
        if (matIdx >= model.materials.size())
        {
            Log::Error(L"%sInvalid material index (%d/%d)!",
                       subItemsLogPrefix.c_str(), matIdx, model.materials.size());
            return false;
        }

        mMaterialIdx = matIdx;
    }

    CalculateTangentsIfNeeded(subItemsLogPrefix);

    return true;
}


bool ScenePrimitive::CalculateTangentsIfNeeded(const std::wstring &logPrefix)
{
    if (!IsTangentPresent())
    {
        Log::Debug(L"%sComputing tangents...", logPrefix.c_str());

        if (!TangentCalculator::Calculate(*this))
        {
            Log::Error(L"%sTangents computation failed!", logPrefix.c_str());
            return false;
        }
        mIsTangentPresent = true;
    }

    return true;
}

size_t ScenePrimitive::GetVerticesPerFace() const
{
    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
        return 1;
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
        return 2;
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return 2;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        return 3;
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return 3;
    default:
        return 0;
    }
}


size_t ScenePrimitive::GetFacesCount() const
{
    FillFaceStripsCacheIfNeeded();

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
        return mIndices.size() / GetVerticesPerFace();

    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
        return mFaceStripsTotalCount;

    default:
        return 0; // Unsupported
    }
}


void ScenePrimitive::FillFaceStripsCacheIfNeeded() const
{
    if (mAreFaceStripsCached)
        return;

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    {
        mFaceStrips.clear();

        const auto count = mIndices.size();
        for (size_t i = 0; i < count; )
        {
            // Start
            while ((i < count) && (mIndices[i] == STRIP_BREAK))
            {
                ++i;
            }
            const size_t start = i;

            // Length
            size_t length = 0;
            while ((i < count) && (mIndices[i] != STRIP_BREAK))
            {
                ++length;
                ++i;
            }

            // Strip
            if (length >= GetVerticesPerFace())
            {
                const auto faceCount = length - (GetVerticesPerFace() - 1);
                mFaceStrips.push_back({ start, faceCount });
            }
        }

        mFaceStripsTotalCount = 0;
        for (const auto &strip : mFaceStrips)
            mFaceStripsTotalCount += strip.faceCount;

        mAreFaceStripsCached = true;
        return;
    }

    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    default:
        return;
    }
}


const size_t ScenePrimitive::GetVertexIndex(const int face, const int vertex) const
{
    if (vertex >= GetVerticesPerFace())
        return 0;

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
        return vertex;

    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    {
        const bool isOdd = (face % 2 == 1);

        if (isOdd && (vertex == 1))
            return 2;
        else if (isOdd && (vertex == 2))
            return 1;
        else
            return vertex;
    }

    default:
        return 0; // Unsupported
    }
}


const SceneVertex& ScenePrimitive::GetVertex(const int face, const int vertex) const
{
    static const SceneVertex invalidVert{};

    FillFaceStripsCacheIfNeeded();

    if (vertex >= GetVerticesPerFace())
        return invalidVert;

    switch (mTopology)
    {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    {
        const auto idx = face * GetVerticesPerFace() + vertex;
        if ((idx < 0) || (idx >= mIndices.size()))
            return invalidVert;
        return mVertices[mIndices[idx]];
    }

    case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
    case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    {
        if (!mAreFaceStripsCached)
            return invalidVert;
        if (face >= mFaceStripsTotalCount)
            return invalidVert;

        // Strip
        // (naive impl for now, could be done in log time using cumulative counts and binary search)
        size_t strip = 0;
        size_t skippedFaces = 0;
        for (; strip < mFaceStrips.size(); strip++)
        {
            const auto currentFaceCount = mFaceStrips[strip].faceCount;
            if (face < (skippedFaces + currentFaceCount))
                break; // found
            skippedFaces += currentFaceCount;
        }
        if (strip >= mFaceStrips.size())
            return invalidVert;

        // Face & vertex
        const auto faceIdx   = face - skippedFaces;
        const auto vertexIdx = GetVertexIndex((int)faceIdx, vertex);
        const auto idx = mFaceStrips[strip].startIdx + faceIdx + vertexIdx;
        if ((idx < 0) || (idx >= mIndices.size()))
            return invalidVert;
        return mVertices[mIndices[idx]];
    }

    default:
        return invalidVert; // Unsupported
    }
}


SceneVertex& ScenePrimitive::GetVertex(const int face, const int vertex)
{
    return
        const_cast<SceneVertex &>(
            static_cast<const ScenePrimitive&>(*this).
                GetVertex(face, vertex));
}


void ScenePrimitive::GetPosition(float outpos[],
                                 const int face,
                                 const int vertex) const
{
    const auto &pos = GetVertex(face, vertex).Pos;
    outpos[0] = pos.x;
    outpos[1] = pos.y;
    outpos[2] = pos.z;
}


void ScenePrimitive::GetNormal(float outnormal[],
                               const int face,
                               const int vertex) const
{
    const auto &normal = GetVertex(face, vertex).Normal;
    outnormal[0] = normal.x;
    outnormal[1] = normal.y;
    outnormal[2] = normal.z;
}


void ScenePrimitive::GetTextCoord(float outuv[],
                                  const int face,
                                  const int vertex) const
{
    const auto &tex = GetVertex(face, vertex).Tex;
    outuv[0] = tex.x;
    outuv[1] = tex.y;
}


void ScenePrimitive::SetTangent(const float intangent[],
                                const float sign,
                                const int face,
                                const int vertex)
{
    auto &tangent = GetVertex(face, vertex).Tangent;
    tangent.x = intangent[0];
    tangent.y = intangent[1];
    tangent.z = intangent[2];
    tangent.w = sign;
}


bool ScenePrimitive::CreateDeviceBuffers(IRenderingContext & ctx)
{
    DestroyDeviceBuffers();

    auto device = ctx.GetDevice();
    if (!device)
        return false;

    HRESULT hr = S_OK;

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory(&initData, sizeof(initData));

    // Vertex buffer
    bd.Usage = D3D11_USAGE_DEFAULT;
    
    int x = sizeof(SceneVertex);
    bd.ByteWidth = (UINT)(sizeof(SceneVertex) * mVertices.size());


    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    initData.pSysMem = mVertices.data();
    hr = device->CreateBuffer(&bd, &initData, &mVertexBuffer);
    if (FAILED(hr))
    {
        DestroyDeviceBuffers();
        return false;
    }

    // Index buffer
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(uint32_t) * (UINT)mIndices.size();
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    initData.pSysMem = mIndices.data();
    hr = device->CreateBuffer(&bd, &initData, &mIndexBuffer);
    if (FAILED(hr))
    {
        DestroyDeviceBuffers();
        return false;
    }

    return true;
}


void ScenePrimitive::Destroy()
{
    DestroyGeomData();
    DestroyDeviceBuffers();
}


void ScenePrimitive::DestroyGeomData()
{
    mVertices.clear();
    mIndices.clear();
    mTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}


void ScenePrimitive::DestroyDeviceBuffers()
{
    Utils::ReleaseAndMakeNull(mVertexBuffer);
    Utils::ReleaseAndMakeNull(mIndexBuffer);
}


void ScenePrimitive::DrawGeometry(IRenderingContext &ctx, ID3D11InputLayout* vertexLayout) const
{
    auto immCtx = ctx.GetImmediateContext();

    immCtx->IASetInputLayout(vertexLayout);
    UINT stride = sizeof(SceneVertex);
    UINT offset = 0;
    immCtx->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);
    immCtx->IASetIndexBuffer(mIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    immCtx->IASetPrimitiveTopology(mTopology);

    immCtx->DrawIndexed((UINT)mIndices.size(), 0, 0);
}


SceneNode::SceneNode(bool isRootNode) :
    mIsRootNode(isRootNode)
{
    XMStoreFloat4x4(&mLocalMtrx, XMMatrixIdentity());
    XMStoreFloat4x4(&mWorldMtrx, XMMatrixIdentity());
}

SceneNode* SceneNode::CreateChildNode()
{
    // Add a new child node to this node's vector of children
    mChildren.emplace_back(false); // 'false' means this is not a root node
    // Return a pointer to the new child
    return &mChildren.back();
}


ScenePrimitive* SceneNode::CreateEmptyPrimitive()
{
    mPrimitives.clear();
    mPrimitives.resize(1);
    if (mPrimitives.size() != 1)
        return nullptr;

    return &mPrimitives[0];
}

void SceneNode::SetIdentity()
{
    // 1. Create an aligned, local identity matrix
    XMMATRIX identity = XMMatrixIdentity();

    // 2. Store that aligned matrix into your unaligned class member
    XMStoreFloat4x4(&mLocalMtrx, identity);
}

void SceneNode::AddScale(double scale)
{
    AddScale({ scale, scale, scale });
}

void SceneNode::AddScale(const std::vector<double>& vec)
{
    // --- Error Checking (No change needed) ---
    if (vec.size() != 3)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddScale: vector of incorrect size (%d instead of 3)",
                vec.size());
        return;
    }

    // --- Load ---
    // 1. Load the unaligned member variable into an aligned local XMMATRIX.
    XMMATRIX localMtrx = XMLoadFloat4x4(&mLocalMtrx);

    // --- Compute ---
    // 2. Create the new scaling matrix (this is also an aligned local).
    XMMATRIX scaleMtrx = XMMatrixScaling((float)vec[0], (float)vec[1], (float)vec[2]);

    // 3. Perform the multiplication using only aligned local variables.
    localMtrx = XMMatrixMultiply(localMtrx, scaleMtrx);

    // --- Store ---
    // 4. Store the aligned result back into the unaligned member variable.
    XMStoreFloat4x4(&mLocalMtrx, localMtrx);
}

void SceneNode::AddMatrix(const XMMATRIX& matrix)
{   
    // 1. Load
    XMMATRIX local = XMLoadFloat4x4(&mLocalMtrx);

    // 2. Compute
    local = XMMatrixMultiply(local, matrix);

    // 3. Store
    XMStoreFloat4x4(&mLocalMtrx, local);
}

void SceneNode::SetMatrix(FXMMATRIX matrix)
{
    XMStoreFloat4x4(&mLocalMtrx, matrix);
}


void SceneNode::AddRotationQuaternion(const std::vector<double>& vec)
{
    // --- Error Checking (No change needed) ---
    if (vec.size() != 4)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddRotationQuaternion: vector of incorrect size (%d instead of 4)",
                vec.size());
        return;
    }

    // --- Load ---
    // 1. Load the unaligned member variable into an aligned local XMMATRIX.
    XMMATRIX localMtrx = XMLoadFloat4x4(&mLocalMtrx);

    // --- Compute ---
    // 2. Load and normalize the quaternion (using aligned XMVECTOR).
    // Note: XMVectorSet is often faster than creating an XMFLOAT4 and loading it.
    XMVECTOR xmQuaternion = XMVectorSet((float)vec[0], (float)vec[1], (float)vec[2], (float)vec[3]);
    xmQuaternion = XMQuaternionNormalize(xmQuaternion);

    // 3. Create the rotation matrix from the normalized quaternion.
    XMMATRIX rotationMtrx = XMMatrixRotationQuaternion(xmQuaternion);

    // 4. Multiply the matrices (using only aligned local variables).
    localMtrx = XMMatrixMultiply(localMtrx, rotationMtrx);

    // --- Store ---
    // 5. Store the aligned result back into the unaligned member variable.
    XMStoreFloat4x4(&mLocalMtrx, localMtrx);
}

void SceneNode::AddTranslation(const std::vector<double>& vec)
{
    // --- Error Checking ---
    if (vec.size() != 3)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddTranslation: vector of incorrect size (%d instead of 3)",
                vec.size());
        return;
    }

    // --- Load ---
    // 1. Load the unaligned member variable into an aligned local XMMATRIX.
    XMMATRIX localMtrx = XMLoadFloat4x4(&mLocalMtrx);

    // --- Compute ---
    // 2. Create the new translation matrix (this is also an aligned local).
    XMMATRIX translationMtrx = XMMatrixTranslation((float)vec[0], (float)vec[1], (float)vec[2]);

    // 3. Perform the multiplication using only aligned local variables.
    localMtrx = XMMatrixMultiply(localMtrx, translationMtrx);

    // --- Store ---
    // 4. Store the aligned result back into the unaligned member variable.
    XMStoreFloat4x4(&mLocalMtrx, localMtrx);
}

void SceneNode::AddMatrix(const std::vector<double>& vec)
{
    
    if (vec.size() != 16)
    {
        if (vec.size() != 0)
            Log::Warning(L"SceneNode::AddMatrix: vector of incorrect size (%d instead of 16)",
                vec.size());
        return;
    }

    // --- Compute (Part 1: Create New Matrix) ---
    // 1. Create the new matrix from the vector.
    //    (This is already an aligned, local XMMATRIX, so it's safe).
    const XMMATRIX newMtrx = XMMatrixSet(
        (float)vec[0], (float)vec[1], (float)vec[2], (float)vec[3],
        (float)vec[4], (float)vec[5], (float)vec[6], (float)vec[7],
        (float)vec[8], (float)vec[9], (float)vec[10], (float)vec[11],
        (float)vec[12], (float)vec[13], (float)vec[14], (float)vec[15]);

    // --- Load ---
    // 2. Load the unaligned member variable into an aligned local XMMATRIX.
    XMMATRIX localMtrx = XMLoadFloat4x4(&mLocalMtrx);

    // --- Compute (Part 2: Combine) ---
    // 3. Perform the multiplication using only aligned local variables.
    localMtrx = XMMatrixMultiply(localMtrx, newMtrx);

    // --- Store ---
    // 4. Store the aligned result back into the unaligned member variable.
    XMStoreFloat4x4(&mLocalMtrx, localMtrx);
}

bool SceneNode::LoadCube(IRenderingContext& ctx)
{
    ScenePrimitive cube;
    bool ok = cube.CreateCube(ctx);

    mPrimitives.push_back(std::move(cube));

    return ok;
}

bool SceneNode::LoadSphere(IRenderingContext& ctx)
{
    ScenePrimitive sphere;
    bool ok = sphere.CreateSphere(ctx);

    mPrimitives.push_back(std::move(sphere));

    return ok;
}

bool SceneNode::LoadFromGLTF(IRenderingContext & ctx,
                             const tinygltf::Model &model,
                             const tinygltf::Node &node,
                             int nodeIdx,
                             const std::wstring &logPrefix)
{
    OutputDebugStringA(node.name.c_str());
    // debug
    if (Log::sLoggingLevel >= Log::eDebug)
    {
        std::wstring transforms;
        if (!node.rotation.empty())
            transforms += L"rotation ";
        if (!node.scale.empty())
            transforms += L"scale ";
        if (!node.translation.empty())
            transforms += L"translation ";
        if (!node.matrix.empty())
            transforms += L"matrix ";
        if (transforms.empty())
            transforms = L"none";
        Log::Debug(L"%sNode %d/%d \"%s\": mesh %d, transform %s, children %d",
                   logPrefix.c_str(), 
                   nodeIdx,
                   model.nodes.size(),
                   Utils::StringToWstring(node.name).c_str(),
                   node.mesh,
                   transforms.c_str(),
                   node.children.size());
    }

    const std::wstring &subItemsLogPrefix = logPrefix + L"   ";

    // Local transformation
    SetIdentity();
    if (node.matrix.size() == 16)
    {
        AddMatrix(node.matrix);

        // Sanity checking
        if (!node.scale.empty())
            Log::Warning(L"%sNode %d/%d \"%s\": node.scale is not empty when tranformation matrix is provided. Ignoring.",
                         logPrefix.c_str(),
                         nodeIdx,
                         model.nodes.size(),
                         Utils::StringToWstring(node.name).c_str());
        if (!node.rotation.empty())
            Log::Warning(L"%sNode %d/%d \"%s\": node.rotation is not empty when tranformation matrix is provided. Ignoring.",
                         logPrefix.c_str(),
                         nodeIdx,
                         model.nodes.size(),
                         Utils::StringToWstring(node.name).c_str());
        if (!node.translation.empty())
            Log::Warning(L"%sNode %d/%d \"%s\": node.translation is not empty when tranformation matrix is provided. Ignoring.",
                         logPrefix.c_str(),
                         nodeIdx,
                         model.nodes.size(),
                         Utils::StringToWstring(node.name).c_str());
    }
    else
    {
        AddScale(node.scale);
        AddRotationQuaternion(node.rotation);
        AddTranslation(node.translation);
    }

    // Mesh
    const auto meshIdx = node.mesh;
    if (meshIdx >= (int)model.meshes.size())
    {
        Log::Error(L"%sInvalid mesh index (%d/%d)!", subItemsLogPrefix.c_str(), meshIdx, model.meshes.size());
        return false;
    }
    if (meshIdx >= 0)
    {
        const auto &mesh = model.meshes[meshIdx];

        Log::Debug(L"%sMesh %d/%d \"%s\": %d primitive(s)",
                   subItemsLogPrefix.c_str(),
                   meshIdx,
                   model.meshes.size(),
                   Utils::StringToWstring(mesh.name).c_str(),
                   mesh.primitives.size());

        // Primitives
        const auto primitivesCount = mesh.primitives.size();
        mPrimitives.reserve(primitivesCount);
        for (size_t i = 0; i < primitivesCount; ++i)
        {
            ScenePrimitive primitive;
            if (!primitive.LoadFromGLTF(ctx, model, mesh, (int)i, subItemsLogPrefix + L"   "))
                return false;
            mPrimitives.push_back(std::move(primitive));
        }
    }

    return true;
}


void SceneNode::Animate(IRenderingContext &ctx)
{
    //if (mIsRootNode)
    //{
    //    const float time = ctx.GetFrameAnimationTime();
    //    const float period = 15.f; //seconds
    //    const float totalAnimPos = time / period;
    //    const float angle = totalAnimPos * XM_2PI;

    //    const XMMATRIX rotMtrx = XMMatrixRotationY(angle);

    //    mWorldMtrx = mLocalMtrx * rotMtrx;
    //}
    //else
   //     mWorldMtrx = mLocalMtrx;

    mWorldMtrx = mLocalMtrx;

    for (auto &child : mChildren)
        child.Animate(ctx);
}
