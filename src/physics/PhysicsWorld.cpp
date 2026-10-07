#include "PhysicsWorld.h"
#include <queue>
#include <stdexcept>
#include <numeric>
namespace lab {
ParticleId PhysicsWorld::addCell(Vec2 position,MaterialType type,bool pinned,float radius) {
    if(particles.size()>=PhysicsConfig::maxParticles) throw std::runtime_error("Достигнут предел частиц (80 000)");
    Particle p;
    p.position=p.previousPosition=position; p.material=type;p.radius=radius; p.mass=material(type).density*std::pow(radius/PhysicsConfig::particleRadius,2); p.pinned=pinned||type==MaterialType::Bedrock;
    particles.push_back(p); return ParticleId(particles.size()-1);
}
void PhysicsWorld::rebuildBonds() {
    bonds.clear();
    float spacing=std::min(config.sampleSpacing,.25f);
    std::unordered_map<std::uint64_t,ParticleId> cells;
    for(ParticleId i=0;i<particles.size();++i) if(particles[i].active)
        cells[gridKey(int(std::lround(particles[i].position.x/spacing)),int(std::lround(particles[i].position.y/spacing)))]=i;
    constexpr int dx[]{1,0,1,-1}, dy[]{0,1,1,1};
    for(ParticleId i=0;i<particles.size();++i) {
        auto& p=particles[i]; if(!p.active) continue;
        int x=int(std::lround(p.position.x/spacing)),y=int(std::lround(p.position.y/spacing));
        int stride=std::max(1,int(std::lround(p.radius/(PhysicsConfig::particleRadius*spacing))));
        for(int k=0;k<4;++k) {
            auto it=cells.find(gridKey(x+dx[k]*stride,y+dy[k]*stride)); if(it==cells.end()) continue;
            auto j=it->second; const auto& a=material(p.material); const auto& b=material(particles[j].material);
            if(std::abs(p.radius-particles[j].radius)>p.radius*.01f) continue;
            // Bedrock is a contact boundary, never a glue anchor for loose soil.
            if((p.material==MaterialType::Bedrock)!=(particles[j].material==MaterialType::Bedrock)) continue;
            Bond bond; bond.a=i; bond.b=j; bond.restLength=bond.originalLength=length(p.position-particles[j].position);
            // Harmonic stiffness and weaker interface strength retain soft layers.
            bond.stiffness=2*a.stiffness*b.stiffness/(a.stiffness+b.stiffness);
            bond.tensileStrength=std::min(a.tensileStrength,b.tensileStrength);
            bond.compressionStrength=std::min(a.compressiveStrength,b.compressiveStrength);
            bond.shearStrength=std::min(a.shearStrength,b.shearStrength);
            bonds.push_back(bond);
        }
    }
}
void PhysicsWorld::buildGrid() {
    std::size_t capacity=16;
    while(capacity<particles.size()*4) capacity*=2;
    if(grid_.size()!=capacity) {grid_.assign(capacity,{});gridStamp_=0;}
    if(++gridStamp_==0) {for(auto& bucket:grid_) bucket.stamp=0;gridStamp_=1;}
    gridNext_.resize(particles.size());
    for(ParticleId i=0;i<particles.size();++i) if(particles[i].active) {
        auto p=particles[i].position;auto key=gridKey(int(std::floor(p.x)),int(std::floor(p.y)));
        auto& bucket=grid_[gridSlot(key)];
        if(bucket.stamp!=gridStamp_) {bucket.key=key;bucket.stamp=gridStamp_;bucket.head=noParticle;}
        gridNext_[i]=bucket.head;bucket.head=i;
    }
}
void PhysicsWorld::buildDetailGrid() {
    std::size_t capacity=16;while(capacity<particles.size()*4) capacity*=2;
    detailLevels_=0;
    if(detailGrid_.size()!=capacity) {detailGrid_.assign(capacity,{});detailStamp_=0;}
    if(++detailStamp_==0) {for(auto& bucket:detailGrid_) bucket.stamp=0;detailStamp_=1;}
    detailNext_.resize(particles.size());
    for(ParticleId i=0;i<particles.size();++i) if(particles[i].active) {
        auto level=particleLevels_[i];float scale=std::ldexp(1.f,int(level));
        detailLevels_|=1u<<level;
        auto p=particles[i].position;auto key=gridKey(int(std::floor(p.x*scale)),int(std::floor(p.y*scale)));
        auto& bucket=detailGrid_[detailSlot(key,level)];
        if(bucket.stamp!=detailStamp_) {bucket.key=key;bucket.level=level;bucket.stamp=detailStamp_;bucket.head=noParticle;}
        detailNext_[i]=bucket.head;bucket.head=i;
    }
}
unsigned PhysicsWorld::detailLevel(const Particle& p) const {return unsigned(std::clamp(int(std::lround(std::log2(PhysicsConfig::particleRadius/p.radius))),0,12));}
std::size_t PhysicsWorld::detailSlot(std::uint64_t key,unsigned level) const {
    std::uint64_t h=key^(std::uint64_t(level)*0x9e3779b97f4a7c15ULL);h^=h>>30;h*=0xbf58476d1ce4e5b9ULL;h^=h>>27;h*=0x94d049bb133111ebULL;h^=h>>31;
    std::size_t slot=std::size_t(h)&(detailGrid_.size()-1);
    while(detailGrid_[slot].stamp==detailStamp_&&(detailGrid_[slot].key!=key||detailGrid_[slot].level!=level)) slot=(slot+1)&(detailGrid_.size()-1);
    return slot;
}
ParticleId PhysicsWorld::detailHead(int x,int y,unsigned level) const {
    auto& b=detailGrid_[detailSlot(gridKey(x,y),level)];return b.stamp==detailStamp_?b.head:noParticle;
}
std::size_t PhysicsWorld::gridSlot(std::uint64_t key) const {
    // Open-addressed hash with contiguous bucket and particle-link storage.
    std::uint64_t h=key;h^=h>>30;h*=0xbf58476d1ce4e5b9ULL;h^=h>>27;h*=0x94d049bb133111ebULL;h^=h>>31;
    std::size_t slot=std::size_t(h)&(grid_.size()-1);
    while(grid_[slot].stamp==gridStamp_&&grid_[slot].key!=key) slot=(slot+1)&(grid_.size()-1);
    return slot;
}
ParticleId PhysicsWorld::gridHead(int x,int y) const {
    if(grid_.empty()) return noParticle;
    auto& bucket=grid_[gridSlot(gridKey(x,y))];return bucket.stamp==gridStamp_?bucket.head:noParticle;
}
bool PhysicsWorld::bonded(ParticleId a,ParticleId b) const {
    for(std::size_t n=bondOffsets_[a];n<bondOffsets_[a+1];++n) {const auto& bond=bonds[incidentBonds_[n]];if(!bond.broken&&(bond.a==b||bond.b==b)) return true;}
    return false;
}
void PhysicsWorld::step(float dt) {
    if(dt<=0||!std::isfinite(dt)) return;
    // Exact quiet lattices need no constraint/contact solve. Scan public state
    // each tick so editing, heating, launch and new incoming objects wake them.
    bool quiet=true,allFixed=true;
    for(const auto& p:particles) if(p.active) {
        allFixed&=p.inverseMass()==0;
        float stride=std::max(1.f,std::round(p.radius/(PhysicsConfig::particleRadius*config.sampleSpacing)));
        float spacing=config.sampleSpacing*stride;
        if(length(p.velocity)>1e-6f||p.liquidFraction>0||std::abs(p.temperature-config.ambientTemperature)>.001f||std::abs(p.radius-PhysicsConfig::particleRadius*spacing)>1e-6f||length(p.position-p.previousPosition)>1e-6f) {quiet=false;break;}
        Vec2 lattice{std::round(p.position.x/spacing)*spacing,std::round(p.position.y/spacing)*spacing};
        if(length(lattice-p.position)>1e-6f) {quiet=false;break;}
    }
    quiet&=allFixed||config.gravity==0;
    if(quiet) for(const auto& b:bonds) if(b.broken||std::abs(length(particles[b.a].position-particles[b.b].position)-b.restLength)>1e-6f) {quiet=false;break;}
    if(quiet&&!particles.empty()) {
        contacts.clear();debugContacts.clear();stats.contacts=0;stats.substeps=1;stats.brokenBonds=0;
        for(auto& blast:blasts) blast.age+=dt;
        std::erase_if(blasts,[](const Blast& b){return b.age>.65f;});time+=dt;return;
    }
    float maxVelocity=0;
    for(auto& p:particles) if(p.active&&p.inverseMass()>0) {
        float speed=length(p.velocity);
        if(speed>PhysicsConfig::maxSpeed) p.velocity*=PhysicsConfig::maxSpeed/speed;
        maxVelocity=std::max(maxVelocity,length(p.velocity));
    }
    int substeps=std::clamp(int(std::ceil(maxVelocity*dt/PhysicsConfig::maxTravel)),1,16);
    float h=dt/substeps;
    stats.contacts=0; stats.substeps=std::size_t(substeps); contacts.clear();
    debugContacts.clear();
    particleLevels_.resize(particles.size());
    for(ParticleId i=0;i<particles.size();++i) particleLevels_[i]=static_cast<unsigned char>(detailLevel(particles[i]));
    bondOffsets_.assign(particles.size()+1,0);
    for(auto& b:bonds) {++bondOffsets_[b.a+1];++bondOffsets_[b.b+1];}
    for(std::size_t i=1;i<bondOffsets_.size();++i) bondOffsets_[i]+=bondOffsets_[i-1];
    bondCursor_=bondOffsets_;incidentBonds_.resize(bonds.size()*2);
    for(std::size_t i=0;i<bonds.size();++i) {incidentBonds_[bondCursor_[bonds[i].a]++]=i;incidentBonds_[bondCursor_[bonds[i].b]++]=i;}
    for(int sub=0;sub<substeps;++sub) {
        buildIslands();
        for(auto& b:bonds) b.lambda=0;
        for(auto& p:particles) if(p.active) {
            p.previousPosition=p.position;
            p.impactFlash=std::max(0.f,p.impactFlash-h*2);
            p.stress*=std::exp(-h*6);
            if(p.inverseMass()>0) {
                p.velocity.y+=config.gravity*h;
                float before=.5f*p.mass*dot(p.velocity,p.velocity);
                float drag=config.airDrag*(1+.02f*length(p.velocity))*std::sqrt(.46f/p.radius)/std::sqrt(p.mass/std::max(.0000001f,p.radius*p.radius));
                p.velocity*=std::exp(-drag*h);
                if(config.thermalEnabled) addHeat(ParticleId(&p-particles.data()),std::max(0.f,before-.5f*p.mass*dot(p.velocity,p.velocity))*.01f);
                p.position+=p.velocity*h;
            }
            else p.velocity={};
        }
        for(int iteration=0;iteration<PhysicsConfig::solverIterations;++iteration) {
            // Only the first contact pass transfers heat. Refresh compliance
            // once after that pass; it stays constant for the remaining solves.
            if(iteration<=1) prepareBonds(h);
            solveBonds(); buildDetailGrid(); collide(h,iteration==0);
        }
        updateDeformation(h);
        for(auto& p:particles) if(p.active&&p.inverseMass()>0) p.velocity=(p.position-p.previousPosition)/h;
        dampBonds(h);
        // Restitution and Coulomb-like tangential impulse after positional projection.
        for(const auto& c:contacts) {
            auto& a=particles[c.a]; auto& b=particles[c.b];
            if(!a.active||!b.active) continue;
            float wa=a.inverseMass(),wb=b.inverseMass(),w=wa+wb; if(w==0) continue;
            float before=.5f*(a.mass*dot(a.velocity,a.velocity)+b.mass*dot(b.velocity,b.velocity));
            float vn=dot(b.velocity-a.velocity,c.normal);
            float target=std::min(material(a.material).restitution,material(b.material).restitution)*c.closingSpeed;
            if(vn<target) { float j=(target-vn)/w; a.velocity-=c.normal*(j*wa); b.velocity+=c.normal*(j*wb); }
            Vec2 tangent{-c.normal.y,c.normal.x}; float vt=dot(b.velocity-a.velocity,tangent);
            float friction=std::sqrt(material(a.material).friction*material(b.material).friction);
            float support=std::abs(c.normal.y)*config.gravity*h;
            float jt=std::clamp(-vt/w,-friction*(c.closingSpeed+support)/w,friction*(c.closingSpeed+support)/w);
            a.velocity-=tangent*(jt*wa); b.velocity+=tangent*(jt*wb);
            float liquid=std::min(1.f,a.liquidFraction+b.liquidFraction);
            if(liquid>0) {
                float viscosity=.5f*(material(a.material).viscosity+material(b.material).viscosity);
                Vec2 impulse=(b.velocity-a.velocity)*((1-std::exp(-viscosity*liquid*h))/w);
                a.velocity+=impulse*wa;b.velocity-=impulse*wb;
            }
            float after=.5f*(a.mass*dot(a.velocity,a.velocity)+b.mass*dot(b.velocity,b.velocity));
            if(config.thermalEnabled) {float heat=std::max(0.f,before-after)*.01f;addHeat(c.a,heat*.5f);addHeat(c.b,heat*.5f);}
        }
        debugContacts.insert(debugContacts.end(),contacts.begin(),contacts.end());
        processExplosions(); contacts.clear();
        updateThermal(h);
    }
    for(auto& blast:blasts) blast.age+=dt;
    std::erase_if(blasts,[](const Blast& b){return b.age>.65f;});
    refineParticles();
    time+=dt; stats.brokenBonds=bonds.size()-liveBondCount();
}
std::vector<ParticleId> PhysicsWorld::component(ParticleId seed) const {
    if(seed>=particles.size()||!particles[seed].active) return {};
    std::vector<std::vector<ParticleId>> adjacency(particles.size());
    for(auto& b:bonds) if(!b.broken&&particles[b.a].active&&particles[b.b].active) { adjacency[b.a].push_back(b.b); adjacency[b.b].push_back(b.a); }
    std::vector<bool> seen(particles.size()); std::vector<ParticleId> result{seed}; seen[seed]=true;
    for(std::size_t n=0;n<result.size();++n) for(auto id:adjacency[result[n]]) if(!seen[id]) { seen[id]=true; result.push_back(id); }
    return result;
}
std::size_t PhysicsWorld::liveBondCount() const { return std::count_if(bonds.begin(),bonds.end(),[](const Bond& b){return !b.broken;}); }
std::size_t PhysicsWorld::activeParticleCount() const { return std::count_if(particles.begin(),particles.end(),[](const Particle& p){return p.active;}); }
float PhysicsWorld::kineticEnergy() const { float e=0; for(auto& p:particles) if(p.active) e+=.5f*p.mass*dot(p.velocity,p.velocity); return e; }
void PhysicsWorld::launch(const std::vector<ParticleId>& ids,Vec2 velocity,Vec2 point) {
    Vec2 center{}; float mass=0,inertia=0;
    for(auto id:ids) if(id<particles.size()) { center+=particles[id].position*particles[id].mass; mass+=particles[id].mass; }
    if(mass==0) return;
    center=center/mass;
    for(auto id:ids) if(id<particles.size()) inertia+=particles[id].mass*dot(particles[id].position-center,particles[id].position-center);
    // Offset impulse produces torque; magnitude is a translational speed in game units.
    float omega=inertia>1e-5f?cross(point-center,velocity*mass)/inertia:0;
    omega=std::clamp(omega,-30.f,30.f);
    for(auto id:ids) if(id<particles.size()) { auto& p=particles[id]; if(p.inverseMass()==0) continue; Vec2 r=p.position-center; p.velocity=velocity+Vec2{-r.y,r.x}*omega; }
}
void PhysicsWorld::buildIslands() {
    collisionIsland_.resize(particles.size());std::iota(collisionIsland_.begin(),collisionIsland_.end(),ParticleId(0));
    islandSize_.assign(particles.size(),1);
    auto root=[&](ParticleId id) {
        while(collisionIsland_[id]!=id) {collisionIsland_[id]=collisionIsland_[collisionIsland_[id]];id=collisionIsland_[id];}
        return id;
    };
    for(const auto& b:bonds) if(!b.broken&&particles[b.a].active&&particles[b.b].active) {
        auto a=root(b.a),c=root(b.b);if(a==c) continue;
        if(islandSize_[a]<islandSize_[c]) std::swap(a,c);
        collisionIsland_[c]=a;islandSize_[a]+=islandSize_[c];
    }
    for(ParticleId i=0;i<particles.size();++i) collisionIsland_[i]=root(i);
}
void PhysicsWorld::wakeOnContact(ParticleId a,ParticleId b) {
    if(collisionIsland_[a]==collisionIsland_[b]) return; // Internal contact cannot release one's own anchors.
    auto wake=[&](ParticleId id,ParticleId otherId) {
        auto& p=particles[id];const auto& other=particles[otherId];
        if(!p.untilContact||p.pinned||p.material==MaterialType::Bedrock) return;
        // A dormant landscape resting on its base is not an incoming object.
        if(p.worldCell&&(other.material==MaterialType::Bedrock||(other.worldCell&&other.untilContact))) return;
        p.untilContact=false;p.previousPosition=p.position;p.velocity={};
    };
    wake(a,b);wake(b,a);
}
}
