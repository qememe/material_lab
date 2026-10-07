#include "PhysicsWorld.h"
#include <deque>
namespace lab {
void PhysicsWorld::explode(ParticleId id,bool explosive) {
    auto& source=particles[id]; if(source.activated||!source.active) return;
    source.activated=true; source.active=false; damageBonds(id,2);
    Vec2 center=source.position;
    float radius=explosive?PhysicsConfig::explosiveRadius:PhysicsConfig::fuseRadius;
    float strength=explosive?PhysicsConfig::explosiveImpulse:PhysicsConfig::fuseImpulse;
    blasts.push_back({center,radius,0,strength}); ++stats.detonations;
    std::vector<ParticleId> nearby;
    int r=int(std::ceil(radius)),x=int(std::floor(center.x)),y=int(std::floor(center.y));
    for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx)
        for(auto j=gridHead(x+dx,y+dy);j!=noParticle;j=gridNext_[j]) nearby.push_back(j);
    for(auto j:nearby) {auto& p=particles[j]; if(!p.active||p.material==MaterialType::Bedrock) continue;
        Vec2 r=p.position-center; float d=length(r); if(d>=radius||p.pinned) continue;
        if(p.untilContact) {p.untilContact=false;p.previousPosition=p.position;}
        float falloff=1-d/radius;
        p.velocity+=normalized(r)*(strength*falloff/std::sqrt(p.mass));
        float speed=length(p.velocity);if(speed>PhysicsConfig::maxSpeed) p.velocity*=PhysicsConfig::maxSpeed/speed;
        if(config.thermalEnabled) addHeat(j,p.mass*falloff*(explosive?1600.f:30.f));
        p.accumulatedDamage=std::min(1.f,p.accumulatedDamage+falloff*(explosive?.6f:.05f));
    }
    for(auto id2:nearby) for(std::size_t n=bondOffsets_[id2];n<bondOffsets_[id2+1];++n) {
        auto& b=bonds[incidentBonds_[n]];if(b.broken||b.a!=id2) continue;
        if(particles[b.a].material==MaterialType::Bedrock&&particles[b.b].material==MaterialType::Bedrock) continue;
        float d=length((particles[b.a].position+particles[b.b].position)*.5f-center);
        if(d<radius) { b.accumulatedDamage+=(1-d/radius)*(explosive?1.6f:.1f); if(b.accumulatedDamage>=1) {b.accumulatedDamage=1;b.broken=true;} }
    }
}
void PhysicsWorld::processExplosions() {
    std::deque<ParticleId> queue;
    for(auto id:fuseQueue_) if(particles[id].active&&!particles[id].activated) queue.push_back(id);
    fuseQueue_.clear();
    if(queue.empty()) return;
    buildGrid();
    while(!queue.empty()) {
        ParticleId id=queue.front(); queue.pop_front();
        if(!particles[id].active||particles[id].activated) continue;
        Vec2 center=particles[id].position; bool isExplosive=particles[id].material==MaterialType::Explosive;
        explode(id,isExplosive);
        // A discrete detonation signal propagates locally; ordinary damage cannot initiate it.
        float signalRadius=isExplosive?PhysicsConfig::explosiveSignalRadius:PhysicsConfig::fuseRadius;
        int r=int(std::ceil(signalRadius)),x=int(std::floor(center.x)),y=int(std::floor(center.y));
        for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx)
            for(auto j=gridHead(x+dx,y+dy);j!=noParticle;j=gridNext_[j])
                if(particles[j].active&&!particles[j].activated&&particles[j].material==MaterialType::Explosive&&length(particles[j].position-center)<=signalRadius) queue.push_back(j);
    }
}
}
