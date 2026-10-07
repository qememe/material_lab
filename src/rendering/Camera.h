#pragma once
#include "physics/Math.h"
namespace lab {
struct Viewport { float x{},y{},width{},height{}; bool contains(Vec2 p) const {return p.x>=x&&p.x<x+width&&p.y>=y&&p.y<y+height;} };
class Camera {
public:
    Vec2 center{};
    float zoom{12};
    Viewport viewport;
    Vec2 toScreen(Vec2 p) const {return (p-center)*zoom+Vec2{viewport.x+viewport.width*.5f,viewport.y+viewport.height*.5f};}
    Vec2 toWorld(Vec2 p) const {return (p-Vec2{viewport.x+viewport.width*.5f,viewport.y+viewport.height*.5f})/zoom+center;}
    void zoomAt(Vec2 cursor,float wheel) {Vec2 before=toWorld(cursor);zoom=std::clamp(zoom*std::pow(1.15f,wheel),.1f,12000.f);center+=before-toWorld(cursor);}
    void pan(Vec2 screenDelta) {center-=screenDelta/zoom;}
    void reset() {center={};zoom=12;}
};
}
