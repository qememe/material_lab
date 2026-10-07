#include "PhysicsWorld.h"
#include <deque>
#include <numeric>
namespace lab {
void PhysicsWorld::explode(const std::vector<ParticleId>& charge,bool explosive) {
    Vec2 center{};float mass=0;
    for(auto id:charge) {center+=particles[id].position*particles[id].mass;mass+=particles[id].mass;}
    center=center/mass;
    // Consume the complete charge before applying a single blast to its surroundings.
    for(auto id:charge) {auto& p=particles[id];p.activated=true;p.active=false;damageBonds(id,2);}
    float power=std::isfinite(config.explosionPower)?std::clamp(config.explosionPower,PhysicsConfig::minExplosionPower,PhysicsConfig::maxExplosionPower):1.f;
    // Total material mass, rather than pixel count, determines charge size.
    float size=explosive?std::clamp(std::cbrt(mass/material(MaterialType::Explosive).density),.05f,2.f):1.f;
    float radius=explosive?PhysicsConfig::explosiveRadius*size:PhysicsConfig::fuseRadius;
    float strength=explosive?PhysicsConfig::explosiveImpulse*power*size:PhysicsConfig::fuseImpulse;
    blasts.push_back({center,radius,0,strength}); ++stats.detonations;
    std::vector<ParticleId> nearby;
    int r=int(std::ceil(radius)),x=int(std::floor(center.x)),y=int(std::floor(center.y));
    for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx)
        for(auto j=gridHead(x+dx,y+dy);j!=noParticle;j=gridNext_[j]) nearby.push_back(j);
    for(auto j:nearby) {auto& p=particles[j]; if(!p.active||p.material==MaterialType::Bedrock) continue;
        Vec2 r=p.position-center; float d=length(r); if(d>=radius||p.pinned) continue;
        if(p.untilContact) {p.untilContact=false;p.previousPosition=p.position;}
        float falloff=1-d/radius;
        // Refining a receiving cell must not multiply its blast velocity.
        p.velocity+=normalized(r)*(strength*falloff/std::sqrt(material(p.material).density));
        float speed=length(p.velocity);if(speed>PhysicsConfig::maxSpeed) p.velocity*=PhysicsConfig::maxSpeed/speed;
        if(config.thermalEnabled) addHeat(j,p.mass*falloff*(explosive?180.f*power:30.f));
        p.accumulatedDamage=std::min(1.f,p.accumulatedDamage+falloff*(explosive?.2f*power:.05f));
    }
    for(auto id2:nearby) for(std::size_t n=bondOffsets_[id2];n<bondOffsets_[id2+1];++n) {
        auto& b=bonds[incidentBonds_[n]];if(b.broken||b.a!=id2) continue;
        if(particles[b.a].material==MaterialType::Bedrock&&particles[b.b].material==MaterialType::Bedrock) continue;
        float d=length((particles[b.a].position+particles[b.b].position)*.5f-center);
        if(d<radius) { b.accumulatedDamage+=(1-d/radius)*(explosive?.45f*power:.1f); if(b.accumulatedDamage>=1) {b.accumulatedDamage=1;b.broken=true;} }
    }
}
void PhysicsWorld::processExplosions() {
    std::deque<ParticleId> queue;
    for(auto id:fuseQueue_) if(particles[id].active&&!particles[id].activated) queue.push_back(id);
    fuseQueue_.clear();
    if(queue.empty()) return;
    buildGrid();buildDetailGrid();
    // Snapshot charge connectivity before any fuse or blast destroys bonds.
    std::vector<ParticleId> parent(particles.size());std::iota(parent.begin(),parent.end(),0);
    auto root=[&](ParticleId id) {while(parent[id]!=id) {parent[id]=parent[parent[id]];id=parent[id];}return id;};
    auto join=[&](ParticleId a,ParticleId b) {a=root(a);b=root(b);if(a!=b) parent[std::max(a,b)]=std::min(a,b);};
    auto chargeCell=[&](ParticleId id) {const auto& p=particles[id];return p.active&&!p.activated&&p.material==MaterialType::Explosive;};
    for(const auto& b:bonds) if(!b.broken&&chargeCell(b.a)&&chargeCell(b.b)) join(b.a,b.b);
    // Also join touching lattice neighbours, including unbonded and refined cells.
    // Searching at each sample's scale avoids merging entire miniature constructions.
    for(ParticleId i=0;i<particles.size();++i) if(chargeCell(i)) {
        const auto& a=particles[i];unsigned ownLevel=particleLevels_[i];
        for(unsigned level=0;level<=ownLevel;++level) {
            if(!(detailLevels_&(1u<<level))) continue;
            float scale=std::ldexp(1.f,int(level));
            int x=int(std::floor(a.position.x*scale)),y=int(std::floor(a.position.y*scale));
            for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx)
                for(auto j=detailHead(x+dx,y+dy,level);j!=noParticle;j=detailNext_[j]) {
                    if((level==ownLevel&&j<=i)||!chargeCell(j)) continue;
                    float reach=(a.radius+particles[j].radius)*PhysicsConfig::explosiveSignalRadius/(2*PhysicsConfig::particleRadius);
                    if(length(a.position-particles[j].position)<=reach) join(i,j);
                }
        }
    }
    std::vector<std::vector<ParticleId>> charges(particles.size());
    for(ParticleId i=0;i<particles.size();++i) if(chargeCell(i)) charges[root(i)].push_back(i);
    while(!queue.empty()) {
        ParticleId id=queue.front(); queue.pop_front();
        if(!particles[id].active||particles[id].activated) continue;
        Vec2 center=particles[id].position;bool isExplosive=particles[id].material==MaterialType::Explosive;
        if(isExplosive) {explode(charges[root(id)],true);continue;}
        explode({id},false);
        // Only a discrete fuse signal initiates charges; heat and damage cannot do so.
        float signalRadius=PhysicsConfig::fuseRadius;
        int r=int(std::ceil(signalRadius)),x=int(std::floor(center.x)),y=int(std::floor(center.y));
        for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx)
            for(auto j=gridHead(x+dx,y+dy);j!=noParticle;j=gridNext_[j])
                if(chargeCell(j)&&length(particles[j].position-center)<=signalRadius) queue.push_back(j);
    }
}
}
