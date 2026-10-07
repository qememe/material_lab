#pragma once
namespace lab {
struct Optimizations {
    bool sleepingTerrain{true};
    bool cachedBonds{true};
    bool bufferedTimeline{true};
    bool backgroundCalculation{true};
    bool cachedTerrainDrawing{true};
    bool cachedLiquidDrawing{true};
    bool operator==(const Optimizations&) const = default;
};
}
