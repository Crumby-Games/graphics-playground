#pragma once

#include "iscene.hpp"
#include "constants.hpp"

// We are using an older version of DirectX headers which causes 
// "warning C4005: '...' : macro redefinition"
#pragma warning(push)
#pragma warning(disable: 4005)
#include <d3d11.h>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable: 4838)
#pragma warning(pop)

#include "tiny_gltf.h" // just the interfaces (no implementation)

#include <string>

#include <DirectXMath.h>
#include "Skeleton.h"

using namespace DirectX;

struct SceneVertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT4 Tangent; // w represents handedness of the tangent basis and is either 1 or -1
    XMFLOAT2 Tex;
    XMUINT4  Joints;
    XMFLOAT4 Weights;
};

struct SkinnedSceneVertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT4 Tangent; // w represents handedness of the tangent basis and is either 1 or -1
    XMFLOAT2 Tex;
    XMUINT4  Joints;
    XMFLOAT4 Weights;
};




class ScenePrimitive
{
public:

    ScenePrimitive();
    ScenePrimitive(const ScenePrimitive &);
    ScenePrimitive(ScenePrimitive &&);
    ~ScenePrimitive();

    ScenePrimitive& operator = (const ScenePrimitive&);
    ScenePrimitive& operator = (ScenePrimitive&&);

    bool CreateQuad(IRenderingContext & ctx);
    bool CreateCube(IRenderingContext & ctx);
    bool CreateOctahedron(IRenderingContext & ctx);
    bool CreateSphere(IRenderingContext & ctx,
                      const WORD vertSegmCount = 40,
                      const WORD stripCount = 80);

    bool LoadFromGLTF(IRenderingContext & ctx,
                      const tinygltf::Model &model,
                      const tinygltf::Mesh &mesh,
                      const int primitiveIdx,
                      const std::wstring &logPrefix);

    // Uses mikktspace tangent space calculator by Morten S. Mikkelsen.
    // Requires position, normal, and texture coordinates to be already loaded.
    bool CalculateTangentsIfNeeded(const std::wstring &logPrefix = std::wstring());

    size_t GetVerticesPerFace() const;
    size_t GetFacesCount() const;
    const size_t GetVertexIndex(const int face, const int vertex) const;
    const SceneVertex& GetVertex(const int face, const int vertex) const;
          SceneVertex& GetVertex(const int face, const int vertex);
    void GetPosition(float outpos[], const int face, const int vertex) const;
    void GetNormal(float outnormal[], const int face, const int vertex) const;
    void GetTextCoord(float outuv[], const int face, const int vertex) const;
    void SetTangent(const float tangent[], const float sign, const int face, const int vertex);

    bool IsTangentPresent() const { return mIsTangentPresent; }

    void DrawGeometry(IRenderingContext &ctx, ID3D11InputLayout *vertexLayout) const;

    void SetMaterialIdx(int idx) { mMaterialIdx = idx; };
    int GetMaterialIdx() const { return mMaterialIdx; };

    void Destroy();

private:

    bool GenerateQuadGeometry();
    bool GenerateCubeGeometry();
    bool GenerateOctahedronGeometry();
    bool GenerateSphereGeometry(const WORD vertSegmCount, const WORD stripCount);

    bool LoadDataFromGLTF(const tinygltf::Model &model,
                              const tinygltf::Mesh &mesh,
                              const int primitiveIdx,
                              const std::wstring &logPrefix);

    void FillFaceStripsCacheIfNeeded() const;
    bool CreateDeviceBuffers(IRenderingContext &ctx);

    void DestroyGeomData();
    void DestroyDeviceBuffers();

public:

    // Geometry data
    std::vector<SceneVertex>    mVertices;
    std::vector<uint32_t>       mIndices;
    D3D11_PRIMITIVE_TOPOLOGY    mTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bool                        mIsTangentPresent = false;

    // Cached geometry data
    struct FaceStrip
    {
        size_t startIdx;
        size_t faceCount;
    };
    mutable bool                    mAreFaceStripsCached = false;
    mutable std::vector<FaceStrip>  mFaceStrips;
    mutable size_t                  mFaceStripsTotalCount = 0;

    // Device geometry data
    ID3D11Buffer*               mVertexBuffer = nullptr;
    ID3D11Buffer*               mIndexBuffer = nullptr;

    // Material
    int                         mMaterialIdx = -1;
};


class SceneNode
{
public:
    SceneNode(bool useDebugAnimation = false);

    ScenePrimitive* CreateEmptyPrimitive();

    void SetIdentity();
    void AddScale(double scale);
    void AddScale(const std::vector<double> &vec);
    void AddRotationQuaternion(const std::vector<double> &vec);
    void AddMatrix(const XMMATRIX& matrix);
    void AddTranslation(const std::vector<double> &vec);
    void AddMatrix(const std::vector<double> &vec);
    //void SetMatrix(const XMMATRIX& matrix);
    void SetMatrix(FXMMATRIX matrix);

    bool LoadSphere(IRenderingContext& ctx);
    bool LoadCube(IRenderingContext& ctx);


    bool LoadFromGLTF(IRenderingContext & ctx,
                      const tinygltf::Model &model,
                      const tinygltf::Node &node,
                      int nodeIdx,
                      const std::wstring &logPrefix);

    void Animate(IRenderingContext &ctx);

    XMMATRIX GetWorldMtrx() const
    {
        // Load the unaligned member into an aligned local XMMATRIX
        return XMLoadFloat4x4(&mWorldMtrx);
    }
    Skeleton* GetSkeleton() {
        return &m_skeleton;
    }

    SceneNode* CreateChildNode();
    SceneNode* GetChildNode(const unsigned int i) { return &mChildren[i]; }

private:
    friend class SceneGraph;
    std::vector<ScenePrimitive> mPrimitives;
    std::vector<SceneNode>      mChildren;
    Skeleton                    m_skeleton;

private:
    bool        mIsRootNode;
    XMFLOAT4X4   mLocalMtrx; // <-- Changed from XMMATRIX
    XMFLOAT4X4   mWorldMtrx; // <-- Changed from XMMATRIX
};

class SceneGraph : public IScene
{
public:

    enum SceneId
    {
        eFirst, 
        eLast
    };

    SceneGraph(const SceneId sceneId);
    SceneGraph() { mSceneId = eFirst; }

    virtual ~SceneGraph();

    virtual bool Init(IRenderingContext &ctx) override;
    virtual void Destroy() override;
    virtual void RenderFrame(IRenderingContext &ctx, const float deltaTime) override;

    bool LoadSphere(IRenderingContext& ctx);
    bool LoadGLTF(IRenderingContext& ctx, const std::wstring& filePath);
    bool LoadGLTFWithSkeleton(IRenderingContext& ctx, const std::wstring& filePath);

    // Transformations
    void AddScaleToRoots(double scale);
    void AddScaleToRoots(const std::vector<double>& vec);
    
    // note - set overwrites it...
    void SetMatrixToRoots(const XMMATRIX& mat);
    // whereas add 'adds' it (so setting a translation each frame will continually move it). 
    void AddTranslationToRoots(const std::vector<double>& vec);
    void AddMatrixToRoots(const std::vector<double>& vec);
    void AddMatrixToRoots(const XMMATRIX& mat);


    void AnimateFrame(IRenderingContext& ctx);
    SceneNode* CreateRootNode();
    SceneNode* GetRootNode(unsigned int i) { return &mRootNodes[i]; }


private:

    
    void AddRotationQuaternionToRoots(const std::vector<double>& vec);// this was public, but has strange issues. The solution is to have an accumulating quaternion, then convert to a matrix (so q = aq * q) then convert to a matrix, apparently

    // Loads the scene specified via constructor
    bool Load(IRenderingContext &ctx);
    bool LoadExternal(IRenderingContext &ctx, const std::wstring &filePath);

    bool PostLoadSanityTest();
    bool NodeTangentSanityTest(const SceneNode &node);

    // glTF loader
    bool LoadSceneFromGltf(IRenderingContext& ctx,
        const tinygltf::Model& model,
        const std::wstring& logPrefix);

    bool LoadSceneFromGltfWithSkeleton(IRenderingContext &ctx,
                           const tinygltf::Model &model,
                           const std::wstring &logPrefix);

    bool LoadSceneNodeFromGLTF(IRenderingContext &ctx,
                               SceneNode &sceneNode,
                               const tinygltf::Model &model,
                               int nodeIdx,
                               const std::wstring &logPrefix);


    void RenderNode(IRenderingContext &ctx,
                    SceneNode &node,
                    const XMMATRIX &parentWorldMtrx,
                    const float deltaTime);

    

private:


    
    SceneId               mSceneId;

    // Geometry
    std::vector<SceneNode>      mRootNodes;

    // Shaders
    ID3D11VertexShader*         mVertexShader = nullptr;
    ID3D11InputLayout*          mVertexLayout = nullptr;

    ID3D11Buffer*               mCbScene = nullptr;
    ID3D11Buffer*               mCbFrame = nullptr;
    ID3D11Buffer*               mCbSceneNode = nullptr;
    ID3D11Buffer*               mCbScenePrimitive = nullptr;

    ID3D11SamplerState*         mSamplerLinear = nullptr;
};
