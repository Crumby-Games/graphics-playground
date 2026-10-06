#include "scenegraph.h"

#include "scene_utils.hpp"
#include "log.hpp"

bool SceneGraph::Load(IRenderingContext &ctx)
{
    switch (mSceneId)
    {
        
    }

    return PostLoadSanityTest();
}


bool SceneGraph::PostLoadSanityTest()
{

    // Geometry using normal map must have tangent specified (for now)
    for (auto &node : mRootNodes)
        if (!NodeTangentSanityTest(node))
            return false;

    return true;
}


bool SceneGraph::NodeTangentSanityTest(const SceneNode &node)
{
    // Test node
    for (auto &primitive : node.mPrimitives)
    {

    }

    // Children
    for (auto &child : node.mChildren)
        if (!NodeTangentSanityTest(child))
            return false;

    return true;
}
