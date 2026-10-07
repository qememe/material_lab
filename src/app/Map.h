#pragma once
#include "physics/PhysicsWorld.h"
#include <string_view>
namespace lab {
enum class MapType { Void, Earth };
inline std::string_view mapName(MapType type) {return type==MapType::Earth?"Земля":"Пустота";}
struct MapConfig {
    static constexpr float earthGravity=9.81f; // metres per second squared
    static constexpr int groundHalfWidth=512, surfaceY=18, soilDepth=12, bedrockDepth=2;
};
void createMap(PhysicsWorld& world,MapType type);
}
