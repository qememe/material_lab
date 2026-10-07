#pragma once
#include "physics/PhysicsWorld.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>
#include <condition_variable>
namespace lab {
// Private, temporary native binary snapshots. Never used for imported files.
class Timeline {
public:
    static constexpr unsigned framesPerSecond=30, stepsPerFrame=8;
    static constexpr std::uint64_t maxBytes=1024ull*1024*1024;
    Timeline(const PhysicsWorld& launched,double duration);
    ~Timeline();
    Timeline(const Timeline&)=delete;
    Timeline& operator=(const Timeline&)=delete;
    void advance(bool limitWork=true);
    void restore(std::size_t frame,PhysicsWorld& destination);
    void pauseCalculation(bool paused);
    bool baking() const;
    double calculatedSeconds() const;
    std::size_t lastFrame() const;
    std::size_t targetFrame() const {return totalFrames_;}
    std::string error() const;
private:
    struct Frame {std::streamoff offset;};
    PhysicsWorld calculation_;
    std::vector<Particle> baseParticles_;
    std::vector<Bond> baseBonds_;
    std::filesystem::path path_;
    std::fstream file_;
    std::vector<Frame> frames_;
    std::size_t totalFrames_{};
    unsigned steps_{};
    std::uint64_t bytes_{};
    mutable std::mutex mutex_;
    std::jthread worker_;
    std::condition_variable_any resume_;
    bool paused_{};
    std::string error_;
    std::optional<PhysicsWorld> restored_;
    std::size_t restoredFrame_{std::size_t(-1)};
    void bake(bool limitWork,std::stop_token stop={});
    void record();
};
}
