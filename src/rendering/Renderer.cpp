#include "Renderer.h"
#include <numbers>
namespace lab {
namespace {
template<class T> void hashValue(std::uint64_t& key,const T& value) {
    auto bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(T);++i) {key^=bytes[i];key*=1099511628211ULL;}
}
std::uint64_t viewKey(const Camera& c,const DebugOptions& d) {
    std::uint64_t key=1469598103934665603ULL;
    hashValue(key,c.center);hashValue(key,c.zoom);hashValue(key,c.viewport);
    hashValue(key,d.temperature);hashValue(key,d.enabled);hashValue(key,d.stress);return key;
}
bool staticTerrain(const Particle& p) {return p.active&&p.worldCell&&p.inverseMass()==0&&p.liquidFraction<=.55f&&p.refinementLevel==0;}
Vector2 rv(Vec2 p) {return {p.x,p.y};}
void disk(Vector2 center,float radius,Color color) {
    // Sagitta below 0.12 pixels for small circles; retain the original high
    // tessellation for larger ones. Subpixel samples need no 36-sided polygon.
    int segments=radius<=1.5f?8:radius<=6?16:36;
    DrawCircleSector(center,radius,0,360,segments,color);
}
void arrow(Vec2 from,Vec2 to,Color color,float thickness=2) {
    DrawLineEx(rv(from),rv(to),thickness,color);Vec2 n=normalized(to-from),side{-n.y,n.x};
    DrawTriangle(rv(to),rv(to-n*10+side*5),rv(to-n*10-side*5),color);
}
}
Color Renderer::cellColor(const Particle& p,bool stress) {
    auto rgb=material(p.material).color;
    float damage=.45f*p.accumulatedDamage;
    Color c{static_cast<unsigned char>(rgb[0]*(1-damage)),static_cast<unsigned char>(rgb[1]*(1-damage)),static_cast<unsigned char>(rgb[2]*(1-damage)),255};
    if(stress) {float t=std::clamp(p.stress*.35f,0.f,1.f);c={static_cast<unsigned char>(60+195*t),static_cast<unsigned char>(190*(1-t)+55*t),static_cast<unsigned char>(220*(1-t)+35*t),255};}
    else if(p.temperature>80) {float t=std::clamp((p.temperature-80)/1600,.0f,1.f);c.r=static_cast<unsigned char>(c.r*(1-t)+255*t);c.g=static_cast<unsigned char>(c.g*(1-t)+100*t);}
    return c;
}
void Renderer::releaseSurface() {
    if(surfaceTexture_.id) UnloadTexture(surfaceTexture_);
    if(terrainBonds_.id) UnloadRenderTexture(terrainBonds_);
    if(terrainParticles_.id) UnloadRenderTexture(terrainParticles_);
    surfaceTexture_={};terrainBonds_={};terrainParticles_={};terrainValid_=liquidValid_=false;
}
void Renderer::drawLiquidSurface(const SimulationState& s,const Camera& c) const {
    auto key=viewKey(c,debug);
    for(const auto& p:s.world.particles) if(p.active&&(p.liquidFraction>.55f||p.refinementLevel>0)) hashValue(key,p);
    if(s.world.config.optimizations.cachedLiquidDrawing&&liquidValid_&&key==liquidKey_&&surfaceTexture_.id) {
        DrawTexturePro(surfaceTexture_,{0,0,float(surfaceTexture_.width),float(surfaceTexture_.height)},
            {c.viewport.x,c.viewport.y,float(surfaceTexture_.width)*3,float(surfaceTexture_.height)*3},{0,0},0,WHITE);return;
    }
    liquidKey_=key;liquidValid_=s.world.config.optimizations.cachedLiquidDrawing;
    constexpr float cell=3;
    int width=int(std::ceil(c.viewport.width/cell)),height=int(std::ceil(c.viewport.height/cell));
    std::size_t count=std::size_t(width)*height;surfaceField_.assign(count,{});surfacePixels_.resize(count);
    gaussianX_.resize(width);gaussianY_.resize(height);
    bool liquid=false;
    for(const auto& p:s.world.particles) if(p.active&&(p.liquidFraction>.55f||p.refinementLevel>0)) {
        Vec2 pos=(c.toScreen(p.position)-Vec2{c.viewport.x,c.viewport.y})/cell;
        float radius=p.radius*c.zoom/cell,h=std::max(.8f,radius*.8f);
        if(pos.x<-h*3||pos.y<-h*3||pos.x>width+h*3||pos.y>height+h*3) continue;
        liquid=true;
        Color col=cellColor(p,debug.enabled&&debug.stress);
        if(debug.temperature) {float t=std::clamp((p.temperature-20)/2000,0.f,1.f);col={static_cast<unsigned char>(35+220*std::min(1.f,t*2)),static_cast<unsigned char>(80+160*std::max(0.f,(t-.5f)*2)),static_cast<unsigned char>(170*(1-t)),255};}
        int left=std::max(0,int(std::floor(pos.x-h*3))),right=std::min(width-1,int(std::ceil(pos.x+h*3)));
        int top=std::max(0,int(std::floor(pos.y-h*3))),bottom=std::min(height-1,int(std::ceil(pos.y+h*3)));
        float inverse=1/(2*h*h),amplitude=radius*radius*inverse;
        for(int x=left;x<=right;++x) {float dx=x+.5f-pos.x;gaussianX_[x]=amplitude*std::exp(-dx*dx*inverse);}
        for(int y=top;y<=bottom;++y) {float dy=y+.5f-pos.y;gaussianY_[y]=std::exp(-dy*dy*inverse);}
        for(int y=top;y<=bottom;++y) for(int x=left;x<=right;++x) {
            float weight=gaussianX_[x]*gaussianY_[y];
            auto& pixel=surfaceField_[std::size_t(y)*width+x];pixel.density+=weight;pixel.red+=weight*col.r;pixel.green+=weight*col.g;pixel.blue+=weight*col.b;
        }
    }
    if(!liquid) {liquidValid_=false;return;}
    for(std::size_t i=0;i<count;++i) {
        const auto& p=surfaceField_[i];float coverage=std::clamp((p.density-.13f)/.22f,0.f,1.f);coverage=coverage*coverage*(3-2*coverage);
        if(p.density<.00001f) surfacePixels_[i]={0,0,0,0};
        else surfacePixels_[i]={static_cast<unsigned char>(p.red/p.density),static_cast<unsigned char>(p.green/p.density),static_cast<unsigned char>(p.blue/p.density),static_cast<unsigned char>(coverage*255)};
    }
    if(!surfaceTexture_.id||surfaceTexture_.width!=width||surfaceTexture_.height!=height) {
        if(surfaceTexture_.id) UnloadTexture(surfaceTexture_);
        Image image{surfacePixels_.data(),width,height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};surfaceTexture_=LoadTextureFromImage(image);SetTextureFilter(surfaceTexture_,TEXTURE_FILTER_BILINEAR);
    } else UpdateTexture(surfaceTexture_,surfacePixels_.data());
    DrawTexturePro(surfaceTexture_,{0,0,float(width),float(height)},{c.viewport.x,c.viewport.y,float(width)*cell,float(height)*cell},{0,0},0,WHITE);
}
void Renderer::drawWorld(const SimulationState& s,const Camera& c,Vec2 mouse) const {
    const auto& v=c.viewport;
    BeginScissorMode(int(v.x),int(v.y),int(v.width),int(v.height));
    DrawRectangle(int(v.x),int(v.y),int(v.width),int(v.height),{13,19,29,255});
    if(debug.grid) {
        int spacing=c.zoom<6?10:5;
        Vec2 a=c.toWorld({v.x,v.y}),b=c.toWorld({v.x+v.width,v.y+v.height});
        for(int x=int(std::floor(a.x/spacing))*spacing;x<=b.x;x+=spacing) {float px=c.toScreen({float(x),0}).x;DrawLine(int(px),int(v.y),int(px),int(v.y+v.height),x==0?Color{38,54,73,255}:Color{22,31,44,255});}
        for(int y=int(std::floor(a.y/spacing))*spacing;y<=b.y;y+=spacing) {float py=c.toScreen({0,float(y)}).y;DrawLine(int(v.x),int(py),int(v.x+v.width),int(py),y==0?Color{38,54,73,255}:Color{22,31,44,255});}
    }
    std::vector<bool> selected(s.world.particles.size());
    for(auto id:s.editor.selection) if(id<selected.size()) selected[id]=true;
    auto shade=[&](const Particle& p) {
        if(debug.temperature) {
            float t=std::clamp((p.temperature-20)/2000,0.f,1.f);
            return Color{static_cast<unsigned char>(35+220*std::min(1.f,t*2)),static_cast<unsigned char>(80+160*std::max(0.f,(t-.5f)*2)),static_cast<unsigned char>(170*(1-t)),255};
        }
        return cellColor(p,debug.enabled&&debug.stress);
    };
    EndScissorMode();
    bool cachedTerrain=s.world.config.optimizations.cachedTerrainDrawing&&!debug.particles;
    auto terrainBond=[&](const Bond& b){return !b.broken&&staticTerrain(s.world.particles[b.a])&&staticTerrain(s.world.particles[b.b]);};
    if(cachedTerrain) {
        auto key=viewKey(c,debug);int width=GetScreenWidth(),height=GetScreenHeight();hashValue(key,width);hashValue(key,height);
        bool hasTerrain=false;
        for(std::size_t i=0;i<s.world.particles.size();++i) if(staticTerrain(s.world.particles[i])) {hasTerrain=true;hashValue(key,i);hashValue(key,s.world.particles[i]);}
        if(!hasTerrain) {cachedTerrain=false;terrainValid_=false;}
        for(const auto& b:s.world.bonds) if(terrainBond(b)) hashValue(key,b);
        if(cachedTerrain&&(!terrainValid_||key!=terrainKey_)) {
            if(!terrainBonds_.id||terrainBonds_.texture.width!=width||terrainBonds_.texture.height!=height) {
                if(terrainBonds_.id) UnloadRenderTexture(terrainBonds_);
                if(terrainParticles_.id) UnloadRenderTexture(terrainParticles_);
                terrainBonds_=LoadRenderTexture(width,height);terrainParticles_=LoadRenderTexture(width,height);
            }
            cachedTerrain=terrainBonds_.id&&terrainParticles_.id;
            if(cachedTerrain) {
                BeginTextureMode(terrainBonds_);ClearBackground(BLANK);
                for(const auto& b:s.world.bonds) if(terrainBond(b)) {
                    const auto& a=s.world.particles[b.a];const auto& p=s.world.particles[b.b];
                    DrawLineEx(rv(c.toScreen(a.position)),rv(c.toScreen(p.position)),std::max(.6f,2.12f*std::min(a.radius,p.radius)*c.zoom),shade(a));
                }
                EndTextureMode();BeginTextureMode(terrainParticles_);ClearBackground(BLANK);
                for(const auto& p:s.world.particles) if(staticTerrain(p)) {
                    auto pos=c.toScreen(p.position);float size=c.zoom*p.radius*2.12f;
                    if(p.temperature>600&&!debug.temperature) disk(rv(pos),std::max(1.f,size*.9f),Color{255,105,25,35});
                    disk(rv(pos),std::max(.4f,size*.5f),shade(p));
                }
                EndTextureMode();terrainKey_=key;terrainValid_=true;
            }
        }
    } else terrainValid_=false;
    BeginScissorMode(int(v.x),int(v.y),int(v.width),int(v.height));
    auto drawTerrainLayer=[&](RenderTexture2D target) {DrawTextureRec(target.texture,{0,0,float(target.texture.width),-float(target.texture.height)},{0,0},WHITE);};
    if(cachedTerrain) drawTerrainLayer(terrainBonds_);
    // Connected samples form a filled material surface; sampling is a separate view.
    if(!debug.particles) for(const auto& bond:s.world.bonds) if(!bond.broken&&!(cachedTerrain&&terrainBond(bond))) {
        const auto& a=s.world.particles[bond.a];const auto& b=s.world.particles[bond.b];
        if(!a.active||!b.active||a.liquidFraction>.55f||b.liquidFraction>.55f) continue;
        Vec2 p=c.toScreen(a.position),q=c.toScreen(b.position);
        float margin=std::max(a.radius,b.radius)*c.zoom*1.1f;
        if(std::max(p.x,q.x)+margin<v.x||std::min(p.x,q.x)-margin>v.x+v.width||std::max(p.y,q.y)+margin<v.y||std::min(p.y,q.y)-margin>v.y+v.height) continue;
        DrawLineEx(rv(p),rv(q),std::max(.6f,2.12f*std::min(a.radius,b.radius)*c.zoom),shade(a));
    }
    if(debug.enabled&&debug.bonds) for(const auto& bond:s.world.bonds) {
        Vec2 a=c.toScreen(s.world.particles[bond.a].position),b=c.toScreen(s.world.particles[bond.b].position);
        if(!s.world.particles[bond.a].active||!s.world.particles[bond.b].active) continue;
        if(bond.broken) {if(length(a-b)<c.zoom*3) DrawLineEx(rv(a),rv(b),1,{255,83,99,100});}
        else DrawLineEx(rv(a),rv(b),1.5f,{100,210,185,180});
    }
    if(cachedTerrain) drawTerrainLayer(terrainParticles_);
    bool hasSurface=false;
    for(std::size_t i=0;i<s.world.particles.size();++i) {
        const auto& p=s.world.particles[i];if(!p.active) continue;
        hasSurface|=p.liquidFraction>.55f||p.refinementLevel>0;
        Vec2 pos=c.toScreen(p.position);if(pos.x<v.x-30||pos.x>v.x+v.width+30||pos.y<v.y-30||pos.y>v.y+v.height+30) continue;
        float size=c.zoom*p.radius*2.12f; Color col=shade(p);
        if(!((cachedTerrain&&staticTerrain(p)))&&((p.liquidFraction<=.55f&&p.refinementLevel==0)||debug.particles)) {
            if(p.temperature>600&&!debug.temperature) disk(rv(pos),std::max(1.f,size*.9f),Color{255,105,25,35});
            disk(rv(pos),std::max(.4f,size*(debug.particles?.35f:.5f)),col);
        }
        if(selected[i]&&s.mode==Mode::Editor) DrawCircleV(rv(pos),size*.5f,{90,244,197,65});
        bool fixationView=debug.particles||s.editor.tool==Tool::Anchor;
        if(p.pinned&&fixationView&&c.zoom*p.radius>=3&&s.mode==Mode::Editor) DrawCircleV(rv(pos),std::max(1.f,c.zoom*.10f),{10,17,25,255});
        if(p.untilContact&&fixationView&&c.zoom*p.radius>=3&&s.mode==Mode::Editor) DrawCircleV(rv(pos),std::max(1.f,c.zoom*.10f),{255,196,83,255});
        if(p.impactFlash>.1f&&debug.particles) DrawCircleLinesV(rv(pos),size*.5f,{255,231,161,static_cast<unsigned char>(p.impactFlash*200)});
        if(debug.enabled&&debug.velocities&&i%std::max(std::size_t(1),s.world.particles.size()/500)==0&&length(p.velocity)>1) arrow(pos,c.toScreen(p.position+p.velocity*.06f),{125,221,255,170},1);
    }
    if(!debug.particles&&hasSurface) drawLiquidSurface(s,c);
    if(debug.enabled&&debug.normals) for(const auto& contact:s.world.debugContacts) {
        Vec2 p=s.world.particles[contact.a].position;arrow(c.toScreen(p),c.toScreen(p+contact.normal*2),{255,127,175,220},1.5f);
    }
    for(const auto& blast:s.world.blasts) {
        float t=blast.age/.65f;Vec2 p=c.toScreen(blast.center);Color col{255,180,55,static_cast<unsigned char>((1-t)*200)};
        DrawRing(rv(p),std::max(0.f,blast.radius*c.zoom*t-2),std::max(1.f,blast.radius*c.zoom*t),0,360,60,col);
    }
    if(s.mode==Mode::Editor) {
        if(!s.editor.selection.empty()) {
            Vec2 p=c.toScreen(s.editor.launchPoint);Vec2 end=p+normalized(s.editor.launchVelocity())*std::clamp(s.editor.launchSpeed*.9f,35.f,180.f);
            arrow(p,end,{90,244,197,255},3);DrawCircleV(rv(p),4,{90,244,197,255});
        }
        if(v.contains(mouse)) {
            Vec2 pos=c.toScreen(c.toWorld(mouse));
            if(s.editor.tool==Tool::Brush||s.editor.tool==Tool::Line||s.editor.tool==Tool::Eraser||s.editor.tool==Tool::Anchor||s.editor.tool==Tool::Heat||s.editor.tool==Tool::Cool) {
                auto rgb=material(s.editor.selectedMaterial).color;Color col=s.editor.tool==Tool::Eraser?Color{255,100,119,220}:Color{rgb[0],rgb[1],rgb[2],220};
                if(s.editor.tool==Tool::Anchor) col=s.editor.anchorMode==FreezeMode::Absolute?Color{100,210,255,240}:s.editor.anchorMode==FreezeMode::UntilContact?Color{255,196,83,240}:Color{190,200,210,240};
                if(s.editor.tool==Tool::Heat) col={255,130,45,240};
                if(s.editor.tool==Tool::Cool) col={90,180,255,240};
                DrawCircleLinesV(rv(pos),s.editor.radius*c.zoom,col);
                if(s.editor.drawing()&&s.editor.tool==Tool::Line) DrawLineEx(rv(c.toScreen(s.editor.strokeStart())),rv(pos),s.editor.radius*2*c.zoom,Color{rgb[0],rgb[1],rgb[2],80});
            }
        }
    }
    EndScissorMode();
}
}
