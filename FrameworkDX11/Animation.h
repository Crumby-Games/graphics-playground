
#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "tiny_gltf.h" // For loading
#include <map>

// Represents a single animation curve (e.g., the translations for one bone).
struct AnimationSampler
{
    enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
    InterpolationType interpolation = LINEAR;
    std::vector<float> timestamps;
       // Have separate vectors for each possible data type
    std::vector<DirectX::XMFLOAT3> vec3_values;
    std::vector<DirectX::XMFLOAT4> vec4_values;
};

// Connects an animation sampler to a specific joint.
struct AnimationChannel
{
    enum PathType { TRANSLATION, ROTATION, SCALE };
    PathType path = TRANSLATION;
    int jointIndex;       // The index of the joint in our skeleton to animate.
    int samplerIndex;     // The index of the sampler to use for keyframe data.
};

// The main container for a single animation clip.
class Animation
{
public:
    Animation() = default;
    // Copy Constructor
    Animation(const Animation& other)
        : m_samplers(other.m_samplers), // This will call std::vector's copy constructor
        m_channels(other.m_channels), // This will call std::vector's copy constructor
        m_name(other.m_name)          // This will call std::string's copy constructor
    {
    }

    // Loads the first animation from the glTF model.
    bool LoadFromGltf(const tinygltf::Model& model, const std::map<int, int>& nodeToJointMap, const unsigned int animationIndex);

    float GetStartTime() const;
    float GetEndTime() const;

    // These are public for easy access from the animation update logic.
    std::vector<AnimationSampler> m_samplers;
    std::vector<AnimationChannel> m_channels;
    std::string m_name;
};