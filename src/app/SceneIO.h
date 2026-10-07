#pragma once
#include "SimulationState.h"
#include <filesystem>
#include <string>
namespace lab {
bool saveScene(const std::filesystem::path& path,const SimulationState& state,std::string& message);
bool loadScene(const std::filesystem::path& path,SimulationState& state,std::string& message);
bool saveConstruction(const std::filesystem::path& path,const SimulationState& state,std::string& message);
}
