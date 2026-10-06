#include "Animation.h"
#include <algorithm>

using namespace std;

// Helper to read a vector of data from a glTF accessor
template<typename T>
void ReadDataFromAccessor(const tinygltf::Model& model, int accessorIndex, std::vector<T>& outData)
{
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    outData.resize(accessor.count);
    const unsigned char* dataPtr = &buffer.data[bufferView.byteOffset + accessor.byteOffset];
    memcpy(outData.data(), dataPtr, accessor.count * sizeof(T));
}

bool Animation::LoadFromGltf(const tinygltf::Model& model, const std::map<int, int>& nodeToJointMap, const unsigned int animationIndex)
{
    if (model.animations.empty()) {
        return false;
    }

    if (animationIndex >= model.animations.size())
        return false;

    const tinygltf::Animation& anim = model.animations[animationIndex]; // Load the first animation
    m_name = anim.name;

    // Load Samplers
    m_samplers.resize(anim.samplers.size());
    for (size_t i = 0; i < anim.samplers.size(); ++i)
    {
        const tinygltf::AnimationSampler& gltfSampler = anim.samplers[i];
        AnimationSampler& sampler = m_samplers[i];

        if (gltfSampler.interpolation == "LINEAR") {
            sampler.interpolation = AnimationSampler::LINEAR;
        } // Add cases for STEP and CUBICSPLINE if needed

        // Read timestamps
        ReadDataFromAccessor(model, gltfSampler.input, sampler.timestamps);

        // Read keyframe values
        const tinygltf::Accessor& outputAccessor = model.accessors[gltfSampler.output];
        if (outputAccessor.type == TINYGLTF_TYPE_VEC3)
        {
            ReadDataFromAccessor(model, gltfSampler.output, sampler.vec3_values);
        }
        else if (outputAccessor.type == TINYGLTF_TYPE_VEC4)
        {
            ReadDataFromAccessor(model, gltfSampler.output, sampler.vec4_values);
        }
    }

    // Load Channels
    for (size_t i = 0; i < anim.channels.size(); ++i)
    {
        const tinygltf::AnimationChannel& gltfChannel = anim.channels[i];
        AnimationChannel channel;

        // Find which joint this channel targets using the map we built earlier
        auto it = nodeToJointMap.find(gltfChannel.target_node);
        if (it == nodeToJointMap.end()) {
            continue; // This channel animates a node that isn't a joint, so we skip it.
        }
        channel.jointIndex = it->second;
        channel.samplerIndex = gltfChannel.sampler;

        if (gltfChannel.target_path == "translation") {
            channel.path = AnimationChannel::TRANSLATION;
        }
        else if (gltfChannel.target_path == "rotation") {
            channel.path = AnimationChannel::ROTATION;
        }
        else if (gltfChannel.target_path == "scale") {
            channel.path = AnimationChannel::SCALE;
        }
        m_channels.push_back(channel);
    }

    return true;
}

float Animation::GetStartTime() const {
    // For simplicity, assuming start time is 0. A more robust implementation
    // would find the minimum timestamp across all samplers.
    return 0.0f;
}

float Animation::GetEndTime() const {
    float endTime = 0.0f;
    for (const auto& sampler : m_samplers) {
        if (!sampler.timestamps.empty()) {
            endTime = max(endTime, sampler.timestamps.back());
        }
    }
    return endTime;
}