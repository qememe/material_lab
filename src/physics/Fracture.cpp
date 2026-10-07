#include "PhysicsWorld.h"
namespace lab {
void PhysicsWorld::damageBonds(ParticleId id,float amount) {
    // Local work removes load paths, not material cells: debris keeps mass and momentum.
    if(id+1>=bondOffsets_.size()) return;
    for(std::size_t n=bondOffsets_[id];n<bondOffsets_[id+1];++n) { auto& bond=bonds[incidentBonds_[n]]; if(bond.broken) continue;
        if(particles[bond.a].material==MaterialType::Bedrock&&particles[bond.b].material==MaterialType::Bedrock) continue;
        bond.accumulatedDamage+=amount;
        if(bond.accumulatedDamage>=1) {bond.accumulatedDamage=1;bond.broken=true;}
    }
}
}
