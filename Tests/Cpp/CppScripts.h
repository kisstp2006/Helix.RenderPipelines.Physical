#pragma once

#include <Helix/Engine/Script/GameBehaviour.h>
#include <Support/TestScene.h>

// The test program's C++ scripts (its game module RenderPipelinesPhysicalTests): the test scene made by a C++ script.
namespace helix::rpphysical::testing
{
    inline SceneObjects& CppScene()
    {
        static SceneObjects objects;
        return objects;
    }

    // Makes the test scene on Start (the C# script CsPhysicalScene makes the same one).
    HX_CLASS(Namespace = "PhysicalTests")
    class CppPhysicalScene : public engine::GameBehaviour
    {
    public:
        HX_FUNCTION() void Start() { BuildPhysicalScene(CppScene()); }
    };
}
