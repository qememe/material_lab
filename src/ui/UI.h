#pragma once
#include "rendering/Renderer.h"
#include <string>
#include <filesystem>
namespace lab {
enum class UIAction { None, Save, Load, Clear, Start, Pause, Reset, Edit, Step, Fit, Menu, Continue, CreateVoid, CreateEarth, Quit, Recalculate, ToySave, ToyPlace };
class UI {
public:
    UI();
    ~UI();
    UI(const UI&)=delete;
    UI& operator=(const UI&)=delete;
    std::string scenePath{"scene.scene"};
    std::string message{"Нарисуйте конструкции, выберите связную часть и запустите её."};
    bool help{};
    bool toyboxOpen{};
    std::string toyName{"Конструкция"},toyPath;
    void refreshToys();
    bool textActive() const {return !activeField_.empty();}
    UIAction draw(SimulationState& state,Camera& camera,DebugOptions& debug);
    UIAction drawMenu(bool hasMap);
    void refreshMaps();
    void clearFocus() {activeField_.clear();}
    void text(const std::string& value,float x,float y,float size,Color color) const;
private:
    Font font_{};
    bool ownsFont_{};
    std::string activeField_, buffer_,tooltip_;
    Vec2 mouse_{};
    bool press_{}, release_{}, down_{};
    bool selectAll_{};
    MapType mapChoice_{MapType::Void};
    std::vector<std::filesystem::path> savedMaps_;
    std::size_t mapPage_{};
    std::vector<std::filesystem::path> toys_;
    std::size_t toyPage_{};
    bool speedKmh_{};
    UIAction drawToybox(const SimulationState& state);
    void panel(Rectangle rect,Color color) const;
    bool button(Rectangle rect,const std::string& label,bool selected=false,bool enabled=true,const std::string& tip="");
    bool toggle(Rectangle rect,const std::string& label,bool& value,const std::string& tip="");
    bool slider(Rectangle rect,float& value,float min,float max,bool logarithmic=false,bool enabled=true);
    bool field(Rectangle rect,const std::string& id,std::string& value,bool enabled=true);
    void number(Rectangle rect,const std::string& id,float& value,float min,float max,bool enabled=true,int precision=1);
    bool hovered(Rectangle rect) const;
};
}
