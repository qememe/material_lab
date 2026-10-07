#pragma once
#include "physics/PhysicsWorld.h"
#include <filesystem>
#include <fstream>
#include <memory>
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
    bool baking() const {return frames_.size()<=totalFrames_&&error.empty();}
    double calculatedSeconds() const {return double(frames_.size()-1)/framesPerSecond;}
    std::size_t lastFrame() const {return frames_.size()-1;}
    std::size_t targetFrame() const {return totalFrames_;}
    std::string error;
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
    void record();
};
}
