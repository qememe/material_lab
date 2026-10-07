#include "Map.h"
namespace lab {
void createMap(PhysicsWorld& world,MapType type) {
    world=PhysicsWorld{};
    if(type==MapType::Void) return;
    world.config.gravity=MapConfig::earthGravity;world.config.airDrag=.015f;world.config.sampleSpacing=.25f;
    for(int y=0;y<MapConfig::soilDepth+MapConfig::bedrockDepth;++y)
        for(int x=-MapConfig::groundHalfWidth;x<MapConfig::groundHalfWidth;++x) {
            auto id=world.addCell({float(x),float(MapConfig::surfaceY+y)},y<MapConfig::soilDepth?MaterialType::Soil:MaterialType::Bedrock);
            auto& p=world.particles[id];p.worldCell=true;
            if(p.material!=MaterialType::Bedrock) p.setFreeze(FreezeMode::UntilContact);
        }
    world.rebuildBonds();
}
}
