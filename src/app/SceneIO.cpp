#include "SceneIO.h"
#include <fstream>
#include <iomanip>
#include <locale>
#include <unordered_set>
namespace lab {
bool saveConstruction(const std::filesystem::path& path,const SimulationState& state,std::string& message) {
    if(state.mode!=Mode::Editor||state.editor.selection.empty()||state.editor.drawing()) {message="Выделите конструкцию инструментом «Выбор» в редакторе.";return false;}
    const auto& source=state.world;SimulationState toy;toy.world.config=source.config;
    std::unordered_map<ParticleId,ParticleId> remap;
    Vec2 lo{1e9f,1e9f},hi{-1e9f,-1e9f};
    for(auto id:state.editor.selection) {
        if(id>=source.particles.size()||!source.particles[id].active) {message="Некорректное выделение.";return false;}
        if(remap.contains(id)) continue;
        remap[id]=ParticleId(toy.world.particles.size());toy.world.particles.push_back(source.particles[id]);
        auto p=source.particles[id].position;lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);
    }
    auto center=(lo+hi)*.5f;
    for(auto& p:toy.world.particles) {p.position-=center;p.previousPosition=p.position;p.velocity={};}
    for(auto b:source.bonds) if(remap.contains(b.a)&&remap.contains(b.b)) {b.a=remap[b.a];b.b=remap[b.b];b.lambda=0;toy.world.bonds.push_back(b);}
    toy.editor.launchSpeed=state.editor.launchSpeed;toy.editor.launchAngle=state.editor.launchAngle;
    return saveScene(path,toy,message);
}
bool saveScene(const std::filesystem::path& path,const SimulationState& state,std::string& message) {
    try {
        auto temp=path;temp+=".tmp";
        std::ofstream out(temp);out.imbue(std::locale::classic());out<<std::setprecision(9);
        if(!out) throw std::runtime_error("Не удалось открыть файл для записи");
        const auto& w=state.editableWorld();const auto& e=state.editor;
        out<<"MATERIAL_LAB 5\n"<<state.timelineDuration<<'\n'<<int(state.mapType)<<' '<<w.config.airDrag<<'\n';
        out<<w.config.sampleSpacing<<' '<<w.config.ambientTemperature<<' '<<w.config.thermalEnabled<<' '<<w.config.adaptiveDetail<<'\n';
        out<<w.particles.size()<<' '<<w.bonds.size()<<' '<<e.selection.size()<<'\n';
        out<<e.launchSpeed<<' '<<e.launchAngle<<' '<<e.launchPoint.x<<' '<<e.launchPoint.y<<' '<<w.config.gravity<<' '<<state.timeScale<<'\n';
        out<<int(e.layer)<<' '<<int(e.anchorMode)<<' '<<int(e.tool)<<' '<<int(e.selectedMaterial)<<' '<<e.radius<<' '<<e.paintTemperature<<' '<<e.heaterTemperature<<'\n';
        for(auto& p:w.particles) out<<p.position.x<<' '<<p.position.y<<' '<<int(p.material)<<' '<<p.pinned<<' '<<p.untilContact<<' '<<p.worldCell<<' '<<p.temperature<<' '<<p.liquidFraction<<' '<<p.radius<<' '<<p.mass<<' '<<p.refinementLevel<<'\n';
        for(auto& b:w.bonds) out<<b.a<<' '<<b.b<<' '<<b.restLength<<' '<<b.originalLength<<' '<<b.stiffness<<' '<<b.tensileStrength<<' '<<b.compressionStrength<<' '<<b.shearStrength<<' '<<b.accumulatedDamage<<' '<<b.broken<<'\n';
        for(auto id:e.selection) out<<id<<' ';
        out<<'\n';out.close();
        if(!out) throw std::runtime_error("Не удалось записать сцену");
        std::filesystem::rename(temp,path);
        message="Сцена сохранена: "+path.string();return true;
    } catch(const std::filesystem::filesystem_error&) {
        message="Не удалось сохранить сцену. Проверьте путь и доступ к папке.";return false;
    } catch(const std::exception& ex) {message=ex.what();return false;}
}
bool loadScene(const std::filesystem::path& path,SimulationState& state,std::string& message) {
    try {
        std::ifstream in(path);in.imbue(std::locale::classic());
        if(!in) throw std::runtime_error("Не удалось открыть файл сцены для чтения");
        auto require=[&](bool condition){if(!condition) throw std::runtime_error("Некорректный файл сцены или неподдерживаемая версия");};
        std::string magic;int version;in>>magic>>version;require(in&&magic=="MATERIAL_LAB"&&(version>=1&&version<=5));
        SimulationState next;
        if(version>=5) {in>>next.timelineDuration;require(in&&std::isfinite(next.timelineDuration)&&next.timelineDuration>=.1f&&next.timelineDuration<=120);}
        if(version>=2) {
            int map;float airDrag;in>>map>>airDrag;
            require(in&&(map==0||map==1)&&std::isfinite(airDrag)&&airDrag>=0&&airDrag<=1);
            next.mapType=MapType(map);next.world.config.airDrag=airDrag;
        }
        if(version>=4) {
            int thermal,detail;in>>next.world.config.sampleSpacing>>next.world.config.ambientTemperature>>thermal>>detail;
            require(in&&(thermal==0||thermal==1)&&(detail==0||detail==1)&&std::isfinite(next.world.config.sampleSpacing)&&next.world.config.sampleSpacing>=.25f&&next.world.config.sampleSpacing<=1&&std::isfinite(next.world.config.ambientTemperature)&&next.world.config.ambientTemperature>=-253&&next.world.config.ambientTemperature<=6000);
            next.world.config.thermalEnabled=thermal!=0;next.world.config.adaptiveDetail=detail!=0;
        }
        std::size_t np,nb,ns;in>>np>>nb>>ns;require(in&&np<=PhysicsConfig::maxParticles&&nb<=np*8&&ns<=np);
        auto& w=next.world;auto& e=next.editor;
        in>>e.launchSpeed>>e.launchAngle>>e.launchPoint.x>>e.launchPoint.y>>w.config.gravity>>next.timeScale;
        require(in&&finite(e.launchPoint)&&std::isfinite(e.launchSpeed)&&e.launchSpeed>=0&&e.launchSpeed<=250&&std::isfinite(e.launchAngle)&&std::abs(e.launchAngle)<=180&&std::isfinite(w.config.gravity)&&std::abs(w.config.gravity)<=30&&std::isfinite(next.timeScale)&&next.timeScale>=.01f&&next.timeScale<=2);
        if(version>=3) {
            int layer,mode,tool,type;in>>layer>>mode>>tool>>type>>e.radius;
            require(in&&layer>=0&&layer<=1&&mode>=0&&mode<=2&&tool>=0&&tool<=(version>=4?6:4)&&type>=0&&type<int(MaterialType::Count)&&std::isfinite(e.radius)&&e.radius>=.6f&&e.radius<=12);
            if(version>=4) {
                in>>e.paintTemperature>>e.heaterTemperature;
                require(in&&std::isfinite(e.paintTemperature)&&e.paintTemperature>=-253&&e.paintTemperature<=6000&&std::isfinite(e.heaterTemperature)&&e.heaterTemperature>=-253&&e.heaterTemperature<=6000);
            }
            e.layer=EditorLayer(layer);e.anchorMode=FreezeMode(mode);e.tool=Tool(tool);e.selectedMaterial=MaterialType(type);
            require(e.layer!=EditorLayer::World||e.tool!=Tool::Select);
        }
        std::unordered_set<std::uint64_t> occupied;
        for(std::size_t i=0;i<np;++i) {
            Vec2 p;int type,pin;in>>p.x>>p.y>>type>>pin;
            require(in&&finite(p)&&std::abs(p.x)<100000&&std::abs(p.y)<100000&&type>=0&&type<(version==1?5:int(MaterialType::Count))&&(pin==0||pin==1));
            float validationSpacing=version>=4?1.f/4096:w.config.sampleSpacing;
            require(occupied.insert(gridKey(int(std::lround(p.x/validationSpacing)),int(std::lround(p.y/validationSpacing)))).second);
            int held=0,worldCell=0;
            if(version>=3) {in>>held>>worldCell;require(in&&(held==0||held==1)&&(worldCell==0||worldCell==1)&&!(pin&&held)&&!(type==int(MaterialType::Bedrock)&&held));}
            auto id=w.addCell(p,MaterialType(type),pin!=0);
            w.particles[id].untilContact=held!=0;w.particles[id].worldCell=worldCell!=0;
            if(version>=4) {
                auto& cell=w.particles[id];in>>cell.temperature>>cell.liquidFraction>>cell.radius>>cell.mass>>cell.refinementLevel;
                require(in&&std::isfinite(cell.temperature)&&cell.temperature>=-253&&cell.temperature<=10000&&std::isfinite(cell.liquidFraction)&&cell.liquidFraction>=0&&cell.liquidFraction<=1&&std::isfinite(cell.radius)&&cell.radius>=PhysicsConfig::particleRadius/4096&&cell.radius<=PhysicsConfig::particleRadius&&std::isfinite(cell.mass)&&cell.mass>0&&cell.mass<=20&&cell.refinementLevel<=10);
                require(cell.material!=MaterialType::Bedrock||cell.liquidFraction==0);
                const auto& m=material(cell.material);
                if(cell.liquidFraction>0&&cell.liquidFraction<1) require(std::abs(cell.temperature-m.meltingPoint)<.02f);
                if(cell.liquidFraction==0) require(cell.temperature<=m.meltingPoint+.02f);
                if(cell.liquidFraction==1) require(cell.temperature>=m.meltingPoint-.02f);
            }
        }
        std::unordered_set<std::uint64_t> pairs;
        for(std::size_t i=0;i<nb;++i) {
            Bond b;int broken;in>>b.a>>b.b>>b.restLength>>b.originalLength>>b.stiffness>>b.tensileStrength>>b.compressionStrength>>b.shearStrength>>b.accumulatedDamage>>broken;
            require(in&&b.a<np&&b.b<np&&b.a!=b.b&&(broken==0||broken==1));
            bool bedrockA=w.particles[b.a].material==MaterialType::Bedrock,bedrockB=w.particles[b.b].material==MaterialType::Bedrock;
            require(bedrockA==bedrockB);
            if(bedrockA) require(broken==0&&b.accumulatedDamage==0);
            for(float v:{b.restLength,b.originalLength,b.stiffness,b.tensileStrength,b.compressionStrength,b.shearStrength}) require(std::isfinite(v)&&v>0&&v<=100000);
            require(b.restLength<=3&&b.originalLength<=2&&std::isfinite(b.accumulatedDamage)&&b.accumulatedDamage>=0&&b.accumulatedDamage<=1);
            require(pairs.insert(gridKey(int(std::min(b.a,b.b)),int(std::max(b.a,b.b)))).second);
            b.broken=broken!=0;w.bonds.push_back(b);
        }
        std::unordered_set<ParticleId> selected;
        for(std::size_t i=0;i<ns;++i) {ParticleId id;in>>id;require(in&&id<np&&w.particles[id].material!=MaterialType::Bedrock&&selected.insert(id).second);e.selection.push_back(id);}
        std::string trailing;require(!(in>>trailing));
        if(ns) { auto connected=w.component(e.selection.front());require(connected.size()==ns);for(auto id:connected) require(selected.contains(id)); }
        // Commit only after complete validation; errors leave the current scene untouched.
        state=std::move(next);message="Сцена загружена: "+path.string();return true;
    } catch(const std::exception& ex) {message=ex.what();return false;}
}
}
