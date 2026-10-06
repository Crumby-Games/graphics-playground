#pragma once

#include <string>
#include <vector>
#include <DirectXMath.h>
#include "tiny_gltf.h"

#include "Animation.h"

struct Joint
{
    // The name of the joint, useful for debugging.
    std::string name = "unknown";

    // The transform of the bone in its parent's space at the time of binding.
    DirectX::XMFLOAT4X4  localBindTransform{};

    // The inverse of the bone's transform in model space at the time of binding.
    DirectX::XMFLOAT4X4  inverseBindMatrix{};

    // Indices of the children of this joint in the skeleton's main joint list.
    std::vector<int> children;

    // The final transform of the bone in model space for the current animation frame.
    // This is calculated at runtime by combining the local animated pose with the parent's final transform.
    DirectX::XMFLOAT4X4 finalTransform{};
};

class Skeleton
{
public:

    Skeleton();

    // Loads the skeleton hierarchy and matrices from a glTF model.
    // Returns true on success.
    bool LoadFromGltf(const tinygltf::Model& model);

    // Updates the pose of the skeleton based on the animation time.
    void Update(float deltaTime);

    // Returns the final skinning matrices ready to be sent to the GPU.
    const void GetSkinningMatrices(DirectX::XMMATRIX* matrixlist, unsigned int arraylength) const;
    const unsigned int GetBoneCount() { return m_skinningMatrices.size(); }
    const DirectX::XMMATRIX& GetRootTransform() const { return XMLoadFloat4x4(&m_rootTransform); }

    unsigned int GetAnimationCount() { return m_animationCount; }
    void PlayAnimation(const unsigned int animation);
    void PlayAnimation(Animation* anim);
    bool IsLoaded() { return m_isLoaded; }
    Animation* CurrentAnimation() { return m_pCurrentAnimation; }

    int AddJoint(int parentIndex, const DirectX::XMFLOAT4X4& localBindTransform);
    Joint* GetJoint(unsigned int joint) {
        return &m_joints[joint];
    }

private:
    // Change the signature to accept a pointer.
    void UpdateJointTransform(int jointIndex, const Animation* anim, float time, const DirectX::XMMATRIX& parentTransform);

    // The flat list of all joints that make up this skeleton.
    std::vector<Joint> m_joints;

    // A list of indices for the root joints (those without a parent in the skeleton).
    std::vector<int> m_rootJointIndices;

    // The final matrices sent to the shader, calculated by multiplying the
    // inverse bind matrix by the final animated transform for each joint.
    std::vector<DirectX::XMFLOAT4X4> m_skinningMatrices;

    DirectX::XMFLOAT4X4 m_rootTransform;

    unsigned int            m_animationCount;
    std::vector<Animation>  m_animations;
    Animation* m_pCurrentAnimation;
    float                   m_currentAnimationTime;
    bool                    m_isLoaded = false;
};


