#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace lab {
// Append new ids to keep existing saved scenes compatible.
enum class MaterialType : std::uint8_t { Steel, DepletedUranium, Plastic, Explosive, Fuse, Soil, Bedrock, Count };
struct MaterialProperties {
    std::string_view name, description;
    std::array<unsigned char,3> color;
    float density, stiffness, tensileStrength, compressiveStrength, shearStrength;
    float plasticity, fractureToughness, damping, restitution, friction, impactResistance;
    // Synthetic thermal coefficients; temperatures are displayed in degrees C.
    float meltingPoint, heatCapacity, latentHeat, conductivity, viscosity;
    float yieldStrain{}; // Separate elastic limit from ultimate fracture strain.
};
const MaterialProperties& material(MaterialType type);
}
