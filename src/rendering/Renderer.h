#pragma once
#include "Camera.h"
#include "app/SimulationState.h"
#include <raylib.h>
namespace lab {
struct DebugOptions {bool enabled{}, bonds{true}, velocities{}, normals{true}, stress{true}, grid{true}, temperature{}, particles{};};
class Renderer {
public:
    DebugOptions debug;
    void releaseSurface();
    void drawWorld(const SimulationState& state,const Camera& camera,Vec2 mouse) const;
    static Color cellColor(const Particle& p,bool stress);
private:
    struct SurfaceSample {float density{},red{},green{},blue{};};
    mutable std::vector<SurfaceSample> surfaceField_;
    mutable std::vector<Color> surfacePixels_;
    mutable std::vector<float> gaussianX_,gaussianY_;
    mutable Texture2D surfaceTexture_{};
    void drawLiquidSurface(const SimulationState& state,const Camera& camera) const;
};
}
