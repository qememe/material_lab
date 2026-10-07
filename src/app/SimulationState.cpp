#include "SimulationState.h"
#include <chrono>
namespace lab {
bool SimulationState::calculate(bool preview) {
    if(editor.drawing()||editableWorld().particles.empty()) return false;
    try {
        PhysicsWorld source=editableWorld();source.time=0;
        PhysicsWorld launched=source;launched.launch(editor.selection,editor.launchVelocity(),editor.launchPoint);
        auto cache=std::make_shared<Timeline>(launched,std::clamp(timelineDuration,.1f,120.f));
        initial_=std::move(source);timeline_=std::move(cache);
        timelineCursor=0;shownFrame_=0;accumulator=0;
        if(preview) {world=std::move(launched);mode=Mode::Running;}
        timelineMessage="Первый проход: расчёт кадров таймлайна.";
        return true;
    } catch(const std::exception& ex) {timelineMessage=ex.what();return false;}
}
void SimulationState::invalidateTimeline() {
    bool existed=hasTimeline();timeline_.reset();timelineCursor=0;
    if(existed&&mode==Mode::Editor) calculate(false);
}
bool SimulationState::seek(double seconds) {
    if(!timeline_||editor.drawing()) return false;
    auto frame=std::size_t(std::llround(std::clamp(seconds,0.,calculatedSeconds())*Timeline::framesPerSecond));
    frame=std::min(frame,timeline_->lastFrame());
    try {timeline_->restore(frame,world);shownFrame_=frame;timelineCursor=double(frame)/Timeline::framesPerSecond;mode=Mode::Paused;accumulator=0;return true;}
    catch(const std::exception& ex) {timelineMessage=ex.what();return false;}
}
void SimulationState::stepFrame(int direction) {seek(timelineCursor+double(direction)/Timeline::framesPerSecond);}
bool SimulationState::start() {
    if(mode!=Mode::Editor||world.particles.empty()||editor.drawing()) return false;
    timeline_.reset();initial_=world; world.launch(editor.selection,editor.launchVelocity(),editor.launchPoint);
    mode=Mode::Running;accumulator=0;sampleSteps_=0;sampleTime_=0;return true;
}
void SimulationState::newMap(MapType type) {
    *this=SimulationState{};mapType=type;createMap(world,type);
    if(type==MapType::Void) world.config.sampleSpacing=.25f;
}
void SimulationState::reset(bool toEditor) {
    if(mode==Mode::Editor) return;
    if(timeline_&&!toEditor) {seek(0);return;}
    world=initial_;accumulator=0;stepsPerSecond=0;sampleSteps_=0;sampleTime_=0;
    if(toEditor) mode=Mode::Editor;
    else {world.launch(editor.selection,editor.launchVelocity(),editor.launchPoint);mode=Mode::Paused;}
}
void SimulationState::advance(double realSeconds,bool limitFrameWork) {
    realSeconds=std::clamp(realSeconds,0.,.1);sampleTime_+=realSeconds;lagging=false;
    if(timeline_) {
        bool calculating=timeline_->baking();
        timeline_->advance(limitFrameWork);
        timelineMessage=!timeline_->error.empty()?timeline_->error:timeline_->baking()?"Первый проход: расчёт кадров таймлайна.":"Кадры рассчитаны. Перемотка и воспроизведение используют кэш.";
        if(mode==Mode::Running) {
            timelineCursor=calculating?calculatedSeconds():std::min(timelineCursor+realSeconds*timeScale,calculatedSeconds());
            auto frame=std::min(std::size_t(std::llround(timelineCursor*Timeline::framesPerSecond)),timeline_->lastFrame());
            if(frame!=shownFrame_) {
                try {timeline_->restore(frame,world);shownFrame_=frame;}
                catch(const std::exception& ex) {timelineMessage=ex.what();mode=Mode::Paused;}
            }
            if(!timeline_->baking()&&frame==timeline_->lastFrame()) mode=Mode::Paused;
        }
        return;
    }
    if(mode==Mode::Running) {
        accumulator+=realSeconds*timeScale;
        int budget=128;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(8);
        while(accumulator>=PhysicsConfig::fixedDt&&budget-->0) {
            world.step();accumulator-=PhysicsConfig::fixedDt;++sampleSteps_;
            if(limitFrameWork&&std::chrono::steady_clock::now()>=deadline) break;
        }
        lagging=accumulator>=PhysicsConfig::fixedDt;
        // Retain outstanding simulation time; never use a larger dt to catch up.
        accumulator=std::min(accumulator,1.0);
    }
    if(sampleTime_>=.5) {stepsPerSecond=sampleSteps_/sampleTime_;sampleTime_=0;sampleSteps_=0;}
}
}
