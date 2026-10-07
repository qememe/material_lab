#include "PhysicsWorld.h"
namespace lab {
namespace {
float specificEnthalpy(const Particle& p) {
    const auto& m=material(p.material);
    return m.heatCapacity*(p.temperature-20)+m.latentHeat*p.liquidFraction;
}
}
void PhysicsWorld::addHeat(ParticleId id,float energy) {
    if(energy==0) return;
    auto& p=particles[id];if(!p.active||p.material==MaterialType::Bedrock||!std::isfinite(energy)) return;
    const auto& m=material(p.material);
    float h=std::max(-m.heatCapacity*273.f,specificEnthalpy(p)+energy/p.mass);
    float solidLimit=m.heatCapacity*(m.meltingPoint-20);
    if(h<solidLimit) {p.temperature=20+h/m.heatCapacity;p.liquidFraction=0;}
    else if(h<=solidLimit+m.latentHeat) {p.temperature=m.meltingPoint;p.liquidFraction=(h-solidLimit)/m.latentHeat;}
    else {p.temperature=m.meltingPoint+(h-solidLimit-m.latentHeat)/m.heatCapacity;p.liquidFraction=1;}
    p.temperature=std::min(p.temperature,10000.f);
    if(p.liquidFraction>.05f&&p.untilContact) {p.untilContact=false;p.previousPosition=p.position;}
}
void PhysicsWorld::setTemperature(ParticleId id,float temperature) {
    auto& p=particles[id];if(p.material==MaterialType::Bedrock) return;
    const auto& m=material(p.material);temperature=std::clamp(temperature,-253.f,10000.f);
    float target=m.heatCapacity*(temperature-20)+(temperature>=m.meltingPoint?m.latentHeat:0);
    addHeat(id,(target-specificEnthalpy(p))*p.mass);
}
float PhysicsWorld::thermalEnergy() const {
    float e=0;for(const auto& p:particles) if(p.active&&p.material!=MaterialType::Bedrock) e+=p.mass*specificEnthalpy(p);return e;
}
void PhysicsWorld::exchangeHeat(ParticleId ia,ParticleId ib,float dt) {
    const auto& a=particles[ia];const auto& b=particles[ib];
    if(!a.active||!b.active||a.material==MaterialType::Bedrock||b.material==MaterialType::Bedrock) return;
    if(std::abs(a.temperature-b.temperature)<.0001f) return;
    const auto& ma=material(a.material);const auto& mb=material(b.material);
    float capacityA=a.mass*ma.heatCapacity,capacityB=b.mass*mb.heatCapacity;
    float reducedCapacity=capacityA*capacityB/(capacityA+capacityB);
    float conductivity=2*ma.conductivity*mb.conductivity/(ma.conductivity+mb.conductivity);
    float transfer=(a.temperature-b.temperature)*reducedCapacity*(1-std::exp(-conductivity*dt));
    addHeat(ia,-transfer);addHeat(ib,transfer);
}
void PhysicsWorld::updateThermal(float dt) {
    if(!config.thermalEnabled) return;
    for(const auto& b:bonds) if(!b.broken) exchangeHeat(b.a,b.b,dt);
    for(auto& p:particles) if(p.active&&p.material!=MaterialType::Bedrock) {
        if(p.temperature==config.ambientTemperature) continue;
        // Convection in air and radiation remain separate: vacuum has no air friction.
        float kelvin=std::max(1.f,p.temperature+273.15f);
        float radiation=.000000000004f*kelvin*kelvin*kelvin;
        float cooling=(config.airDrag*.3f+radiation)*dt*std::sqrt(.46f/p.radius);
        auto id=ParticleId(&p-particles.data());
        addHeat(id,(config.ambientTemperature-p.temperature)*p.mass*material(p.material).heatCapacity*(1-std::exp(-cooling)));
    }
    for(auto& b:bonds) if(!b.broken&&(particles[b.a].liquidFraction>.55f||particles[b.b].liquidFraction>.55f)) {b.broken=true;b.accumulatedDamage=1;}
}
void PhysicsWorld::refineParticles() {
    // Adaptive material sampling keeps the quiet bulk coarse. Molten surface
    // droplets can reach 1/1024 of the original diameter without a million
    // samples in every original cell. Each split conserves mass and momentum.
    if(!config.adaptiveDetail) return;
    std::size_t count=particles.size(),budget=32;
    for(ParticleId id=0;id<count&&budget>0&&particles.size()+3<=PhysicsConfig::maxParticles;++id) {
        const auto p=particles[id];
        bool liquid=p.liquidFraction>=.65f;
        bool fragment=p.accumulatedDamage>.7f&&p.stress>2&&p.refinementLevel<3;
        if(!p.active||p.pinned||p.untilContact||p.material==MaterialType::Bedrock||p.refinementLevel>=10||(!liquid&&!fragment)) continue;
        // Refine flowing spray, retaining larger samples in calm liquid pools.
        if(p.refinementLevel>=2&&length(p.velocity)<4) continue;
        if(p.refinementLevel>=2) {
            unsigned level=particleLevels_[id];float scale=std::ldexp(1.f,int(level));
            int x=int(std::floor(p.position.x*scale)),y=int(std::floor(p.position.y*scale)),neighbors=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                for(auto j=detailHead(x+dx,y+dy,level);j!=noParticle;j=detailNext_[j])
                    if(j!=id&&length(particles[j].position-p.position)<p.radius*3) ++neighbors;
            if(neighbors>=6) continue;
        }
        bool attached=false;for(std::size_t n=bondOffsets_[id];n<bondOffsets_[id+1];++n) if(!bonds[incidentBonds_[n]].broken) {attached=true;break;}
        if(attached) continue;
        float r=p.radius*.5f;
        for(int k=0;k<4;++k) {
            Particle child=p;child.mass=p.mass*.25f;child.radius=r;child.refinementLevel=p.refinementLevel+1;
            Vec2 offset{(k&1?1.f:-1.f)*r,(k&2?1.f:-1.f)*r};
            child.position=p.position+offset;child.previousPosition=p.previousPosition+offset;
            if(k==0) particles[id]=child;else particles.push_back(child);
        }
        --budget;
    }
}
}
