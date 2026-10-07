#include "PhysicsWorld.h"
namespace lab {
void PhysicsWorld::collide(float dt,bool damage) {
    for(ParticleId i=0;i<particles.size();++i) {
        auto& a=particles[i]; if(!a.active) continue;
        unsigned ownLevel=particleLevels_[i];
        for(unsigned level=0;level<=ownLevel;++level) {
        if(!(detailLevels_&(1u<<level))) continue;
        // Fine resting samples must still find incoming coarse particles.
        // Same-size/coarser resting samples are found by the awake particle.
        if(sleepingTerrain_[i]&&(level==ownLevel||!(awakeLevels_&(1u<<level)))) continue;
        float scale=std::ldexp(1.f,int(level));
        int x=int(std::floor(a.position.x*scale)), y=int(std::floor(a.position.y*scale));
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            for(auto j=detailHead(x+dx,y+dy,level);j!=noParticle;j=detailNext_[j]) {
                if(level==ownLevel&&j<=i&&!sleepingTerrain_[j]) continue;
                if(sleepingTerrain_[i]&&sleepingTerrain_[j]) continue;
                auto& b=particles[j];
                float diameter=a.radius+b.radius;
                Vec2 delta=b.position-a.position; float d2=dot(delta,delta);
                if(d2>=diameter*diameter*1.25f) continue;
                if(bonded(i,j)) continue;
                if(damage&&config.thermalEnabled) exchangeHeat(i,j,dt);
                if(d2>=diameter*diameter) continue;
                wakeOnContact(i,j);
                float w=a.inverseMass()+b.inverseMass(); if(w==0) continue;
                float d=std::sqrt(d2); Vec2 n=d>1e-5f?delta/d:normalized(b.previousPosition-a.previousPosition);
                float closing=std::max(0.f,-dot(b.velocity-a.velocity,n));
                if(damage) {
                    applyImpact(i,j,n,closing); contacts.push_back({i,j,n,closing}); ++stats.contacts;
                }
                Vec2 correction=n*((diameter-d)*.85f/w);
                a.position-=correction*a.inverseMass(); b.position+=correction*b.inverseMass();
                // Overlap recovery is geometric stabilization, not an impulse.
                // Without this shift, reconstructing velocity divides penetration by
                // a tiny substep and launches newly separated debris explosively.
                a.previousPosition-=correction*a.inverseMass();
                b.previousPosition+=correction*b.inverseMass();
            }
        }
        }
    }
}
void PhysicsWorld::applyImpact(ParticleId ia,ParticleId ib,Vec2,float speed) {
    auto& a=particles[ia]; auto& b=particles[ib];
    float reducedMass=1.f/(1.f/a.mass+1.f/b.mass);
    float energy=.5f*reducedMass*speed*speed;
    const float resistanceA=material(a.material).impactResistance;
    const float resistanceB=material(b.material).impactResistance;
    for(auto id:{ia,ib}) {
        auto& p=particles[id]; const auto& m=material(p.material);
        if(p.material==MaterialType::Bedrock) continue;
        // Partition contact work by compliance: a soft layer absorbs most of the
        // local irreversible work rather than transmitting full energy into both sides.
        float share=(id==ia?resistanceB:resistanceA)/(resistanceA+resistanceB);
        float localEnergy=energy*share;
        float severity=localEnergy/(m.impactResistance*PhysicsConfig::impactEnergyScale);
        float damage=std::clamp(severity*PhysicsConfig::impactDamageRate,0.f,.8f);
        p.accumulatedDamage=std::min(1.f,p.accumulatedDamage+damage);
        p.impactFlash=std::max(p.impactFlash,std::min(1.f,speed/25));
        p.stress=std::max(p.stress,severity);
        // Mechanical bonds fail from their own resolved strain, not by damaging
        // every incident bond once for each touching neighbour/substep.
    }
    // Calm contact and contacts with EXPLOSIVE never trigger a fuse.
    constexpr float fuseTriggerSpeed=PhysicsConfig::fuseTriggerSpeed;
    if(speed>fuseTriggerSpeed) {
        if(a.material==MaterialType::Fuse&&b.material!=MaterialType::Explosive&&!a.activated) fuseQueue_.push_back(ia);
        if(b.material==MaterialType::Fuse&&a.material!=MaterialType::Explosive&&!b.activated) fuseQueue_.push_back(ib);
    }
}
}
