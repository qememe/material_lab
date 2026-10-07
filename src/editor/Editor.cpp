#include "Editor.h"
#include <numbers>
#include <string>
namespace lab {
bool Editor::insert(PhysicsWorld& w,const PhysicsWorld& toy,Vec2 at,std::string& message) {
    if(drawing_||toy.particles.empty()||w.particles.size()+toy.particles.size()>PhysicsConfig::maxParticles) {message="Недостаточно места для конструкции или не завершён мазок.";return false;}
    float spacing=.25f;
    for(auto& p:toy.particles) spacing=std::min(spacing,p.radius/PhysicsConfig::particleRadius);
    auto origin=toy.particles.front().position;
    at={std::round((at.x+origin.x)/spacing)*spacing-origin.x,std::round((at.y+origin.y)/spacing)*spacing-origin.y};
    std::unordered_map<std::uint64_t,std::vector<ParticleId>> occupancy;
    for(ParticleId i=0;i<w.particles.size();++i) if(w.particles[i].active) {auto p=w.particles[i].position;occupancy[gridKey(int(std::floor(p.x)),int(std::floor(p.y)))].push_back(i);}
    for(auto& p:toy.particles) {
        auto position=p.position+at;int x=int(std::floor(position.x)),y=int(std::floor(position.y));
        if(!finite(position)||std::abs(position.x)>=100000||std::abs(position.y)>=100000) {message="Конструкция выходит за пределы координат.";return false;}
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            auto it=occupancy.find(gridKey(x+dx,y+dy));if(it==occupancy.end()) continue;
            for(auto id:it->second) if(length(position-w.particles[id].position)<(p.radius+w.particles[id].radius)*.95f) {message="Место занято. Выберите свободный участок.";return false;}
        }
    }
    remember(w);selection.clear();auto offset=ParticleId(w.particles.size());
    for(auto p:toy.particles) {p.position+=at;p.previousPosition=p.position;p.velocity={};w.particles.push_back(p);if(p.material!=MaterialType::Bedrock) selection.push_back(ParticleId(w.particles.size()-1));}
    for(auto b:toy.bonds) {b.a+=offset;b.b+=offset;b.lambda=0;w.bonds.push_back(b);}
    launchPoint=at;message="Конструкция добавлена. Ctrl+Z: отменить.";return true;
}
void Editor::remember(PhysicsWorld& w) {
    undo_.push_back({w,selection,launchPoint}); if(undo_.size()>32) undo_.erase(undo_.begin()); redo_.clear();
}
void Editor::rebuildOccupancy(const PhysicsWorld& w) {
    occupied_.clear(); for(ParticleId i=0;i<w.particles.size();++i) if(w.particles[i].active) {
        Vec2 p=w.particles[i].position; occupied_[gridKey(int(std::lround(p.x/w.config.sampleSpacing)),int(std::lround(p.y/w.config.sampleSpacing)))]=i;
    }
}
std::optional<ParticleId> Editor::pick(const PhysicsWorld& w,Vec2 at) const {
    float best=.85f; std::optional<ParticleId> result;
    for(ParticleId i=0;i<w.particles.size();++i) if(w.particles[i].active) {
        float d=length(at-w.particles[i].position); if(d<best) {best=d;result=i;}
    }
    return result;
}
void Editor::beginStroke(PhysicsWorld& w,Vec2 at) {
    if(drawing_) return;
    if(tool==Tool::Select) {
        if(layer==EditorLayer::World) return;
        auto id=pick(w,at);
        if(!id) { selection.clear(); return; }
        if(w.particles[*id].material==MaterialType::Bedrock) {selection.clear();return;}
        selection=w.component(*id); launchPoint=at;
        drawing_=true; start_=last_=at;
        return;
    }
    beforeStroke_=Snapshot{w,selection,launchPoint};
    remember(w); drawing_=true; start_=last_=at;
    if(tool!=Tool::Anchor&&tool!=Tool::Heat&&tool!=Tool::Cool) selection.clear();
    rebuildOccupancy(w);
    if(tool!=Tool::Line) stamp(w,at,tool==Tool::Eraser);
}
void Editor::continueStroke(PhysicsWorld& w,Vec2 at) {
    if(!drawing_) return;
    if(tool==Tool::Brush||tool==Tool::Eraser||tool==Tool::Anchor||tool==Tool::Heat||tool==Tool::Cool) segment(w,last_,at,tool==Tool::Eraser);
    if(tool==Tool::Select&&length(at-start_)>.2f) launchAngle=std::atan2(at.y-start_.y,at.x-start_.x)*180/std::numbers::pi_v<float>;
    last_=at;
}
void Editor::endStroke(PhysicsWorld& w,Vec2 at) {
    if(!drawing_) return;
    continueStroke(w,at);
    if(tool==Tool::Line) segment(w,start_,at,false);
    if(tool==Tool::Brush||tool==Tool::Line||tool==Tool::Eraser) {
        std::erase_if(w.particles,[](const Particle& p){return !p.active;}); w.rebuildBonds();
    }
    drawing_=false; beforeStroke_.reset();
}
void Editor::cancelStroke(PhysicsWorld& w) {
    if(beforeStroke_) { w=std::move(beforeStroke_->world); selection=std::move(beforeStroke_->selection); launchPoint=beforeStroke_->point; if(!undo_.empty()) undo_.pop_back(); }
    beforeStroke_.reset(); drawing_=false;
}
void Editor::stamp(PhysicsWorld& w,Vec2 at,bool erase) {
    float spacing=w.config.sampleSpacing;
    int minX=int(std::ceil((at.x-radius)/spacing)),maxX=int(std::floor((at.x+radius)/spacing));
    int minY=int(std::ceil((at.y-radius)/spacing)),maxY=int(std::floor((at.y+radius)/spacing));
    for(int y=minY;y<=maxY;++y) for(int x=minX;x<=maxX;++x) {
        if(length(Vec2{x*spacing,y*spacing}-at)>radius) continue;
        auto key=gridKey(x,y); auto it=occupied_.find(key);
        if(tool==Tool::Heat||tool==Tool::Cool) {
            if(it!=occupied_.end()) w.setTemperature(it->second,tool==Tool::Heat?heaterTemperature:w.config.ambientTemperature);
            continue;
        }
        if(tool==Tool::Anchor) {
            if(it!=occupied_.end()) w.particles[it->second].setFreeze(anchorMode);
            continue;
        }
        if(erase) { if(it!=occupied_.end()) {w.particles[it->second].active=false; occupied_.erase(it);} }
        else {
            ParticleId id;
            if(it!=occupied_.end()) {
                id=it->second;auto& p=w.particles[id];bool wasBedrock=p.material==MaterialType::Bedrock;
                p.material=selectedMaterial;p.mass=material(selectedMaterial).density*std::pow(p.radius/PhysicsConfig::particleRadius,2);
                if(wasBedrock||selectedMaterial==MaterialType::Bedrock) p.setFreeze(selectedMaterial==MaterialType::Bedrock?FreezeMode::Absolute:FreezeMode::Free);
            } else {
                if(w.particles.size()>=PhysicsConfig::maxParticles) continue;
                id=w.addCell({x*spacing,y*spacing},selectedMaterial,false,PhysicsConfig::particleRadius*spacing);occupied_[key]=id;
            }
            auto& p=w.particles[id];p.worldCell=layer==EditorLayer::World;
            if(p.worldCell&&!p.pinned) p.setFreeze(FreezeMode::UntilContact);
            w.setTemperature(id,paintTemperature);
        }
    }
}
void Editor::segment(PhysicsWorld& w,Vec2 a,Vec2 b,bool erase) {
    float dist=length(b-a); int steps=std::max(1,int(std::ceil(dist/.4f)));
    // Input travel is bounded to avoid a pathological mouse event allocating huge loops.
    steps=std::min(steps,10000);
    for(int i=0;i<=steps;++i) stamp(w,a+(b-a)*(float(i)/steps),erase);
}
bool Editor::undo(PhysicsWorld& w) {
    if(undo_.empty()||drawing_) return false;
    redo_.push_back({w,selection,launchPoint}); auto s=std::move(undo_.back()); undo_.pop_back();
    w=std::move(s.world); selection=std::move(s.selection);launchPoint=s.point;return true;
}
bool Editor::redo(PhysicsWorld& w) {
    if(redo_.empty()||drawing_) return false;
    undo_.push_back({w,selection,launchPoint});auto s=std::move(redo_.back());redo_.pop_back();
    w=std::move(s.world);selection=std::move(s.selection);launchPoint=s.point;return true;
}
void Editor::clearHistory() {undo_.clear();redo_.clear();beforeStroke_.reset();drawing_=false;selection.clear();}
Vec2 Editor::launchVelocity() const { float angle=launchAngle*std::numbers::pi_v<float>/180; return {std::cos(angle)*launchSpeed,std::sin(angle)*launchSpeed}; }
}
