#pragma once
#include "editor/Editor.h"
#include "Map.h"
#include "Timeline.h"
namespace lab {
enum class Mode { Editor, Running, Paused };
class SimulationState {
public:
    PhysicsWorld world;
    Editor editor;
    Mode mode{Mode::Editor};
    MapType mapType{MapType::Void};
    float timeScale{1};
    double accumulator{}, stepsPerSecond{};
    bool lagging{};
    float timelineDuration{10};
    double timelineCursor{};
    std::string timelineMessage;
    bool calculate(bool preview=true);
    void invalidateTimeline();
    void setOptimizations(const Optimizations& options);
    void setExplosionPower(float power);
    void pauseCalculation(bool paused) {if(timeline_) timeline_->pauseCalculation(paused);}
    bool hasTimeline() const {return bool(timeline_);}
    bool baking() const {return timeline_&&timeline_->baking();}
    double calculatedSeconds() const {return timeline_?timeline_->calculatedSeconds():0;}
    bool timelineFailed() const {return timeline_&&!timeline_->error().empty();}
    bool seek(double seconds);
    void stepFrame(int direction=1);
    bool start();
    void newMap(MapType type);
    void reset(bool returnToEditor);
    void advance(double realSeconds,bool limitFrameWork=true);
    const PhysicsWorld& editableWorld() const {return mode==Mode::Editor?world:initial_;}
private:
    PhysicsWorld initial_;
    std::shared_ptr<Timeline> timeline_;
    std::size_t shownFrame_{};
    double sampleTime_{};
    std::size_t sampleSteps_{};
};
}
