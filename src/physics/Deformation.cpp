#include "PhysicsWorld.h"
namespace lab {
void PhysicsWorld::prepareBonds(float dt) {
    bondAlpha_.resize(bonds.size());
    for(std::size_t i=0;i<bonds.size();++i) {
        const auto& b=bonds[i];if(b.broken) continue;
        const auto& a=particles[b.a];const auto& c=particles[b.b];
        float heat=std::max((a.temperature-20)/std::max(1.f,material(a.material).meltingPoint-20),(c.temperature-20)/std::max(1.f,material(c.material).meltingPoint-20));
        float softness=std::clamp(1-.75f*heat-.9f*std::max(a.liquidFraction,c.liquidFraction),.02f,1.f);
        bondAlpha_[i]=1.f/(b.stiffness*softness*std::max(.08f,1-b.accumulatedDamage)*dt*dt);
    }
}
void PhysicsWorld::solveBonds() {
    for(std::size_t i=0;i<bonds.size();++i) {
        auto& b=bonds[i];
        if(b.broken) continue;
        auto& a=particles[b.a]; auto& c=particles[b.b];
        if(a.material==MaterialType::Bedrock&&c.material==MaterialType::Bedrock) continue;
        if(a.inverseMass()==0&&c.inverseMass()==0) continue;
        if(!a.active||!c.active) { b.broken=true; continue; }
        Vec2 delta=c.position-a.position; float distance=length(delta); if(distance<1e-6f) continue;
        float w=a.inverseMass()+c.inverseMass(); if(w==0) continue;
        float alpha=bondAlpha_[i];
        float dl=(-(distance-b.restLength)-alpha*b.lambda)/(w+alpha); b.lambda+=dl;
        Vec2 correction=delta*(dl/distance);
        a.position-=correction*a.inverseMass(); c.position+=correction*c.inverseMass();
    }
}
void PhysicsWorld::updateDeformation(float dt) {
    // Only the converged geometry can yield or fracture. Intermediate solver
    // guesses are not physical strain and must not irreversibly remove stiffness.
    for(auto& b:bonds) {
        if(b.broken) continue;
        auto& a=particles[b.a]; auto& c=particles[b.b];
        if(a.material==MaterialType::Bedrock&&c.material==MaterialType::Bedrock) continue;
        if(!a.active||!c.active) {b.broken=true;continue;}
        if(a.inverseMass()==0&&c.inverseMass()==0) continue;
        float distance=length(c.position-a.position);
        float strain=(distance-b.restLength)/b.restLength;
        a.strain=std::max(a.strain,std::abs(strain)); c.strain=std::max(c.strain,std::abs(strain));
        float strength=strain>0?b.tensileStrength:b.compressionStrength;
        strength*=std::clamp(1-.8f*std::max(a.liquidFraction,c.liquidFraction),.05f,1.f);
        // Diagonal constraints represent shear as well as axial stress.
        if(b.originalLength>(a.radius+c.radius)*(1.1f/.92f)) strength=std::min(strength,b.shearStrength);
        float load=std::abs(strain)/std::max(.01f,strength);
        a.stress=std::max(a.stress,load); c.stress=std::max(c.stress,load);
        float toughness=std::min(material(a.material).fractureToughness,material(c.material).fractureToughness);
        float materialDt=dt;
        if(load>1) b.accumulatedDamage+=(load-1)*materialDt*32/toughness;
        if(b.accumulatedDamage>=1||std::abs(strain)>strength*4) { b.accumulatedDamage=1; b.broken=true; continue; }
        float plasticity=.5f*(material(a.material).plasticity+material(c.material).plasticity);
        float yieldA=material(a.material).yieldStrain,yieldC=material(c.material).yieldStrain;
        if(yieldA>0&&yieldC>0) {
            float heat=std::max((a.temperature-20)/std::max(1.f,material(a.material).meltingPoint-20),(c.temperature-20)/std::max(1.f,material(c.material).meltingPoint-20));
            float permanent=std::abs(b.restLength/b.originalLength-1);
            float elasticLimit=std::min(yieldA,yieldC)*std::clamp(1-.8f*heat,.1f,1.f)*(1+std::min(permanent,.3f)*2);
            if(std::abs(strain)>elasticLimit) {
                // Return mapping: excess strain is permanent during this load,
                // rather than slowly creeping while a spring stores the impact.
                float next=distance/(1+std::copysign(elasticLimit,strain));
                next=std::clamp(next,b.originalLength*.5f,b.originalLength*1.8f);
                float increment=std::abs(next-b.restLength)/b.originalLength;
                b.restLength=next;
                b.accumulatedDamage+=increment/(std::max(.01f,strength)*8.f*toughness);
                if(b.accumulatedDamage>=1||std::abs(next/b.originalLength-1)>strength*3.f) {b.accumulatedDamage=1;b.broken=true;}
            }
        } else if(load>.55f) {
            float flow=std::clamp(plasticity*materialDt*18,0.f,.10f);
            b.restLength=std::clamp(b.restLength+(distance-b.restLength)*flow,b.originalLength*.5f,b.originalLength*1.8f);
        }
    }
}
void PhysicsWorld::dampBonds(float dt) {
    // Dampen axial vibrations, preserving translation, rigid rotation and pair
    // momentum. A global velocity multiplier would wrongly brake flying bodies.
    for(const auto& b:bonds) if(!b.broken) {
        auto& a=particles[b.a];auto& c=particles[b.b];
        if(!a.active||!c.active||material(a.material).yieldStrain<=0||material(c.material).yieldStrain<=0) continue;
        float wa=a.inverseMass(),wc=c.inverseMass(),w=wa+wc;if(w==0) continue;
        Vec2 n=normalized(c.position-a.position);float speed=dot(c.velocity-a.velocity,n);
        float damping=.5f*(material(a.material).damping+material(c.material).damping);
        float fraction=1-std::exp(-damping*.12f*std::sqrt(b.stiffness*w)*dt);
        float impulse=speed*fraction/w;
        a.velocity+=n*(impulse*wa);c.velocity-=n*(impulse*wc);
        if(config.thermalEnabled) {float lost=(speed*speed-std::pow(speed*(1-fraction),2))*.5f/w*.01f;addHeat(b.a,lost*.5f);addHeat(b.b,lost*.5f);}
    }
}
}
