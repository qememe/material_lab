#include "UI.h"
#include "app/Units.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace lab {
namespace {
constexpr Color panelColor{30,32,36,255}, textColor{223,226,231,255},muted{137,143,153,255},accent{105,173,246,255};
std::string format(double v,int precision=1) {std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(precision)<<v;auto result=out.str();std::replace(result.begin(),result.end(),'.',',');return result;}
}
UI::UI() {
    font_=GetFontDefault();
    std::vector<int> codepoints;
    for(int cp=32;cp<=255;++cp) codepoints.push_back(cp);
    for(int cp=0x0400;cp<=0x04ff;++cp) codepoints.push_back(cp);
    for(int cp:{0x2013,0x2014,0x2116}) codepoints.push_back(cp);
    const std::filesystem::path appDir=GetApplicationDirectory();
    const std::vector<std::filesystem::path> paths{
        appDir/"assets/fonts/DejaVuSans.ttf", "assets/fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "C:/Windows/Fonts/segoeui.ttf"};
    for(const auto& path:paths) {
        if(!std::filesystem::exists(path)) continue;
        font_=LoadFontEx(path.string().c_str(),32,codepoints.data(),int(codepoints.size()));
        ownsFont_=font_.texture.id!=0&&font_.texture.id!=GetFontDefault().texture.id;
        if(ownsFont_&&font_.glyphs[GetGlyphIndex(font_,0x0416)].value==0x0416) {
            SetTextureFilter(font_.texture,TEXTURE_FILTER_BILINEAR);return;
        }
        if(ownsFont_) UnloadFont(font_);
        ownsFont_=false;
    }
    throw std::runtime_error("Не найден шрифт с кириллицей. Проверьте папку assets/fonts.");
}
UI::~UI() {if(ownsFont_) UnloadFont(font_);}
void UI::text(const std::string& s,float x,float y,float size,Color color) const {DrawTextEx(font_,s.c_str(),{x,y},size,.5f,color);}
void UI::panel(Rectangle r,Color col) const {DrawRectangleRec(r,col);}
bool UI::hovered(Rectangle r) const {return CheckCollisionPointRec({mouse_.x,mouse_.y},r);}
bool UI::button(Rectangle r,const std::string& label,bool selected,bool enabled,const std::string& tip) {
    bool hover=hovered(r);Color bg=selected?Color{48,65,84,255}:(hover&&enabled?Color{57,61,68,255}:Color{41,44,50,255});
    DrawRectangleRec(r,bg);
    if(selected) DrawRectangleLinesEx(r,1,accent);
    float size=14;auto ext=MeasureTextEx(font_,label.c_str(),size,.5f);
    if(ext.x>r.width-12) {size=std::max(10.f,size*(r.width-12)/ext.x);ext=MeasureTextEx(font_,label.c_str(),size,.5f);}
    text(label,r.x+(r.width-ext.x)*.5f,r.y+(r.height-size)*.5f,size,enabled?(selected?accent:textColor):Color{76,90,109,255});
    if(hover&&!tip.empty()) tooltip_=tip;
    return enabled&&hover&&press_;
}
bool UI::toggle(Rectangle r,const std::string& label,bool& value,const std::string& tip) {
    if(button(r,label,value,true,tip)) {value=!value;return true;}return false;
}
bool UI::slider(Rectangle r,float& value,float lo,float hi,bool log,bool enabled) {
    float ratio=log?std::log(value/lo)/std::log(hi/lo):(value-lo)/(hi-lo);ratio=std::clamp(ratio,0.f,1.f);
    Rectangle hit{r.x,r.y-8,r.width,r.height+16};bool changed=false;
    // Press starts capture; dragging outside is handled by a dedicated field id.
    std::string id="slider:"+std::to_string(int(r.x))+":"+std::to_string(int(r.y));
    if(enabled&&press_&&hovered(hit)) activeField_=id;
    if(enabled&&down_&&activeField_==id) {ratio=std::clamp((mouse_.x-r.x)/r.width,0.f,1.f);value=log?lo*std::pow(hi/lo,ratio):lo+(hi-lo)*ratio;changed=true;}
    if(release_&&activeField_==id) activeField_.clear();
    DrawRectangleRounded({r.x,r.y+r.height*.5f-2,r.width,4},1,4,{47,62,79,255});
    DrawRectangleRounded({r.x,r.y+r.height*.5f-2,r.width*ratio,4},1,4,enabled?accent:muted);
    DrawCircleV({r.x+r.width*ratio,r.y+r.height*.5f},5,enabled?accent:muted);return changed;
}
bool UI::field(Rectangle r,const std::string& id,std::string& value,bool enabled) {
    bool active=activeField_==id,commit=false;
    if(enabled&&press_&&hovered(r)) {activeField_=id;buffer_=value;active=true;selectAll_=true;}
    else if(active&&press_&&!hovered(r)) {value=buffer_;activeField_.clear();active=false;commit=true;}
    if(active) {
        if((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))&&IsKeyPressed(KEY_A)) selectAll_=true;
        int ch;while((ch=GetCharPressed())>0) if(ch>=32&&ch<=0x10ffff&&!(ch>=0xd800&&ch<=0xdfff)&&(selectAll_||buffer_.size()<220)) {
            if(selectAll_) buffer_.clear();
            selectAll_=false;
            int bytes=0;const char* utf8=CodepointToUTF8(ch,&bytes);buffer_.append(utf8,std::size_t(bytes));
        }
        if(IsKeyPressed(KEY_BACKSPACE)||IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if(selectAll_) buffer_.clear();else if(!buffer_.empty()) {
                auto last=buffer_.size()-1;
                while(last>0&&(static_cast<unsigned char>(buffer_[last])&0xc0)==0x80) --last;
                buffer_.erase(last);
            }
            selectAll_=false;
        }
        if(IsKeyPressed(KEY_ENTER)) {value=buffer_;activeField_.clear();active=false;commit=true;}
        if(IsKeyPressed(KEY_ESCAPE)) {activeField_.clear();active=false;}
    }
    panel(r,{13,20,30,255});DrawRectangleLinesEx(r,1,active?accent:Color{51,66,84,255});
    BeginScissorMode(int(r.x+4),int(r.y),int(r.width-8),int(r.height));
    if(active&&selectAll_) panel({r.x+6,r.y+5,MeasureTextEx(font_,buffer_.c_str(),14,.5f).x+2,r.height-10},{40,79,78,255});
    text(active?buffer_:value,r.x+7,r.y+7,14,enabled?textColor:muted);
    if(active&&int(GetTime()*2)%2==0) {float x=r.x+7+MeasureTextEx(font_,buffer_.c_str(),14,.5f).x;DrawLine(int(x),int(r.y+6),int(x),int(r.y+r.height-6),accent);}
    EndScissorMode();return commit;
}
void UI::number(Rectangle r,const std::string& id,float& value,float lo,float hi,bool enabled,int precision) {
    std::string val=format(value,precision);
    if(field(r,id,val,enabled)) {std::replace(val.begin(),val.end(),',','.');char* end=nullptr;float parsed=std::strtof(val.c_str(),&end);if(end&&*end=='\0'&&end!=val.c_str()&&std::isfinite(parsed)) value=std::clamp(parsed,lo,hi);}
}
void UI::refreshMaps() {
    savedMaps_.clear();mapPage_=0;
    for(const auto& folder:{std::filesystem::path("."),std::filesystem::path("maps")}) {
        std::error_code ec;
        for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)) {
            if(it->is_regular_file(ec)&&it->path().extension()==".scene") savedMaps_.push_back(it->path());
        }
    }
    std::sort(savedMaps_.begin(),savedMaps_.end());
}
void UI::refreshToys() {
    toys_.clear();toyPage_=0;std::error_code ec;
    for(std::filesystem::directory_iterator it("toybox",ec),end;!ec&&it!=end;it.increment(ec))
        if(it->is_regular_file(ec)&&it->path().extension()==".toy") toys_.push_back(it->path());
    std::sort(toys_.begin(),toys_.end());
}
UIAction UI::drawToybox(const SimulationState& state) {
    float x=GetScreenWidth()*.5f-260,y=150;
    panel({x-20,y-20,560,520},{21,29,42,255});DrawRectangleLinesEx({x-20,y-20,560,520},1,accent);
    text("TOY BOX / КОНСТРУКЦИИ",x,y,20,accent);
    if(button({x+425,y,90,27},"ЗАКРЫТЬ")) {toyboxOpen=false;clearFocus();}
    text("Выделите тело в редакторе и сохраните его сюда.",x,y+45,13,muted);
    field({x,y+72,325,32},"toyName",toyName,state.mode==Mode::Editor);
    if(button({x+335,y+72,180,32},"СОХРАНИТЬ ВЫБОР",false,state.mode==Mode::Editor&&!state.editor.selection.empty())) return UIAction::ToySave;
    text("Добавление доступно в любой карте. Свойства сохраняются.",x,y+120,12,muted);
    auto begin=toyPage_*7,end=std::min(begin+7,toys_.size());
    if(toys_.empty()) text("Библиотека пока пуста.",x,y+165,14,muted);
    for(auto i=begin;i<end;++i) if(button({x,y+155+float(i-begin)*34,515,29},toys_[i].stem().string(),toyPath==toys_[i].string())) toyPath=toys_[i].string();
    if(button({x,y+404,35,25},"<",false,toyPage_>0)) --toyPage_;
    text(std::to_string(toyPage_+1)+" / "+std::to_string(std::max(std::size_t(1),(toys_.size()+6)/7)),x+50,y+410,12,muted);
    if(button({x+133,y+404,35,25},">",false,end<toys_.size())) ++toyPage_;
    if(button({x+190,y+400,325,34},"ДОБАВИТЬ НА КАРТУ",false,state.mode==Mode::Editor&&!toyPath.empty())) return UIAction::ToyPlace;
    BeginScissorMode(int(x),int(y+450),515,35);text(message,x,y+453,12,muted);EndScissorMode();
    return UIAction::None;
}
UIAction UI::drawMenu(bool hasMap,Optimizations& options) {
    mouse_={float(GetMouseX()),float(GetMouseY())};press_=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);release_=IsMouseButtonReleased(MOUSE_BUTTON_LEFT);down_=IsMouseButtonDown(MOUSE_BUTTON_LEFT);tooltip_.clear();
    float width=float(GetScreenWidth()),height=float(GetScreenHeight()),cx=width*.5f;
    panel({0,0,width,height},{13,19,29,255});
    for(int x=0;x<width;x+=60) DrawLine(x,0,x,int(height),{20,29,41,255});
    for(int y=0;y<height;y+=60) DrawLine(0,y,int(width),y,{20,29,41,255});
    auto centered=[&](const char* line,float y,float size,Color color){text(line,cx-MeasureTextEx(font_,line,size,.5f).x*.5f,y,size,color);};
    centered("MATERIAL LAB  /  V2",70,38,accent);centered("ТЕРМОМЕХАНИКА / ЛАБОРАТОРИЯ МАТЕРИАЛОВ",122,14,muted);
    if(settingsOpen_) {
        centered("НАСТРОЙКИ",180,26,textColor);
        if(button({cx-360,225,190,36},"ОПТИМИЗАЦИЯ",optimizationPage_)) optimizationPage_=true;
        if(button({cx+170,225,190,36},"НАЗАД")) {
            if(optimizationPage_) optimizationPage_=false;
            else settingsOpen_=false;
            clearFocus();return UIAction::None;
        }
        if(!optimizationPage_) {centered("Выберите раздел настроек.",310,16,muted);return UIAction::None;}
        struct Entry {const char* name;const char* description;bool* value;};
        Entry entries[]{
            {"Спящий грунт","Пропуск поиска контактов между спокойными ячейками грунта.",&options.sleepingTerrain},
            {"Кэш связей","Повторное использование соседей и связных частей тел.",&options.cachedBonds},
            {"Быстрый таймлайн","Один проход записи изменений и кэш последнего прочитанного кадра.",&options.bufferedTimeline},
            {"Фоновый расчёт","Расчёт таймлайна в отдельном потоке для отзывчивого интерфейса.",&options.backgroundCalculation},
            {"Кэш изображения грунта","Готовые слои неподвижного грунта до изменения сцены или камеры.",&options.cachedTerrainDrawing},
            {"Кэш поверхности расплава","Повторное использование поверхности, пока её данные не изменились.",&options.cachedLiquidDrawing}
        };
        bool changed=false;
        for(int i=0;i<6;++i) {
            float y=290+i*76.f;Rectangle row{cx-360,y,720,65};
            if(button(row,"")) {*entries[i].value=!*entries[i].value;changed=true;}
            Rectangle check{cx-343,y+12,22,22};DrawRectangleLinesEx(check,1,*entries[i].value?accent:muted);
            if(*entries[i].value) {
                DrawLineEx({check.x+4,check.y+11},{check.x+9,check.y+17},2,accent);
                DrawLineEx({check.x+9,check.y+17},{check.x+18,check.y+5},2,accent);
            }
            text(entries[i].name,cx-305,y+10,17,textColor);
            text(entries[i].description,cx-305,y+37,12,muted);
        }
        centered("Настройки сохраняются автоматически. Изменение пересоздаёт расчёт карты.",775,13,muted);
        return changed?UIAction::ApplyOptimizations:UIAction::None;
    }
    if(button({25,25,150,32},"НАСТРОЙКИ")) {settingsOpen_=true;optimizationPage_=false;clearFocus();return UIAction::None;}
    UIAction action=UIAction::None;
    for(int i=0;i<2;++i) {
        Rectangle card{cx-380+i*395,180,365,190};bool selected=mapChoice_==MapType(i);
        if(button(card,"",selected)) mapChoice_=MapType(i);
        float x=card.x+22;
        text(i==0?"ПУСТОТА":"ЗЕМЛЯ",x,198,22,selected?accent:textColor);
        if(i==0) {
            text("Вакуум. Без гравитации и пола.",x,241,14,muted);
            text("Свободные столкновения в пространстве.",x,266,13,muted);
            DrawCircleLines(int(card.x+295),int(card.y+146),18,{78,149,195,255});
            DrawRectangle(int(x+30),int(card.y+137),28,16,{64,151,255,255});
            DrawLine(int(x+64),int(card.y+145),int(x+118),int(card.y+145),accent);
        } else {
            text("9,81 м/с². Грунт шириной 1,024 км.",x,241,14,muted);
            text("Неразрушимый бедрок под поверхностью.",x,266,13,muted);
            DrawRectangle(int(x),int(card.y+142),320,15,{155,105,59,255});
            DrawRectangle(int(x),int(card.y+159),320,9,{97,90,115,255});
            DrawRectangle(int(x+150),int(card.y+115),18,18,{64,151,255,255});
        }
    }
    if(button({cx-190,390,380,43},mapChoice_==MapType::Earth?"СОЗДАТЬ КАРТУ «ЗЕМЛЯ»":"СОЗДАТЬ КАРТУ «ПУСТОТА»",true)) action=mapChoice_==MapType::Earth?UIAction::CreateEarth:UIAction::CreateVoid;
    if(hasMap) {
        if(button({cx-190,451,380,38},"ПРОДОЛЖИТЬ ТЕКУЩУЮ КАРТУ")) action=UIAction::Continue;
        centered("Создание новой карты заменит текущую. Сохраните изменения заранее.",510,12,muted);
    } else centered("Выберите карту или загрузите сохранение ниже.",467,14,muted);
    panel({cx-380,550,760,260},panelColor);text("ЗАГРУЗИТЬ СОХРАНЁННУЮ КАРТУ",cx-360,570,15,accent);
    field({cx-360,600,510,34},"menuPath",scenePath);
    if(button({cx+160,600,200,34},"ЗАГРУЗИТЬ КАРТУ")) action=UIAction::Load;
    text("Файлы .scene в папке игры и maps",cx-360,650,12,muted);
    if(button({cx+165,643,195,28},"ОБНОВИТЬ СПИСОК")) refreshMaps();
    if(savedMaps_.empty()) text("Сохранений пока нет. Можно указать путь к файлу выше.",cx-360,697,14,muted);
    std::size_t begin=mapPage_*6,end=std::min(begin+6,savedMaps_.size());
    for(std::size_t i=begin;i<end;++i) {
        auto& path=savedMaps_[i];auto n=i-begin;
        if(button({cx-360+float(n%2)*365,685+float(n/2)*35,355,29},path.filename().string(),scenePath==path.string())) scenePath=path.string();
    }
    if(savedMaps_.size()>6) {
        if(button({cx-360,790,32,22},"<",false,mapPage_>0)) --mapPage_;
        text(std::to_string(mapPage_+1)+" / "+std::to_string((savedMaps_.size()+5)/6),cx-314,793,12,muted);
        if(button({cx-240,790,32,22},">",false,end<savedMaps_.size())) ++mapPage_;
    }
    BeginScissorMode(int(cx-380),823,760,30);text(message,cx-360,832,12,muted);EndScissorMode();
    if(button({width-120,25,95,32},"ВЫХОД")) action=UIAction::Quit;
    return action;
}
UIAction UI::draw(SimulationState& s,Camera& camera,DebugOptions& debug) {
    mouse_={float(GetMouseX()),float(GetMouseY())};press_=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);release_=IsMouseButtonReleased(MOUSE_BUTTON_LEFT);down_=IsMouseButtonDown(MOUSE_BUTTON_LEFT);tooltip_.clear();
    float width=float(GetScreenWidth()),height=float(GetScreenHeight());bool edit=s.mode==Mode::Editor;UIAction action=UIAction::None;
    bool modal=toyboxOpen,modalPress=press_,modalDown=down_,modalRelease=release_;
    if(modal) {press_=false;down_=false;release_=false;}
    panel({0,0,width,64},panelColor);DrawLine(0,63,int(width),63,{45,59,77,255});
    text("Material Lab",20,12,23,textColor);text("V2 / ТЕРМОМЕХАНИКА",21,41,10,muted);
    text(edit?"РЕДАКТОР":s.mode==Mode::Running?"СИМУЛЯЦИЯ":"ПАУЗА",290,24,14,edit?accent:Color{255,189,83,255});
    text("Карта: "+std::string(mapName(s.mapType)),290,44,11,muted);
    if(button({505,16,90,32},"TOY BOX",toyboxOpen,edit)) {toyboxOpen=!toyboxOpen;refreshToys();}
    if(button({410,16,80,32},"В МЕНЮ",false,true,"Вернуться в главное меню; текущая карта сохраняется в памяти.")) action=UIAction::Menu;
    if(button({width-487,16,95,32},"СОХРАНИТЬ",false,true,"Сохранить исходную сцену, связи и настройки запуска.")) action=UIAction::Save;
    if(button({width-382,16,95,32},"ЗАГРУЗИТЬ",false,edit,"Загрузить сцену из файла, указанного слева.")) action=UIAction::Load;
    if(button({width-277,16,100,32},"ОЧИСТИТЬ",false,edit,"Очистить сцену. Для подтверждения нажмите дважды.")) action=UIAction::Clear;
    if(button({width-167,16,72,32},"ОТЛАДКА",debug.enabled,true,"Показать напряжения, связи, нормали и векторы скорости.")) debug.enabled=!debug.enabled;
    if(button({width-85,16,65,32},"СПРАВКА",help)) help=!help;
    panel({0,64,270,height-218},panelColor);DrawLine(269,64,269,int(height-154),{45,59,77,255});
    text("РЕЖИМ РЕДАКТОРА",18,78,12,muted);
    bool canEdit=edit&&!s.editor.drawing();
    if(button({18,97,114,24},"ОБЪЕКТЫ",s.editor.layer==EditorLayer::Objects,canEdit,"Обычные объекты: физика действует сразу после запуска.")) s.editor.layer=EditorLayer::Objects;
    if(button({138,97,114,24},"МИР",s.editor.layer==EditorLayer::World,canEdit,"Редактирование мира: новые ячейки неподвижны до внешнего контакта.")) {
        s.editor.layer=EditorLayer::World;s.editor.selectedMaterial=MaterialType::Soil;
        if(s.editor.tool==Tool::Select) s.editor.tool=Tool::Brush;
    }
    text("ИНСТРУМЕНТЫ",18,134,12,muted);
    constexpr const char* tools[]{"1  КИСТЬ","2  ЛИНИЯ","3  ЛАСТИК","4  ВЫБОР","5  ФИКСАЦИЯ"};
    constexpr const char* tips[]{"Рисуйте ячейки, удерживая левую кнопку мыши. [1]","Протяните мышь, чтобы нарисовать прямую полосу. [2]","Удаляйте ячейки кистью выбранного радиуса. [3]","Нажмите на связную часть и протяните направление запуска. [4]","Кисть фиксации: меняет только ячейки под курсором. [5]"};
    for(int i=0;i<5;++i) if(button({18.f+(i%2)*120.f,153.f+(i/2)*32.f,114,26},tools[i],s.editor.tool==Tool(i),canEdit&&(i!=3||s.editor.layer==EditorLayer::Objects),tips[i])) s.editor.tool=Tool(i);
    if(button({138,217,114,26},"6  НАГРЕВ",s.editor.tool==Tool::Heat,canEdit,"Нагреть участок кистью. Клавиша 7: охлаждение.")) s.editor.tool=Tool::Heat;
    constexpr const char* freezeNames[]{"Снять","Навсегда","До касания"};
    constexpr const char* freezeTips[]{"Снять фиксацию кистью; бедрок остаётся неподвижным.","Абсолютная фиксация: ячейки остаются на месте до ручного снятия.","Ячейки освобождаются при контакте с другим, отдельным объектом."};
    constexpr FreezeMode freezeModes[]{FreezeMode::Absolute,FreezeMode::UntilContact,FreezeMode::Free};
    for(int i=0;i<3;++i) {
        auto mode=freezeModes[i];
        if(button({18.f+i*79,252,76,26},freezeNames[int(mode)],s.editor.anchorMode==mode,canEdit,freezeTips[int(mode)])) {s.editor.anchorMode=mode;s.editor.tool=Tool::Anchor;}
    }
    text("МАТЕРИАЛЫ",18,293,12,muted);
    constexpr const char* names[]{"СТАЛЬ","УРАН (ИГР.)","ПЛАСТИК","ЗАРЯД","ВЗРЫВАТЕЛЬ","ЗЕМЛЯ","БЕДРОК"};
    for(int i=0;i<int(MaterialType::Count);++i) {
        auto type=MaterialType(i);const auto& m=material(type);Rectangle r{18.f+(i%2)*120,314.f+(i/2)*34,114,29};
        if(button(r,"",s.editor.selectedMaterial==type,edit,std::string(m.name)+": "+std::string(m.description))) s.editor.selectedMaterial=type;
        float size=12;float labelWidth=MeasureTextEx(font_,names[i],size,.5f).x;
        if(labelWidth>82) size*=82/labelWidth;
        text(names[i],r.x+27,r.y+(r.height-size)*.5f,size,edit?(s.editor.selectedMaterial==type?accent:textColor):muted);
        DrawRectangleRounded({r.x+7,r.y+8,12,16},.2f,4,{m.color[0],m.color[1],m.color[2],255});
    }
    text("РАДИУС КИСТИ",18,465,11,muted);text(format(s.editor.radius)+" м",184,465,12,textColor);
    slider({22,488,224,14},s.editor.radius,.6f,12,false,edit);
    DrawLine(18,520,252,520,{45,59,77,255});
    text("ЗАПУСК",18,535,12,accent);text("Ячеек: "+std::to_string(s.editor.selection.size()),111,535,12,muted);
    text("СКОРОСТЬ",18,560,11,muted);
    if(button({110,550,65,23},"м/с",!speedKmh_)) speedKmh_=false;
    if(button({182,550,70,23},"км/ч",speedKmh_)) speedKmh_=true;
    float speed=speedKmh_?units::toKmh(s.editor.launchSpeed):s.editor.launchSpeed;
    slider({22,586,143,14},speed,0,speedKmh_?900:250,false,edit);number({179,574,73,30},"speed",speed,0,speedKmh_?900:250,edit);
    if(edit) s.editor.launchSpeed=speedKmh_?units::fromKmh(speed):speed;
    text("Угол / градусы по часовой стрелке",18,619,11,muted);
    slider({22,645,143,14},s.editor.launchAngle,-180,180,false,edit);number({179,633,73,30},"angle",s.editor.launchAngle,-180,180,edit);
    text("Гравитация / м/с²",18,675,11,muted);number({179,670,73,30},"gravity",s.world.config.gravity,0,30,edit,2);
    float fileY=std::max(690.f,height-207);
    text("ФАЙЛ СЦЕНЫ",18,fileY,11,muted);field({18,fileY+20,234,29},"path",scenePath);
    panel({0,height-154,width,154},panelColor);DrawLine(0,int(height-154),int(width),int(height-154),{45,59,77,255});
    // Cached simulation timeline.
    float oldDuration=s.timelineDuration;
    text("ДЛИТ. / с",18,height-141,11,muted);number({91,height-146,65,29},"duration",s.timelineDuration,.1f,120,!s.editor.drawing(),1);
    if(oldDuration!=s.timelineDuration) action=UIAction::Recalculate;
    if(button({166,height-146,130,29},"ПЕРЕСЧИТАТЬ",false,!s.editor.drawing()&&!s.world.particles.empty())) action=UIAction::Recalculate;
    float trackX=320,trackY=height-135,trackWidth=width-605;
    panel({trackX,trackY-4,trackWidth,8},{47,62,79,255});
    float progress=float(s.calculatedSeconds()/s.timelineDuration);
    panel({trackX,trackY-4,trackWidth*std::clamp(progress,0.f,1.f),8},{56,103,131,255});
    float cursor=float(s.timelineCursor/s.timelineDuration);
    DrawLine(int(trackX+trackWidth*cursor),int(trackY-10),int(trackX+trackWidth*cursor),int(trackY+11),accent);
    Rectangle track{trackX,trackY-12,trackWidth,24};
    if(press_&&hovered(track)&&s.hasTimeline()) activeField_="timeline";
    if(down_&&activeField_=="timeline") s.seek(std::clamp(double((mouse_.x-trackX)/trackWidth),0.,1.)*s.timelineDuration);
    if(release_&&activeField_=="timeline") activeField_.clear();
    for(int i=0;i<=10;++i) {float x=trackX+trackWidth*i/10;DrawLine(int(x),int(trackY+6),int(x),int(trackY+10),muted);text(format(s.timelineDuration*i/10,s.timelineDuration<1?2:1),x-8,trackY+14,10,muted);}
    text(format(s.timelineCursor,2)+" / "+format(s.timelineDuration,1)+" с",width-265,height-145,13,accent);
    text(s.timelineFailed()?"РАСЧЁТ ОСТАНОВЛЕН":s.baking()?"РАСЧЁТ "+format(progress*100,0)+"%":s.hasTimeline()?"КЭШ ГОТОВ":"ОЖИДАЕТ РАСЧЁТА",width-265,height-122,11,muted);
    // Bottom control strip.

    float y=height-80;
    if(button({18,y,234,38},edit?"НАЧАТЬ СИМУЛЯЦИЮ":s.mode==Mode::Running?"ПАУЗА":"ПРОДОЛЖИТЬ",true,!edit||!s.world.particles.empty(),"Пробел: запуск / пауза. Выделение задаёт запуск; без него объекты просто падают.")) action=edit?UIAction::Start:UIAction::Pause;
    if(button({280,y,72,33},"СБРОС",false,!edit,"Восстановить исходную сцену и запуск на паузе.")) action=UIAction::Reset;
    if(button({362,y,108,33},"РЕДАКТОР",false,!edit,"Вернуться к исходной конструкции в редакторе.")) action=UIAction::Edit;
    if(button({480,y,62,33},"ШАГ",false,s.mode==Mode::Paused,"Следующий сохранённый кадр (1/30 с); ← / →: соседние кадры.")) action=UIAction::Step;
    text("СКОРОСТЬ ВРЕМЕНИ",566,y-3,10,muted);text(format(s.timeScale,2)+"x",752,y-3,12,accent);
    slider({569,y+18,224,14},s.timeScale,.01f,2,true);
    if(button({819,y,107,33},"ВСЯ СЦЕНА",false,true,"Показать все ячейки в окне. [F]")) action=UIAction::Fit;
    if(button({936,y,107,33},"КАМЕРА 1:1",false,true,"Вернуть исходное положение и масштаб камеры. [Home]")) camera.reset();
    text("КАДРЫ/С "+std::to_string(GetFPS())+"   |   ФИЗИКА "+format(s.stepsPerSecond,0)+" шаг/с   |   ЯЧЕЙКИ "+std::to_string(s.world.activeParticleCount())+"   |   СВЯЗИ "+std::to_string(s.world.liveBondCount())+"   |   ВРЕМЯ "+format(s.world.time,3)+" с",280,height-35,12,muted);
    text(s.baking()||s.timelineFailed()?s.timelineMessage:s.lagging?"Расчёт не успевает: симуляция догоняет накопленное время.":message,18,height-17,10,s.lagging?Color{255,183,83,255}:muted);
    if(s.world.particles.empty()) {
        float cx=camera.viewport.x+camera.viewport.width*.5f,cy=camera.viewport.y+camera.viewport.height*.5f;
        auto centered=[&](const char* line,float y,float size,Color color) {float w=MeasureTextEx(font_,line,size,.5f).x;text(line,cx-w*.5f,y,size,color);};
        centered("СОЗДАЙТЕ ПЕРВОЕ СТОЛКНОВЕНИЕ",cy-36,22,{106,129,153,255});
        centered("Нарисуйте две отдельные конструкции из любых материалов.",cy+1,14,muted);
        centered("Выберите одну, задайте направление и скорость, затем запустите.",cy+28,14,muted);
    }
    if(debug.enabled&&width<1380) {
        float x=width-205;panel({x-12,80,197,186},{21,29,42,235});text("ОТЛАДОЧНЫЙ ВИД",x,93,12,accent);
        toggle({x,116,171,27},"Напряжения",debug.stress);toggle({x,149,171,27},"Связи ячеек",debug.bonds);
        toggle({x,182,171,27},"Векторы скорости",debug.velocities);toggle({x,215,171,27},"Нормали контактов",debug.normals);
    }
    if(width>=1380) {
        float x=width-260;
        panel({x-20,64,280,height-218},panelColor);DrawLine(int(x-20),64,int(x-20),int(height-154),{62,66,74,255});
        const auto& m=material(s.editor.selectedMaterial);
        text("СВОЙСТВА МАТЕРИАЛА",x,82,12,muted);text(std::string(m.name),x,111,17,textColor);
        text("Плавление",x,145,12,muted);text(s.editor.selectedMaterial==MaterialType::Bedrock?"НЕ ПЛАВИТСЯ":format(m.meltingPoint,0)+" °C",x+117,145,13,accent);
        text("Теплопроводность",x,171,12,muted);text(format(m.conductivity,2),x+185,171,12,textColor);
        DrawLine(int(x),195,int(x+240),195,{62,66,74,255});
        text("Начальная температура",x,209,12,muted);
        number({x+130,230,110,30},"paintTemp",s.editor.paintTemperature,-253,6000,canEdit,0);text("°C",x+104,237,13,muted);
        if(button({x,270,115,28},"6  НАГРЕВ",s.editor.tool==Tool::Heat,canEdit,"Кисть задаёт температуру участка и учитывает энергию плавления.")) s.editor.tool=Tool::Heat;
        if(button({x+125,270,115,28},"7  ОХЛАЖДЕНИЕ",s.editor.tool==Tool::Cool,canEdit,"Кисть охлаждает участок до температуры среды.")) s.editor.tool=Tool::Cool;
        text("Нагреватель / °C",x,320,12,muted);number({x+130,310,110,30},"heaterTemp",s.editor.heaterTemperature,-253,6000,canEdit,0);
        text("Среда / °C",x,363,12,muted);number({x+130,353,110,30},"ambientTemp",s.world.config.ambientTemperature,-253,6000,canEdit,0);
        text("Воздух / сопротивление",x,406,11,muted);number({x+160,396,80,30},"airDrag",s.world.config.airDrag,0,1,canEdit,3);
        if(button({x,441,240,28},"ТЕПЛОПЕРЕДАЧА",s.world.config.thermalEnabled,canEdit,"Трение, теплопроводность, охлаждение и фазовые переходы.")) s.world.config.thermalEnabled=!s.world.config.thermalEnabled;
        if(button({x,479,240,28},"АДАПТИВНЫЕ ЧАСТИЦЫ",s.world.config.adaptiveDetail,canEdit,"Детализация расплава с сохранением массы. Внутри спокойных тел частицы крупнее.")) s.world.config.adaptiveDetail=!s.world.config.adaptiveDetail;
        DrawLine(int(x),524,int(x+240),524,{62,66,74,255});text("ОТОБРАЖЕНИЕ",x,540,12,muted);
        if(button({x,565,115,28},"МАТЕРИАЛ",!debug.temperature)) debug.temperature=false;
        if(button({x+125,565,115,28},"ТЕМПЕРАТУРА",debug.temperature)) debug.temperature=true;
        toggle({x,603,240,28},"Показать частицы",debug.particles,"Показать реальную дискретизацию материала.");
        float maxTemp=20,minRadius=.46f;std::size_t liquid=0;
        for(const auto& p:s.world.particles) if(p.active) {maxTemp=std::max(maxTemp,p.temperature);minRadius=std::min(minRadius,p.radius);liquid+=p.liquidFraction>.05f;}
        text("МАКС. ТЕМПЕРАТУРА",x,639,11,muted);text(format(maxTemp,0)+" °C",x,659,22,debug.temperature?Color{255,173,77,255}:textColor);
        text("Частиц расплава: "+std::to_string(liquid),x,690,12,muted);
        text("Мельче исходной: "+format(.46f/minRadius,0)+"x",x,714,12,muted);
        if(debug.enabled&&!s.hasTimeline()) {
            toggle({x,height-64,115,24},"Напряжения",debug.stress);toggle({x+125,height-64,115,24},"Связи",debug.bonds);
        }
    }
    if(!help&&!toyboxOpen) {
        float ruler=std::pow(10.f,std::floor(std::log10(110.f/camera.zoom)));
        if(ruler*camera.zoom<50) ruler*=5;
        float rx=camera.viewport.x+18,ry=camera.viewport.y+camera.viewport.height-18;
        DrawLineEx({rx,ry},{rx+ruler*camera.zoom,ry},2,accent);
        text(format(ruler,ruler<1?3:0)+" м",rx,ry-19,11,accent);
        auto position=camera.toWorld(mouse_);
        if(camera.viewport.contains(mouse_)) text("x "+format(position.x,2)+" м   y "+format(-position.y,2)+" м",rx+180,ry-12,11,muted);
        float x=camera.viewport.x+18,y2=camera.viewport.y+18;
        text(debug.temperature?"ПОЛЕ ТЕМПЕРАТУРЫ / °C":debug.particles?"ДИСКРЕТИЗАЦИЯ МАТЕРИАЛА":"МАТЕРИАЛ / СПЛОШНАЯ ПОВЕРХНОСТЬ",x,y2,11,muted);
        if(debug.temperature) {
            for(int i=0;i<160;++i) {
                float t=float(i)/159;
                DrawRectangle(int(x+i),int(y2+24),1,5,{static_cast<unsigned char>(35+220*std::min(1.f,t*2)),static_cast<unsigned char>(80+160*std::max(0.f,(t-.5f)*2)),static_cast<unsigned char>(170*(1-t)),255});
            }
            text("20",x,y2+35,10,muted);text("1000",x+63,y2+35,10,muted);text("2000+",x+132,y2+35,10,muted);
        }
        if(camera.viewport.contains(mouse_)) if(auto id=s.editor.pick(s.world,camera.toWorld(mouse_))) {
            const auto& p=s.world.particles[*id];
            std::string phase=p.liquidFraction>.99f?"ЖИДКОСТЬ":p.liquidFraction>.01f?"ПЛАВЛЕНИЕ":"ТВЁРДОЕ";
            std::string info=std::string(material(p.material).name)+"  |  "+format(p.temperature,0)+" °C  |  "+phase+"  |  v "+format(length(p.velocity),1)+" м/с ("+format(units::toKmh(length(p.velocity)),0)+" км/ч)";
            float bottom=camera.viewport.y+camera.viewport.height-42;
            panel({x-8,bottom-7,MeasureTextEx(font_,info.c_str(),12,.5f).x+18,28},{30,32,36,240});text(info,x,bottom,12,textColor);
        }
    }
    if(help) {
        float x=std::max(285.f,width*.5f-240),y2=110;panel({x-20,y2-20,505,476},{21,29,42,250});DrawRectangleLinesEx({x-20,y2-20,505,476},1,{65,85,108,255});
        text("MATERIAL LAB / СПРАВКА",x,y2,20,accent);
        constexpr const char* lines[]{"1. Рисуйте тела; соседние частицы связаны.","2. Выберите тело и задайте направление запуска.","3. Температура материала задаётся справа.","4. Фиксация: кисть «Навсегда» или «До касания».","5. Нагрев / охлаждение кистью: клавиши 6 / 7.","","Колесо: масштаб. Средняя кнопка: перемещение.","1–7: инструменты. Ctrl+Z / Ctrl+Y: отмена / повтор.","Пробел: запуск / пауза. F: вся сцена. Home: камера.","Ctrl+S / Ctrl+O: сохранить / загрузить. Esc: отмена.","","Расплав теряет связи и образует вязкие капли.","Трение превращает потерянную энергию в тепло.","Коэффициенты и фазовые переходы — игровые."};
        for(int i=0;i<14;++i) text(lines[i],x,y2+47+i*25,14,i>=12?Color{255,191,87,255}:textColor);
        if(button({x+375,y2+395,90,30},"ЗАКРЫТЬ")) help=false;
    }
    if(toyboxOpen) {press_=modalPress;down_=modalDown;release_=modalRelease;auto toyAction=drawToybox(s);if(toyAction!=UIAction::None) action=toyAction;}
    if(!tooltip_.empty()) {
        float size=12,w=MeasureTextEx(font_,tooltip_.c_str(),size,.5f).x+20;float x=std::clamp(mouse_.x+15,4.f,std::max(4.f,width-w-4));float y2=std::min(mouse_.y+22,height-45);
        panel({x,y2,w,28},{8,14,22,245});DrawRectangleLinesEx({x,y2,w,28},1,{65,85,108,255});text(tooltip_,x+10,y2+7,size,textColor);
    }
    return action;
}
}
