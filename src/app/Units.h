#pragma once
namespace lab::units {
// World positions are metres; simulation time is seconds. Legacy scenes retain
// their numeric coordinates and now have an explicit one metre world scale.
constexpr float toKmh(float metresPerSecond) {return metresPerSecond*3.6f;}
constexpr float fromKmh(float kilometresPerHour) {return kilometresPerHour/3.6f;}
}
