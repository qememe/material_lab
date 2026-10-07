#pragma once
#include "Particle.h"
#include "app/Optimizations.h"
#include <unordered_map>
#include <vector>
#include <utility>
namespace lab {
struct PhysicsConfig {
    static constexpr float fixedDt=1.f/240.f;
    static constexpr float particleRadius=.46f;
    static constexpr float maxSpeed=350.f;
    static constexpr std::size_t maxParticles=80000;
    static constexpr int solverIterations=6;
    static constexpr float maxTravel=.22f;
    // Tunable fictional impact and detonation coefficients (no physical units).
    static constexpr float impactEnergyScale=18.f, impactDamageRate=.15f;
    static constexpr float fuseTriggerSpeed=8.f, fuseRadius=2.4f, fuseImpulse=3.f;
    static constexpr float explosiveRadius=5.f, explosiveImpulse=12.f;
    static constexpr float explosiveSignalRadius=1.65f;
    static constexpr float minExplosionPower=.1f, maxExplosionPower=3.f;
    float explosionPower{1.f};
    float gravity{};
    float airDrag{}; // Environmental drag; zero in vacuum.
    float ambientTemperature{20};
    bool thermalEnabled{true}, adaptiveDetail{true};
    float sampleSpacing{1};
    Optimizations optimizations;
};
struct Contact { ParticleId a,b; Vec2 normal; float closingSpeed; };
struct Blast { Vec2 center; float radius, age{}, strength; };
struct WorldStats { std::size_t contacts{}, brokenBonds{}, detonations{}, substeps{}; };
class PhysicsWorld {
public:
    std::vector<Particle> particles;
    std::vector<Bond> bonds;
    std::vector<Contact> contacts;
    std::vector<Contact> debugContacts;
    std::vector<Blast> blasts;
    PhysicsConfig config;
    WorldStats stats;
    double time{};
    ParticleId addCell(Vec2 position,MaterialType type,bool pinned=false,float radius=PhysicsConfig::particleRadius);
    void rebuildBonds();
    void step(float dt=PhysicsConfig::fixedDt);
    std::vector<ParticleId> component(ParticleId seed) const;
    std::size_t liveBondCount() const;
    std::size_t activeParticleCount() const;
    float kineticEnergy() const;
    void launch(const std::vector<ParticleId>& ids,Vec2 velocity,Vec2 applicationPoint);
    void addHeat(ParticleId id,float energy);
    void setTemperature(ParticleId id,float temperature);
    float thermalEnergy() const;
private:
    static constexpr ParticleId noParticle=UINT32_MAX;
    struct GridBucket { std::uint64_t key{}; ParticleId head{noParticle}; std::uint32_t stamp{}; };
    struct DetailBucket {std::uint64_t key{};ParticleId head{noParticle};std::uint32_t stamp{};unsigned level{};};
    std::vector<DetailBucket> detailGrid_;
    std::vector<ParticleId> detailNext_;
    std::uint32_t detailStamp_{};
    unsigned detailLevels_{};
    std::vector<unsigned char> particleLevels_;
    std::vector<float> bondAlpha_;
    std::vector<GridBucket> grid_;
    std::vector<ParticleId> gridNext_;
    std::uint32_t gridStamp_{};
    std::vector<ParticleId> fuseQueue_;
    std::vector<std::size_t> incidentBonds_,bondOffsets_,bondCursor_;
    std::vector<ParticleId> collisionIsland_,islandSize_;
    std::vector<std::pair<ParticleId,ParticleId>> cachedEndpoints_,islandEndpoints_;
    std::vector<unsigned char> islandActive_,islandBroken_,sleepingTerrain_;
    unsigned awakeLevels_{};
    void prepareTerrainSleep();
    void prepareTopology();
    void buildIslands();
    void wakeOnContact(ParticleId a,ParticleId b);
    void buildGrid();
    void buildDetailGrid();
    unsigned detailLevel(const Particle& p) const;
    std::size_t detailSlot(std::uint64_t key,unsigned level) const;
    ParticleId detailHead(int x,int y,unsigned level) const;
    ParticleId gridHead(int x,int y) const;
    std::size_t gridSlot(std::uint64_t key) const;
    bool bonded(ParticleId a,ParticleId b) const;
    void prepareBonds(float dt);
    void solveBonds();
    void updateDeformation(float dt);
    void dampBonds(float dt);
    void collide(float dt,bool damage);
    void applyImpact(ParticleId a,ParticleId b,Vec2 normal,float speed);
    void damageBonds(ParticleId id,float amount);
    void processExplosions();
    void explode(const std::vector<ParticleId>& charge,bool explosive);
    void exchangeHeat(ParticleId a,ParticleId b,float dt);
    void updateThermal(float dt);
    void refineParticles();
};
}
