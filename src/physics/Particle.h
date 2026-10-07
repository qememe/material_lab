#pragma once
#include "Math.h"
#include "Material.h"
#include <cstddef>
namespace lab {
using ParticleId=std::uint32_t;
enum class FreezeMode { Free, Absolute, UntilContact };
struct Particle {
    Vec2 position{}, previousPosition{}, velocity{};
    float mass{1}, temperature{20}, accumulatedDamage{}, stress{}, strain{}, impactFlash{};
    float liquidFraction{}, radius{.46f};
    unsigned refinementLevel{};
    MaterialType material{MaterialType::Steel};
    bool pinned{}, active{true}, activated{};
    bool untilContact{}, worldCell{};
    FreezeMode freezeMode() const {return pinned?FreezeMode::Absolute:untilContact?FreezeMode::UntilContact:FreezeMode::Free;}
    void setFreeze(FreezeMode mode) {
        pinned=mode==FreezeMode::Absolute||material==MaterialType::Bedrock;
        untilContact=mode==FreezeMode::UntilContact&&!pinned;
        velocity={};previousPosition=position;
    }
    float inverseMass() const { return pinned||untilContact||!active||material==MaterialType::Bedrock?0.f:1.f/mass; }
};
struct Bond {
    ParticleId a{},b{};
    float restLength{1}, originalLength{1}, stiffness{}, tensileStrength{}, compressionStrength{}, shearStrength{};
    float accumulatedDamage{}, lambda{};
    bool broken{};
};
}
