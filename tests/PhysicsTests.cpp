#include "app/SceneIO.h"
#include "app/Units.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace lab;
namespace {
void check(bool value,const char* msg) {if(!value) throw std::runtime_error(msg);}
void run(PhysicsWorld& w,int steps=240) {for(int i=0;i<steps;++i) w.step();for(auto& p:w.particles) check(finite(p.position)&&finite(p.velocity),"Non-finite physics");}
void rectangle(PhysicsWorld& w,int x,int y,int width,int height,MaterialType m,bool pin=false) {for(int j=0;j<height;++j) for(int i=0;i<width;++i) w.addCell({float(x+i),float(y+j)},m,pin);}
void testOptimizationSwitches() {
    PhysicsWorld source;source.config.gravity=9.81f;source.config.adaptiveDetail=false;
    rectangle(source,-5,0,10,3,MaterialType::Soil);
    for(auto& p:source.particles) {p.worldCell=true;p.setFreeze(FreezeMode::UntilContact);}
    auto projectile=source.addCell({0,-3},MaterialType::Steel);source.particles[projectile].velocity={0,15};source.rebuildBonds();
    auto optimized=source,reference=source;reference.config.optimizations.sleepingTerrain=false;reference.config.optimizations.cachedBonds=false;
    run(optimized,80);run(reference,80);
    check(!optimized.particles[5].untilContact,"Optimized terrain failed to wake");
    for(std::size_t i=0;i<source.particles.size();++i) {
        check(length(optimized.particles[i].position-reference.particles[i].position)<.01f,"Terrain optimization changed cold impact");
    }
    // Public topology can change without a dedicated mutation API.
    optimized=source;reference=source;reference.config.optimizations.cachedBonds=false;
    optimized.step();reference.step();optimized.bonds[0].broken=reference.bonds[0].broken=true;
    std::swap(optimized.bonds[1].b,optimized.bonds[2].b);std::swap(reference.bonds[1].b,reference.bonds[2].b);
    run(optimized,10);run(reference,10);
    for(std::size_t i=0;i<source.particles.size();++i) check(length(optimized.particles[i].position-reference.particles[i].position)<1e-6f,"Stale cached topology");
    PhysicsWorld flying;flying.addCell({0,0},MaterialType::Steel);flying.particles[0].velocity={5,0};
    for(bool buffered:{false,true}) for(bool background:{false,true}) {
        flying.config.optimizations.bufferedTimeline=buffered;flying.config.optimizations.backgroundCalculation=background;
        Timeline timeline(flying,.1);timeline.advance(true);timeline.advance(false);
        check(!timeline.baking()&&timeline.error().empty()&&timeline.lastFrame()==3,"Timeline option calculation failed");
        PhysicsWorld frame;timeline.restore(3,frame);auto position=frame.particles[0].position;
        timeline.restore(0,frame);check(frame.time==0,"Timeline option rewind failed");
        timeline.restore(3,frame);timeline.restore(3,frame);
        check(length(frame.particles[0].position-position)<1e-6f,"Timeline cached frame changed");
    }
    {Timeline cancelled(flying,120);cancelled.advance(true);cancelled.pauseCalculation(true);} // Stop and join before releasing frame storage.
    SimulationState state;state.world=flying;state.timelineDuration=.1f;state.calculate();state.advance(.1,false);
    auto options=state.world.config.optimizations;options.backgroundCalculation=!options.backgroundCalculation;
    state.setOptimizations(options);
    check(state.mode==Mode::Editor&&state.world.config.optimizations==options&&state.hasTimeline(),"Settings did not rebuild source timeline");
}
void testFuse() {
    PhysicsWorld w;w.addCell({0,0},MaterialType::Fuse);w.addCell({3,0},MaterialType::Steel,true);w.particles[0].velocity={35,0};run(w,60);
    check(w.stats.detonations==1&&!w.particles[0].active,"Fuse must trigger on strong steel contact");
    PhysicsWorld chain;chain.addCell({0,0},MaterialType::Fuse);chain.addCell({0,1},MaterialType::Explosive);chain.addCell({0,2},MaterialType::Explosive);chain.addCell({3,0},MaterialType::Steel,true);chain.rebuildBonds();
    chain.launch({0,1,2},{35,0},{0,1});run(chain,80);check(chain.stats.detonations==3,"Fuse must initiate nearby explosive chain");
    PhysicsWorld calm;calm.addCell({0,0},MaterialType::Fuse);calm.addCell({.9f,0},MaterialType::Explosive);run(calm,20);check(calm.stats.detonations==0,"Calm fuse/explosive contact detonated");
    PhysicsWorld only;only.addCell({0,0},MaterialType::Explosive);only.addCell({3,0},MaterialType::Steel,true);only.particles[0].velocity={140,0};run(only,80);check(only.stats.detonations==0&&only.particles[0].active,"Mechanical impact detonated explosive");
    PhysicsWorld soft;soft.addCell({0,0},MaterialType::Fuse);soft.addCell({3,0},MaterialType::Explosive,true);soft.particles[0].velocity={80,0};run(soft,80);check(soft.stats.detonations==0,"Fuse triggered on explosive contact");
}
void testMaterials() {
    PhysicsWorld weak;rectangle(weak,0,0,3,3,MaterialType::Plastic);rectangle(weak,10,-8,5,20,MaterialType::Steel,true);weak.rebuildBonds();
    weak.launch(weak.component(0),{25,0},{1,1});run(weak);
    float plasticDamage=0,steelDamage=0;std::size_t debris=0;
    for(auto& p:weak.particles) {if(p.material==MaterialType::Plastic){plasticDamage+=p.accumulatedDamage;check(p.position.x<10,"Slow plastic passed thick steel");++debris;}else steelDamage+=p.accumulatedDamage;}
    check(plasticDamage>steelDamage*2,"Weak material did not absorb more damage");check(debris==9,"Debris disappeared");check(weak.stats.brokenBonds>0,"Plastic impact did not fracture");
    PhysicsWorld heavy;rectangle(heavy,0,0,4,3,MaterialType::DepletedUranium);rectangle(heavy,10,-8,1,20,MaterialType::Plastic);heavy.rebuildBonds();heavy.launch(heavy.component(0),{90,0},{1.5f,1});run(heavy,160);
    check(heavy.stats.brokenBonds>0,"Thin plastic wall did not fracture");float maxX=0;for(auto& p:heavy.particles) if(p.material==MaterialType::DepletedUranium) maxX=std::max(maxX,p.position.x);check(maxX>12,"Dense object did not penetrate thin plastic");
}
void testDeformationAndRod() {
    PhysicsWorld bent;rectangle(bent,0,0,12,2,MaterialType::Steel);bent.rebuildBonds();
    // A distributed beam is clamped at one end and loaded sideways at the other.
    for(auto& p:bent.particles) {if(p.position.x==0) p.pinned=true;if(p.position.x>=9) p.velocity={0,55};}
    run(bent,80);float permanent=0;for(auto& b:bent.bonds) permanent=std::max(permanent,std::abs(b.restLength-b.originalLength));
    std::cout<<"Beam residual rest-length change: "<<permanent<<'\n';
    check(permanent>.001f,"Beam never developed permanent strain");
    float lowDamage=0,highDamage=0;
    for(float speed:{20.f,110.f}) {
        PhysicsWorld rod;rectangle(rod,0,0,10,2,MaterialType::Steel);rectangle(rod,18,-12,2,26,MaterialType::Steel);rod.rebuildBonds();rod.launch(rod.component(0),{speed,0},{4.5f,.5f});run(rod,180);
        float damage=0;for(auto& b:rod.bonds) damage+=std::min(1.f,b.accumulatedDamage);
        if(speed==20) lowDamage=damage;else highDamage=damage;
    }
    check(highDamage>lowDamage,"Higher normal rod speed did not increase fracture work");
    PhysicsWorld fastPlastic;rectangle(fastPlastic,0,0,3,3,MaterialType::Plastic);rectangle(fastPlastic,10,-15,7,32,MaterialType::DepletedUranium);fastPlastic.rebuildBonds();fastPlastic.launch(fastPlastic.component(0),{250,0},{1,1});run(fastPlastic,120);
    float wallDamage=0,projectileDamage=0;for(auto& p:fastPlastic.particles) {if(p.material==MaterialType::Plastic) projectileDamage+=p.accumulatedDamage;else wallDamage+=p.accumulatedDamage;}
    std::cout<<"Fast plastic damage: "<<projectileDamage/9<<" / target "<<wallDamage/224<<'\n';
    std::cout<<"Fast impact final energy: "<<fastPlastic.kineticEnergy()<<'\n';
    check(projectileDamage/9>wallDamage/224,"Fast plastic did not fail preferentially against dense thick target");
}
void testMomentumAndEnergy() {
    PhysicsWorld w;w.addCell({0,0},MaterialType::Steel);w.addCell({3,0},MaterialType::Steel);w.particles[0].velocity={25,0};
    float initial=w.kineticEnergy();run(w,40);
    Vec2 momentum{};for(auto& p:w.particles) momentum+=p.velocity*p.mass;
    check(std::abs(momentum.x-125)<2,"Contact did not conserve momentum within damping tolerance");
    check(w.kineticEnergy()<initial*1.05f,"Unforced contact created excessive kinetic energy");
}
void benchmark() {
    PhysicsWorld w;rectangle(w,-50,-50,100,100,MaterialType::Steel);w.rebuildBonds();
    auto t=std::chrono::steady_clock::now();run(w,120);
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
    std::cout<<"BENCHMARK: "<<w.particles.size()<<" cells, "<<w.bonds.size()<<" bonds, "<<120/seconds<<" fixed ticks/s (resting lattice)\n";
}
void testSteelImpactStability() {
    for(float speed:{35.f,65.f,110.f,180.f}) {
        PhysicsWorld w;rectangle(w,0,0,10,3,MaterialType::Steel);rectangle(w,20,-15,3,33,MaterialType::Steel);w.rebuildBonds();
        w.launch(w.component(0),{speed,0},{4.5f,1});float initialEnergy=w.kineticEnergy(),peak=initialEnergy;
        for(int i=0;i<180;++i) {w.step();peak=std::max(peak,w.kineticEnergy());}
        std::size_t brokenProjectile=0,brokenWall=0,totalProjectile=0,totalWall=0,largest=0;
        for(const auto& b:w.bonds) {if(b.a<30&&b.b<30){++totalProjectile;brokenProjectile+=b.broken;}else{++totalWall;brokenWall+=b.broken;}}
        std::vector<bool> visited(w.particles.size());
        for(ParticleId id=0;id<w.particles.size();++id) if(!visited[id]) {auto ids=w.component(id);largest=std::max(largest,ids.size());for(auto j:ids)visited[j]=true;}
        std::cout<<"STEEL speed="<<speed<<" broken projectile="<<brokenProjectile<<'/'<<totalProjectile<<" wall="<<brokenWall<<'/'<<totalWall<<" largest="<<largest<<" energy peak="<<peak/initialEnergy<<" detonations="<<w.stats.detonations<<'\n';
        check(w.stats.detonations==0&&w.blasts.empty(),"Steel impact generated a detonation");
        check(peak<=initialEnergy*1.10f,"Steel collision numerically multiplied kinetic energy");
        float mass=0;for(const auto& p:w.particles) if(p.active) mass+=p.mass;
        check(std::abs(mass-129*material(MaterialType::Steel).density)<.01f,"Steel impact removed ordinary debris mass");
        if(speed<=110) {
            check(brokenProjectile<totalProjectile/2,"Moderate steel impact pulverized the entire projectile");
            check(brokenWall<totalWall/3,"Moderate steel impact destroyed the whole wall");
            // A local opening may divide this 3-cell wall into two large pieces.
            check(largest>=30,"Moderate steel impact pulverized material away from contact");
        }
        for(const auto& p:w.particles) check(finite(p.position)&&finite(p.velocity),"Steel impact produced non-finite state");
    }
}
void testSweptAndAngle() {
    PhysicsWorld w;w.addCell({0,0},MaterialType::Steel);w.addCell({2,0},MaterialType::DepletedUranium,true);w.particles[0].velocity={250,0};run(w,8);check(w.particles[0].position.x<2,"High speed tunneling through one-cell obstacle");
    PhysicsWorld glancing;glancing.addCell({0,0},MaterialType::Steel);for(int y=-3;y<30;++y) glancing.addCell({4,float(y)},MaterialType::Steel,true);glancing.particles[0].velocity={15,100};run(glancing,70);
    check(glancing.particles[0].position.x<4,"Glancing contact crossed wall");check(glancing.particles[0].velocity.y>30,"Glancing contact lost all tangent velocity");
}
void testEditorAndIO() {
    SimulationState s;s.editor.radius=1;s.editor.beginStroke(s.world,{0,0});s.editor.continueStroke(s.world,{5,0});s.editor.endStroke(s.world,{5,0});auto count=s.world.particles.size();check(count>5,"Brush continuity");check(s.editor.undo(s.world)&&s.world.particles.empty(),"Undo");check(s.editor.redo(s.world)&&s.world.particles.size()==count,"Redo");
    s.editor.tool=Tool::Select;s.editor.beginStroke(s.world,{0,0});s.editor.endStroke(s.world,{1,0});check(s.editor.selection.size()==count,"Component selection");
    auto path=std::filesystem::temp_directory_path()/"материалы_тест.scene";std::string message;check(saveScene(path,s,message),"Save failed");SimulationState loaded;check(loadScene(path,loaded,message),"Load failed");check(loaded.world.particles.size()==count&&loaded.world.bonds.size()==s.world.bonds.size(),"Roundtrip mismatch");check(loaded.start(),"Start selected object");loaded.advance(.1);check(loaded.world.time>0,"Accumulator");loaded.reset(true);check(loaded.world.time==0&&loaded.mode==Mode::Editor,"Reset snapshot");
    {std::ofstream out(path);out<<"MATERIAL_LAB 1\n20000000 0 0\n";}check(!loadScene(path,loaded,message)&&loaded.world.particles.size()==count,"Load must reject oversized scene transactionally");std::filesystem::remove(path);
}
void testMapsAndTerrain() {
    SimulationState empty;empty.newMap(MapType::Void);
    check(empty.world.particles.empty()&&empty.world.config.gravity==0&&empty.world.config.airDrag==0,"Void is not empty vacuum");
    SimulationState earth;earth.newMap(MapType::Earth);
    check(earth.world.config.gravity==MapConfig::earthGravity,"Earth gravity missing");
    std::size_t soil=0,bedrock=0;
    for(auto& p:earth.world.particles) {soil+=p.material==MaterialType::Soil;bedrock+=p.material==MaterialType::Bedrock;if(p.material==MaterialType::Bedrock) check(p.pinned&&p.inverseMass()==0,"Bedrock not fixed");}
    check(soil==2*MapConfig::groundHalfWidth*MapConfig::soilDepth&&bedrock==2*MapConfig::groundHalfWidth*MapConfig::bedrockDepth,"Earth terrain generation");
    check(MapConfig::groundHalfWidth>=500&&earth.world.config.sampleSpacing==.25f,"Earth size and drawing resolution");
    check(earth.world.component(0).size()==soil,"Ground glued to bedrock");
    check(earth.start(),"Gravity-only scene must start without projectile");earth.reset(true);
    auto path=std::filesystem::temp_directory_path()/"земля_карта.scene";std::string msg;
    check(saveScene(path,earth,msg),"Earth save");SimulationState loaded;check(loadScene(path,loaded,msg),"Earth load");
    check(loaded.mapType==MapType::Earth&&loaded.world.particles.size()==soil+bedrock&&loaded.world.config.airDrag==earth.world.config.airDrag,"Earth roundtrip environment");
    // Version 1 contained no environment header; keep original material ids and settings.
    {std::ofstream out(path);out<<"MATERIAL_LAB 1\n1 0 0\n65 0 0 0 0 1\n0 0 0 0\n";}
    check(loadScene(path,loaded,msg)&&loaded.mapType==MapType::Void&&loaded.world.particles[0].material==MaterialType::Steel,"Legacy scene compatibility");std::filesystem::remove(path);
    PhysicsWorld falling;falling.config.gravity=MapConfig::earthGravity;
    falling.addCell({0,0},MaterialType::Steel);falling.addCell({4,0},MaterialType::Plastic);run(falling,120);
    check(std::abs(falling.particles[0].position.y-falling.particles[1].position.y)<1e-5f,"Gravity depends on material mass");
    check(std::abs(falling.particles[0].velocity.y-MapConfig::earthGravity*.5f)<.02f,"Earth gravity acceleration");
    PhysicsWorld granular;rectangle(granular,0,0,8,3,MaterialType::Soil);rectangle(granular,-3,4,14,1,MaterialType::Bedrock);rectangle(granular,3,-4,2,2,MaterialType::Steel);granular.rebuildBonds();
    granular.config.gravity=MapConfig::earthGravity;auto projectile=granular.component(38);granular.launch(projectile,{0,35},{3.5f,-3.5f});run(granular,180);
    std::size_t brokenSoil=0;float displacement=0;
    for(const auto& b:granular.bonds) if(b.broken&&(granular.particles[b.a].material==MaterialType::Soil||granular.particles[b.b].material==MaterialType::Soil)) ++brokenSoil;
    for(std::size_t i=0;i<24;++i) {check(granular.particles[i].active,"Soil grains deleted on impact");displacement=std::max(displacement,length(granular.particles[i].position-Vec2{float(i%8),float(i/8)}));}
    check(brokenSoil>0&&displacement>1,"Soil did not scatter on impact");
}
void testBedrock() {
    PhysicsWorld w;w.addCell({0,-3},MaterialType::Steel);rectangle(w,-3,0,7,2,MaterialType::Bedrock);w.rebuildBonds();w.particles[0].velocity={0,250};
    auto original=w.particles;for(auto& p:w.particles) if(p.material==MaterialType::Bedrock) p.pinned=false; // Material-level guarantee, not merely an editor flag.
    run(w,100);check(w.particles[0].position.y<0,"Projectile penetrated bedrock");
    for(std::size_t i=1;i<original.size();++i) if(original[i].material==MaterialType::Bedrock) check(length(w.particles[i].position-original[i].position)<1e-6f&&w.particles[i].active&&w.particles[i].accumulatedDamage==0,"Bedrock moved or was damaged by contact");
    check(w.stats.brokenBonds==0,"Bedrock fractured on impact");
    PhysicsWorld blast;blast.addCell({0,-3},MaterialType::Fuse);blast.addCell({1,-3},MaterialType::Explosive);rectangle(blast,-3,0,7,2,MaterialType::Bedrock);blast.rebuildBonds();
    blast.particles[0].velocity=blast.particles[1].velocity={0,40};original=blast.particles;run(blast,100);
    check(blast.stats.detonations==2,"Bedrock contact must still trigger fuse");
    for(std::size_t i=2;i<original.size();++i) if(original[i].material==MaterialType::Bedrock) check(length(blast.particles[i].position-original[i].position)<1e-6f&&blast.particles[i].active&&blast.particles[i].accumulatedDamage==0&&blast.particles[i].temperature==20,"Bedrock damaged by explosion");
    for(auto& b:blast.bonds) if(blast.particles[b.a].material==MaterialType::Bedrock) check(!b.broken&&b.accumulatedDamage==0,"Bedrock bonds damaged by explosion");
}
void testFreezeBrushAndWorld() {
    SimulationState s;rectangle(s.world,0,0,3,1,MaterialType::Steel);s.world.rebuildBonds();
    s.world.bonds[0].accumulatedDamage=.2f;
    s.editor.radius=.6f;s.editor.tool=Tool::Anchor;s.editor.anchorMode=FreezeMode::Absolute;
    s.editor.beginStroke(s.world,{1,0});s.editor.endStroke(s.world,{1,0});
    check(!s.world.particles[0].pinned&&s.world.particles[1].pinned&&!s.world.particles[2].pinned,"Fixation brush pinned whole object");
    check(s.world.bonds[0].accumulatedDamage==.2f,"Fixation rebuilt bonds");
    check(s.editor.undo(s.world)&&!s.world.particles[1].pinned,"Fixation undo");
    check(s.editor.redo(s.world)&&s.world.particles[1].pinned,"Fixation redo");
    s.world.launch({0,1,2},{30,0},{1,0});s.world.config.gravity=9.81f;run(s.world,60);
    check(length(s.world.particles[1].position-Vec2{1,0})<1e-6f&&s.world.particles[1].pinned,"Launch released absolute fixation");
    PhysicsWorld held;held.config.gravity=9.81f;held.addCell({0,0},MaterialType::Steel);held.particles[0].setFreeze(FreezeMode::UntilContact);
    run(held,240);check(held.particles[0].untilContact&&length(held.particles[0].position)<1e-6f,"Until-contact fell without contact");
    held.addCell({0,-3},MaterialType::Steel);held.particles[1].velocity={0,10};run(held,120);
    check(!held.particles[0].untilContact&&held.particles[0].position.y>0,"External contact did not release fixation");
    PhysicsWorld internal;rectangle(internal,0,0,3,1,MaterialType::Steel);internal.rebuildBonds();internal.particles[0].setFreeze(FreezeMode::UntilContact);
    internal.particles[2].position={.7f,0};run(internal,1);
    check(internal.particles[0].untilContact,"Internal object contact released fixation");
    SimulationState terrain;terrain.newMap(MapType::Earth);
    terrain.editor.layer=EditorLayer::World;terrain.editor.selectedMaterial=MaterialType::Soil;terrain.editor.radius=2;
    terrain.editor.beginStroke(terrain.world,{0,17});terrain.editor.continueStroke(terrain.world,{0,12});terrain.editor.endStroke(terrain.world,{0,12});
    auto original=terrain.world.particles;run(terrain.world,240);
    for(std::size_t i=0;i<original.size();++i) check(length(terrain.world.particles[i].position-original[i].position)<1e-6f,"Dormant mountain compressed or fell");
    for(const auto& b:terrain.world.bonds) check(!b.broken&&b.accumulatedDamage==0,"Dormant ground gained damage");
    terrain.world.addCell({0,7},MaterialType::Steel);terrain.world.particles.back().velocity={0,30};run(terrain.world,100);
    std::size_t released=0,dormant=0;float moved=0;
    for(std::size_t i=0;i<original.size();++i) if(original[i].material==MaterialType::Soil) {
        released+=!terrain.world.particles[i].untilContact;dormant+=terrain.world.particles[i].untilContact;
        moved=std::max(moved,length(terrain.world.particles[i].position-original[i].position));
        check(terrain.world.particles[i].active,"Awakened soil disappeared");
    }
    check(released>0&&dormant>0&&moved>.1f,"Terrain did not wake locally and scatter");
    // Save an editable lattice, as the application's Save command does, not a destruction checkpoint.
    terrain.world.particles=original;terrain.world.rebuildBonds();
    terrain.world.particles[0].setFreeze(FreezeMode::Absolute);
    terrain.world.particles[1].setFreeze(FreezeMode::Free);
    auto path=std::filesystem::temp_directory_path()/"freeze_modes.scene";std::string msg;
    terrain.editor.tool=Tool::Anchor;terrain.editor.anchorMode=FreezeMode::UntilContact;
    check(saveScene(path,terrain,msg),"Freeze save");SimulationState loaded;check(loadScene(path,loaded,msg),"Freeze load");
    check(loaded.editor.layer==EditorLayer::World&&loaded.editor.anchorMode==FreezeMode::UntilContact&&loaded.editor.tool==Tool::Anchor,"Editor modes lost in save");
    for(std::size_t i=0;i<loaded.world.particles.size();++i) check(loaded.world.particles[i].freezeMode()==terrain.world.particles[i].freezeMode()&&loaded.world.particles[i].worldCell==terrain.world.particles[i].worldCell,"Cell freeze modes lost in save");
    {std::ofstream out(path);out<<"MATERIAL_LAB 2\n1 0.015\n1 0 0\n65 0 0 0 9.81 1\n0 0 5 0\n";}
    check(loadScene(path,loaded,msg)&&loaded.world.particles[0].freezeMode()==FreezeMode::Free,"Version 2 compatibility");
    {std::ofstream out(path);out<<"MATERIAL_LAB 3\n1 0.015\n1 0 0\n65 0 0 0 9.81 1\n1 2 4 5 0.6\n0 0 5 0 1 1\n";}
    check(loadScene(path,loaded,msg)&&loaded.world.particles[0].untilContact&&loaded.world.particles[0].worldCell&&loaded.world.particles[0].temperature==20,"Version 3 compatibility");
    std::filesystem::remove(path);
    PhysicsWorld blast;blast.addCell({0,-3},MaterialType::Fuse);blast.addCell({1,-3},MaterialType::Explosive);blast.addCell({0,0},MaterialType::Bedrock);
    blast.addCell({2,0},MaterialType::Steel);blast.particles[3].setFreeze(FreezeMode::UntilContact);
    blast.addCell({-2,0},MaterialType::Steel);blast.particles[4].setFreeze(FreezeMode::Absolute);
    blast.particles[0].velocity=blast.particles[1].velocity={0,40};run(blast,100);
    check(blast.stats.detonations==2&&!blast.particles[3].untilContact,"Blast did not wake temporary fixation");
    check(blast.particles[4].pinned&&length(blast.particles[4].position-Vec2{-2,0})<1e-6f,"Blast moved absolute fixation");
    Editor e;e.tool=Tool::Anchor;e.anchorMode=FreezeMode::Free;e.radius=.6f;e.beginStroke(blast,{0,0});e.endStroke(blast,{0,0});
    check(blast.particles[2].inverseMass()==0,"Release brush made bedrock movable");
}
void testDeterminism() {
    PhysicsWorld a;rectangle(a,0,0,4,4,MaterialType::Steel);a.rebuildBonds();a.launch(a.component(0),{30,0},{1.5f,1.5f});auto b=a;run(a,100);run(b,100);
    for(std::size_t i=0;i<a.particles.size();++i) check(length(a.particles[i].position-b.particles[i].position)<1e-6f,"Deterministic stepping");
    SimulationState sa,sb;sa.world=b;sb.world=b;sa.editor.selection=b.component(0);sb.editor.selection=sa.editor.selection;sa.editor.launchPoint=sb.editor.launchPoint={1.5f,1.5f};sa.start();sb.start();
    for(int i=0;i<120;++i) sa.advance(1./120,false);for(int i=0;i<60;++i) sb.advance(1./60,false);
    check(std::abs(sa.world.time-sb.world.time)<PhysicsConfig::fixedDt*1.01,"Frame rate dependent fixed clock");
    SimulationState budgeted;rectangle(budgeted.world,0,0,40,40,MaterialType::Steel);budgeted.world.rebuildBonds();budgeted.editor.selection=budgeted.world.component(0);budgeted.editor.launchPoint={19.5f,19.5f};budgeted.start();budgeted.advance(.1);
    check(budgeted.world.time>0&&std::abs(budgeted.world.time+budgeted.accumulator-.1)<1e-6,"Frame work budget lost outstanding simulation time");
}
void benchmarkActive() {
    for(int scene=0;scene<3;++scene) {
        PhysicsWorld w;
        if(scene==0) {
            w.config.sampleSpacing=.25f;
            for(int y=0;y<24;++y) for(int x=0;x<96;++x) w.addCell({x*.25f,y*.25f},MaterialType::Steel,x<2||x>93,.115f);
            for(int y=0;y<24;++y) for(int x=0;x<12;++x) {auto id=w.addCell({10+x*.25f,-9+y*.25f},MaterialType::Steel,false,.115f);w.particles[id].velocity={0,65};}
            w.rebuildBonds();
        } else if(scene==1) {
            w.config.sampleSpacing=.25f;
            for(int y=0;y<24;++y) for(int x=0;x<64;++x) {auto id=w.addCell({x*.25f,y*.25f},MaterialType::Plastic,false,.115f);w.setTemperature(id,500);w.particles[id].velocity={12,5};}
            w.rebuildBonds();
        } else {rectangle(w,0,0,100,100,MaterialType::Steel);w.rebuildBonds();w.launch(w.component(0),{20,0},{49.5f,49.5f});}
        auto start=std::chrono::steady_clock::now();run(w,120);
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),checksum=0;
        for(const auto& p:w.particles) if(p.active) checksum+=p.mass*(p.position.x+3*p.position.y+p.temperature*.001f);
        std::cout<<"ACTIVE scene="<<scene<<" cells="<<w.particles.size()<<" ticks/s="<<120/seconds<<" checksum="<<std::setprecision(15)<<checksum<<'\n';
    }
}
void diagnoseClampedSteel() {
    for(float spacing:{1.f,.25f}) for(float speed:{15.f,35.f,65.f}) {
        PhysicsWorld w;w.config.sampleSpacing=spacing;w.config.adaptiveDetail=false;
        int nx=int(20/spacing)+1,ny=int(1/spacing)+1;
        for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) w.addCell({x*spacing,y*spacing},MaterialType::Steel,x==0||x==nx-1,.46f*spacing);
        auto targetCount=w.particles.size();
        for(int y=0;y<int(4/spacing);++y) for(int x=0;x<int(1/spacing)+1;++x) w.addCell({9.5f+x*spacing,-6+y*spacing},MaterialType::Steel,false,.46f*spacing);
        w.rebuildBonds();std::vector<ParticleId> projectile;for(ParticleId i=ParticleId(targetCount);i<w.particles.size();++i) projectile.push_back(i);
        w.launch(projectile,{0,speed},{10,-4});float peak=0,residual=0,plastic=0;std::size_t broken=0;
        for(int step=0;step<1440;++step) {w.step();float deflection=0;for(std::size_t i=0;i<targetCount;++i) if(std::abs(w.particles[i].position.x-10)<1) deflection=std::max(deflection,std::abs(w.particles[i].position.y-float(i/nx)*spacing));peak=std::max(peak,deflection);if(step>1200) residual+=deflection/239;}
        for(const auto& b:w.bonds) if(b.a<targetCount&&b.b<targetCount) {plastic=std::max(plastic,std::abs(b.restLength/b.originalLength-1));broken+=b.broken;}
        std::cout<<"CLAMP spacing="<<spacing<<" speed="<<speed<<" peak="<<peak<<" residual="<<residual<<" plastic="<<plastic<<" broken="<<broken<<'\n';
    }
}
void testMetalYieldAndUnloading() {
    PhysicsWorld small;small.config.adaptiveDetail=false;small.addCell({0,0},MaterialType::Steel);small.addCell({1,0},MaterialType::Steel);small.rebuildBonds();
    small.particles[1].position=small.particles[1].previousPosition={1.003f,0};run(small,240);
    check(std::abs(small.bonds[0].restLength-1)<1e-6f&&!small.bonds[0].broken,"Small elastic steel load became a permanent dent");
    PhysicsWorld flying;rectangle(flying,0,0,3,3,MaterialType::Steel);flying.rebuildBonds();flying.launch(flying.component(0),{17,0},{1,1});float initial=flying.kineticEnergy();run(flying,120);
    check(std::abs(flying.kineticEnergy()-initial)<initial*.005f,"Bond damping globally braked a rigid flying body");
    PhysicsWorld w;w.config.sampleSpacing=.25f;w.config.adaptiveDetail=false;
    constexpr int nx=81,ny=5;constexpr float spacing=.25f;
    for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) w.addCell({x*spacing,y*spacing},MaterialType::Steel,x==0||x==nx-1,.46f*spacing);
    auto count=w.particles.size();
    for(int y=0;y<16;++y) for(int x=0;x<5;++x) w.addCell({9.5f+x*spacing,-6+y*spacing},MaterialType::Steel,false,.46f*spacing);
    w.rebuildBonds();std::vector<ParticleId> projectile;for(ParticleId i=ParticleId(count);i<w.particles.size();++i) projectile.push_back(i);w.launch(projectile,{0,15},{10,-4});
    float peak=0,lateMin=1000,lateMax=0;
    for(int step=0;step<1200;++step) {
        if(step==240) for(auto id:projectile) w.particles[id].active=false; // Remove the external load after the collision.
        w.step();float deflection=0;
        for(std::size_t i=0;i<count;++i) {
            const auto& p=w.particles[i];Vec2 original{float(i%nx)*spacing,float(i/nx)*spacing};
            if(p.pinned) check(length(p.position-original)<1e-6f,"Clamped metal support moved");
            if(std::abs(original.x-10)<1) deflection=std::max(deflection,std::abs(p.position.y-original.y));
        }
        peak=std::max(peak,deflection);if(step>=1000) {lateMin=std::min(lateMin,deflection);lateMax=std::max(lateMax,deflection);}
    }
    float plastic=0;std::size_t broken=0;
    for(const auto& b:w.bonds) if(b.a<count&&b.b<count) {plastic=std::max(plastic,std::abs(b.restLength/b.originalLength-1));broken+=b.broken;}
    std::cout<<"UNLOADED STEEL peak="<<peak<<" retained="<<lateMin<<".."<<lateMax<<" plastic="<<plastic<<" broken="<<broken<<'\n';
    check(peak>1&&lateMin>peak*.7f&&lateMax-lateMin<peak*.2f,"Steel beam sprang back after a plastic bend");
    check(plastic>.02f&&broken==0,"Moderate plastic bending should leave an intact permanent dent");
}
void testThermalV2() {
    PhysicsWorld latent;latent.config.adaptiveDetail=false;latent.addCell({0,0},MaterialType::Steel);
    const auto& steel=material(MaterialType::Steel);float mass=latent.particles[0].mass;
    latent.addHeat(0,mass*(steel.heatCapacity*(steel.meltingPoint-20)+steel.latentHeat*.5f));
    check(std::abs(latent.particles[0].temperature-steel.meltingPoint)<.01f&&std::abs(latent.particles[0].liquidFraction-.5f)<.001f,"Latent heat did not hold melting temperature");
    latent.addHeat(0,-mass*steel.latentHeat*.5f);
    check(latent.particles[0].liquidFraction<.001f,"Cooling failed to solidify metal");
    PhysicsWorld transfer;transfer.config.adaptiveDetail=false;
    transfer.addCell({0,0},MaterialType::Steel,true);transfer.addCell({1,0},MaterialType::Steel,true);transfer.rebuildBonds();transfer.setTemperature(0,2200);
    float before=transfer.thermalEnergy();run(transfer,60);
    check(transfer.particles[1].temperature>300,"Hot material did not conduct into neighbor");
    check(transfer.thermalEnergy()<=before*1.001f&&transfer.thermalEnergy()>before*.97f,"Conduction generated or excessively lost heat");
    PhysicsWorld contact;contact.config.adaptiveDetail=false;contact.addCell({0,0},MaterialType::Steel,true);contact.addCell({.9f,0},MaterialType::Plastic,true);contact.setTemperature(0,1800);run(contact,240);
    check(contact.particles[1].liquidFraction>.1f,"Hot contact did not melt plastic obstacle");
    PhysicsWorld melting;rectangle(melting,0,0,3,1,MaterialType::Plastic);melting.rebuildBonds();melting.setTemperature(1,300);melting.config.adaptiveDetail=false;run(melting,1);
    check(melting.liveBondCount()==0&&melting.activeParticleCount()==3,"Melting did not release liquid without losing material");
    melting.setTemperature(1,20);check(melting.particles[1].liquidFraction==0,"Melt failed to solidify on cooling");
    PhysicsWorld friction;friction.addCell({0,0},MaterialType::Steel);friction.addCell({3,0},MaterialType::Steel);friction.particles[0].velocity={40,0};float energy=friction.kineticEnergy();run(friction,40);
    check(friction.thermalEnergy()>0&&friction.kineticEnergy()<energy,"Impact did not turn dissipation into heat");
    PhysicsWorld air;air.config.airDrag=.5f;air.addCell({0,0},MaterialType::Steel);air.particles[0].velocity={80,0};auto vacuum=air;vacuum.config.airDrag=0;
    run(air,120);run(vacuum,120);check(air.particles[0].velocity.x<vacuum.particles[0].velocity.x&&air.thermalEnergy()>0,"Air friction missing");
    check(std::abs(vacuum.particles[0].velocity.x-80)<.1f&&std::abs(vacuum.thermalEnergy())<.001f,"Vacuum generated air friction");
    PhysicsWorld detail;detail.addCell({0,0},MaterialType::Plastic);detail.setTemperature(0,300);detail.particles[0].velocity={20,0};float originalMass=detail.particles[0].mass;Vec2 originalMomentum=detail.particles[0].velocity*originalMass;
    run(detail,16);float totalMass=0;Vec2 momentum{};float minRadius=.46f;
    for(const auto& p:detail.particles) if(p.active) {totalMass+=p.mass;momentum+=p.velocity*p.mass;minRadius=std::min(minRadius,p.radius);}
    check(detail.particles.size()>4&&minRadius<=.46f/1024,"Adaptive spray did not reach microscopic detail");
    check(std::abs(totalMass-originalMass)<1e-4f&&length(momentum-originalMomentum)<.02f,"Refinement lost mass or momentum");
    SimulationState s;s.newMap(MapType::Void);s.editor.paintTemperature=500;s.editor.selectedMaterial=MaterialType::Plastic;s.editor.radius=.6f;
    s.editor.beginStroke(s.world,{0,0});s.editor.endStroke(s.world,{0,0});
    check(s.world.particles.size()>4&&s.world.particles[0].radius==.115f&&s.world.particles[0].liquidFraction==1,"Fine hot brush did not create melt");
    s.editor.tool=Tool::Cool;s.editor.beginStroke(s.world,{0,0});s.editor.endStroke(s.world,{0,0});check(s.world.particles[0].temperature==20,"Cooling brush");
    check(s.editor.undo(s.world)&&s.world.particles[0].temperature==500,"Thermal brush undo");
    auto path=std::filesystem::temp_directory_path()/"v2_thermal.scene";std::string msg;
    check(saveScene(path,s,msg),"V2 thermal save");SimulationState loaded;check(loadScene(path,loaded,msg),"V2 thermal load");
    check(loaded.world.config.sampleSpacing==.25f&&loaded.editor.paintTemperature==500&&loaded.world.particles[0].temperature==500&&loaded.world.particles[0].liquidFraction==1&&loaded.world.particles[0].radius==.115f,"V2 thermal roundtrip");
    std::filesystem::remove(path);
}
void testTimelineAndToybox() {
    check(std::abs(units::toKmh(10)-36)<1e-6f&&std::abs(units::fromKmh(72)-20)<1e-6f,"Metric speed conversions");
    SimulationState s;s.newMap(MapType::Void);s.timelineDuration=.2f;
    s.world.config.adaptiveDetail=false;s.world.config.gravity=9.81f;
    auto id=s.world.addCell({0,0},MaterialType::Steel,false,.115f);s.world.rebuildBonds();
    s.editor.selection={id};s.editor.launchPoint={0,0};s.editor.launchSpeed=10;
    auto initial=s.world;auto expected=initial;expected.launch(s.editor.selection,s.editor.launchVelocity(),s.editor.launchPoint);
    run(expected,48);
    check(s.calculate(),"Timeline calculation start");s.advance(.1,false);
    check(!s.baking()&&s.mode==Mode::Paused&&std::abs(s.calculatedSeconds()-.2)<1e-6,"Timeline range completion");
    check(length(s.world.particles[0].position-expected.particles[0].position)<1e-6f,"Cached final frame differs from fixed-step calculation");
    check(s.seek(.1)&&s.mode==Mode::Paused,"Timeline seek");auto saved=s.world.particles[0];
    s.advance(.1,false);check(length(s.world.particles[0].position-saved.position)<1e-6f,"Paused cached playback ran physics");
    check(s.seek(0)&&length(s.world.particles[0].position)<1e-6f,"Timeline rewind");
    check(s.seek(.1)&&length(s.world.particles[0].position-saved.position)<1e-6f&&s.world.particles[0].temperature==saved.temperature,"Timeline replay changed cached state");
    s.stepFrame(-1);check(std::abs(s.timelineCursor-2./30)<1e-6,"Previous cached frame");
    s.reset(true);check(s.mode==Mode::Editor&&s.hasTimeline(),"Return to source editor lost cache");
    s.world.particles[0].position.x=3;s.world.particles[0].previousPosition=s.world.particles[0].position;
    s.invalidateTimeline();check(s.baking()&&s.mode==Mode::Editor&&s.calculatedSeconds()==0,"Scene edits did not invalidate and automatically recalculate cache");
    s.advance(.1,false);check(s.mode==Mode::Editor&&s.world.particles[0].position.x==3,"Background calculation replaced editor source");
    s.seek(0);check(s.world.particles[0].position.x==3,"Recalculated source is stale");
    s.reset(true);s.timelineDuration=.3f;s.calculate(false);s.advance(.1,false);check(std::abs(s.calculatedSeconds()-.3)<1e-6,"Manual recalculation duration");
    auto path=std::filesystem::temp_directory_path()/"material-lab-v5.scene";std::string msg;
    check(saveScene(path,s,msg),"Timeline scene save");SimulationState loaded;check(loadScene(path,loaded,msg)&&loaded.timelineDuration==.3f&&!loaded.hasTimeline(),"Timeline settings roundtrip / transient cache");std::filesystem::remove(path);
    SimulationState toy;toy.world.config.sampleSpacing=.25f;
    toy.world.addCell({5,2},MaterialType::Steel,false,.115f);toy.world.addCell({5.25f,2},MaterialType::Steel,false,.115f);toy.world.rebuildBonds();
    toy.world.particles[0].setFreeze(FreezeMode::UntilContact);toy.world.setTemperature(0,700);toy.editor.selection=toy.world.component(0);
    auto toyPath=std::filesystem::temp_directory_path()/"material-lab-construction.toy";
    check(saveConstruction(toyPath,toy,msg),"Save selected construction");SimulationState prefab;check(loadScene(toyPath,prefab,msg),"Load saved construction");
    SimulationState destination;destination.newMap(MapType::Earth);auto before=destination.world.particles.size();auto bondsBefore=destination.world.bonds.size();
    check(destination.editor.insert(destination.world,prefab.world,{30,-5},msg),"Insert construction into another map");
    check(destination.world.particles.size()==before+2&&destination.world.bonds.size()==bondsBefore+1&&destination.world.particles[before].untilContact&&destination.world.particles[before].temperature==700,"Construction properties and bonds lost");
    check(!destination.editor.insert(destination.world,prefab.world,{30,-5},msg)&&destination.world.particles.size()==before+2,"Overlapping construction must fail atomically");
    check(destination.editor.undo(destination.world)&&destination.world.particles.size()==before,"Construction insertion undo");
    check(destination.editor.redo(destination.world)&&destination.world.particles.size()==before+2,"Construction insertion redo");
    check(saveScene(path,destination,msg)&&loadScene(path,loaded,msg),"Inserted mixed-scale construction scene roundtrip");
    destination.world.rebuildBonds();check(destination.world.component(before).size()==2&&destination.world.component(0).size()==2*MapConfig::groundHalfWidth*MapConfig::soilDepth,"Mixed-scale terrain / object bond rebuilding");
    std::filesystem::remove(path);std::filesystem::remove(toyPath);
    SimulationState legacyMap;legacyMap.world.config.sampleSpacing=1;
    check(legacyMap.editor.insert(legacyMap.world,prefab.world,{0,0},msg),"Insert fine construction into legacy coarse map");legacyMap.world.rebuildBonds();
    check(legacyMap.world.component(0).size()==2,"Legacy map destroyed fine construction bonds");
    SimulationState melt;melt.timelineDuration=.1f;melt.world.config.sampleSpacing=.25f;
    auto hot=melt.world.addCell({0,0},MaterialType::Plastic,false,.115f);melt.world.setTemperature(hot,500);
    auto expectedMelt=melt.world;expectedMelt.launch({},melt.editor.launchVelocity(),{});run(expectedMelt,24);
    check(melt.calculate(),"Adaptive timeline start");melt.advance(.1,false);
    check(melt.world.particles.size()==expectedMelt.particles.size()&&melt.world.particles.size()>1,"Adaptive cache dropped newly created particles");
    for(std::size_t i=0;i<expectedMelt.particles.size();++i) check(length(melt.world.particles[i].position-expectedMelt.particles[i].position)<1e-6f&&melt.world.particles[i].mass==expectedMelt.particles[i].mass&&melt.world.particles[i].temperature==expectedMelt.particles[i].temperature,"Adaptive frame differs from original calculation");
    melt.seek(0);check(melt.world.particles.size()==1,"Adaptive timeline rewind retained future particles");
    SimulationState cachedEarth;cachedEarth.newMap(MapType::Earth);check(cachedEarth.calculate(false),"Earth timeline start");cachedEarth.advance(.1,false);
    check(!cachedEarth.baking()&&!cachedEarth.timelineFailed()&&cachedEarth.calculatedSeconds()==10,"Static kilometre Earth exceeded default timeline cache budget");
    // Old v4 files remain readable without the new timeline settings header.
    {std::ofstream out(path);out<<"MATERIAL_LAB 4\n0 0\n0.25 20 1 1\n1 0 0\n65 0 0 0 0 1\n0 1 0 0 2 20 1800\n0 0 0 0 0 0 20 0 0.115 0.3125 0\n\n";}
    check(loadScene(path,loaded,msg)&&loaded.timelineDuration==10,"Version 4 compatibility");std::filesystem::remove(path);
}

}
int main(int argc,char** argv) {try {
    if(argc==2&&std::string(argv[1])=="--benchmark") {benchmark();return 0;}
    if(argc==2&&std::string(argv[1])=="--benchmark-active") {benchmarkActive();return 0;}
    if(argc==2&&std::string(argv[1])=="--diagnose-clamp") {diagnoseClampedSteel();return 0;}
    if(argc==2&&std::string(argv[1])=="--diagnose-steel") {testSteelImpactStability();return 0;}
    testOptimizationSwitches();std::cout<<"PASS independent optimizations / topology invalidation / threaded timeline / cancellation\n";
    testFuse();std::cout<<"PASS fuse / explosive gating\n";
    testMaterials();std::cout<<"PASS material contrast / fracture / penetration\n";
    testDeformationAndRod();std::cout<<"PASS beam plasticity / normal rod impact\n";
    testMetalYieldAndUnloading();std::cout<<"PASS metal yield / unloaded permanent bend / elastic limit / axial damping\n";
    testMomentumAndEnergy();std::cout<<"PASS unforced momentum / energy bounds\n";
    testSteelImpactStability();std::cout<<"PASS steel impact / no artificial detonation\n";
    testSweptAndAngle();std::cout<<"PASS high-speed / glancing collisions\n";
    testEditorAndIO();std::cout<<"PASS editor / scene IO / reset\n";
    testMapsAndTerrain();std::cout<<"PASS maps / gravity / soil / legacy saves\n";
    testBedrock();std::cout<<"PASS bedrock / contacts / explosion immunity\n";
    testFreezeBrushAndWorld();std::cout<<"PASS fixation brush / contact release / dormant terrain / save compatibility\n";
    testDeterminism();std::cout<<"PASS fixed timestep / determinism\n";
    testTimelineAndToybox();std::cout<<"PASS metric units / cached timeline / automatic recalculation / toybox / mixed-scale Earth / v5 saves\n";
    testThermalV2();std::cout<<"PASS V2 thermal / latent heat / contact melting / air / adaptive conservation / saves\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
