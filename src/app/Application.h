#pragma once
#include "ui/UI.h"
#include <memory>
namespace lab {
class Application {
public:
    Application();
    ~Application();
    Application(const Application&)=delete;
    Application& operator=(const Application&)=delete;
    int run(int smokeFrames=0,const std::string& scene="",const std::string& screenshot="",bool uiTest=false,bool earthTest=false,bool thermalTest=false,int renderBenchmark=-1,bool workflowTest=false,bool optimizationTest=false);
private:
    SimulationState state_;
    Optimizations optimizations_;
    float explosionPower_{1.f};
    Camera camera_;
    Renderer renderer_;
    std::unique_ptr<UI> ui_;
    Vec2 previousMouse_{};
    Vec2 strokeMouse_{};
    double clearConfirmUntil_{};
    bool mainMenu_{true},hasMap_{},exitRequested_{};
    bool legacyUiTest_{};
    std::optional<PhysicsWorld> toyPreview_;
    void handleInput();
    void action(UIAction action);
    void fitScene();
};
}
