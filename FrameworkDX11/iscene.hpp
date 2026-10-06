#pragma once

#include "irenderingcontext.hpp"

class IScene
{
public:

    virtual ~IScene() {};

    virtual bool Init(IRenderingContext &ctx) = 0;
    virtual void Destroy() = 0;
    virtual void RenderFrame(IRenderingContext &ctx, const float deltaTime) = 0;
};
