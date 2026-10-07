#include "Application.h"
#include "SceneIO.h"
#include <stdexcept>
#include <chrono>
#include <tuple>
namespace lab {
Application::Application() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
    InitWindow(1600,900,"Material Lab | V2 / Термомеханика материалов");
    if(!IsWindowReady()) throw std::runtime_error("Не удалось открыть графическое окно");
    SetWindowMinSize(1380,900);SetTargetFPS(120);SetExitKey(KEY_NULL);
    ui_=std::make_unique<UI>();
    ui_->refreshMaps();
    ui_->message="Выберите тип карты или загрузите сохранение.";
}
Application::~Application() {renderer_.releaseSurface();ui_.reset();CloseWindow();}
void Application::fitScene() {
    if(state_.world.particles.empty()) {camera_.reset();return;}
    Vec2 lo{1e9f,1e9f},hi{-1e9f,-1e9f};
    for(const auto& p:state_.world.particles) if(p.active) {lo.x=std::min(lo.x,p.position.x);lo.y=std::min(lo.y,p.position.y);hi.x=std::max(hi.x,p.position.x);hi.y=std::max(hi.y,p.position.y);}
    if(lo.x>hi.x) return;
    camera_.center=(lo+hi)*.5f;camera_.zoom=std::clamp(std::min(camera_.viewport.width/(hi.x-lo.x+12),camera_.viewport.height/(hi.y-lo.y+12)),.1f,40.f);
}
void Application::action(UIAction a) {
    switch(a) {
    case UIAction::Save: if(!state_.editor.drawing()) saveScene(ui_->scenePath,state_,ui_->message);break;
    case UIAction::Load:
        if((mainMenu_||state_.mode==Mode::Editor)&&!state_.editor.drawing()&&loadScene(ui_->scenePath,state_,ui_->message)) {
            mainMenu_=false;hasMap_=true;ui_->clearFocus();ui_->help=false;fitScene();
        }break;
    case UIAction::Clear:
        if(state_.mode==Mode::Editor) {if(GetTime()<clearConfirmUntil_) {state_.newMap(state_.mapType);ui_->message="Карта очищена; базовый ландшафт восстановлен.";clearConfirmUntil_=0;}else{clearConfirmUntil_=GetTime()+3;ui_->message="Нажмите «Очистить» ещё раз в течение 3 секунд.";}}break;
    case UIAction::Start:
        if(legacyUiTest_) state_.start();
        else if(state_.hasTimeline()) {state_.seek(0);state_.mode=Mode::Running;}
        else state_.calculate();
        ui_->message=legacyUiTest_?"Симуляция запущена.":state_.timelineMessage;break;
    case UIAction::Recalculate:
        state_.calculate(state_.mode!=Mode::Editor);ui_->message=state_.timelineMessage;break;
    case UIAction::Pause:
        if(state_.mode!=Mode::Editor) {
            if(state_.mode==Mode::Paused&&state_.hasTimeline()&&!state_.baking()&&state_.timelineCursor>=state_.calculatedSeconds()) state_.seek(0);
            state_.mode=state_.mode==Mode::Paused?Mode::Running:Mode::Paused;
        }break;
    case UIAction::Reset: state_.reset(false);ui_->message="Исходная сцена восстановлена; запуск подготовлен на паузе.";break;
    case UIAction::Edit: state_.reset(true);ui_->message="Возврат в редактор. Ctrl+Z / Ctrl+Y: отмена / повтор.";break;
    case UIAction::Step: if(state_.mode==Mode::Paused) {if(state_.hasTimeline()) state_.stepFrame();else state_.world.step();}break;
    case UIAction::ToySave: {
        auto name=ui_->toyName;
        if(name.empty()||name=="."||name==".."||name.find_first_of("/\\:*?\"<>|")!=std::string::npos||name.back()=='.'||name.back()==' ') {ui_->message="Введите имя без символов / \\ : * ? и точки в конце.";break;}
        try {
            std::filesystem::create_directories("toybox");
            auto utf8Path=[](const std::string& value){return std::filesystem::path(std::u8string(value.begin(),value.end()));};
            auto path=std::filesystem::path("toybox")/utf8Path(name+".toy");
            // Preserve existing library entries by giving repeated names a suffix.
            for(unsigned n=2;std::filesystem::exists(path);++n) path=std::filesystem::path("toybox")/utf8Path(name+" ("+std::to_string(n)+").toy");
            if(saveConstruction(path,state_,ui_->message)) {ui_->toyPath=path.string();ui_->refreshToys();}
        } catch(const std::exception& ex) {ui_->message=ex.what();}
        break;
    }
    case UIAction::ToyPlace: {
        SimulationState toy;
        if(state_.mode==Mode::Editor&&loadScene(ui_->toyPath,toy,ui_->message)) {toyPreview_=std::move(toy.world);ui_->toyboxOpen=false;ui_->clearFocus();ui_->message="Нажмите на свободное место карты. Esc: отменить добавление.";}
        break;
    }
    case UIAction::Fit:fitScene();break;
    case UIAction::Menu:
        if(state_.editor.drawing()) state_.editor.cancelStroke(state_.world);
        if(state_.mode==Mode::Running) state_.mode=Mode::Paused;
        mainMenu_=true;ui_->help=false;ui_->toyboxOpen=false;toyPreview_.reset();ui_->clearFocus();ui_->refreshMaps();break;
    case UIAction::Continue: if(hasMap_) {mainMenu_=false;ui_->clearFocus();}break;
    case UIAction::CreateVoid:
    case UIAction::CreateEarth:
        state_.newMap(a==UIAction::CreateEarth?MapType::Earth:MapType::Void);
        mainMenu_=false;hasMap_=true;clearConfirmUntil_=0;camera_.reset();ui_->clearFocus();ui_->help=false;
        ui_->message=a==UIAction::CreateEarth?"Карта «Земля»: рыхлый грунт, бедрок и гравитация. Рисуйте объекты над полом.":"Карта «Пустота»: вакуум, без пола и гравитации.";break;
    case UIAction::Quit:exitRequested_=true;break;
    case UIAction::None:break;
    }
}
void Application::handleInput() {
    Vec2 mouse{float(GetMouseX()),float(GetMouseY())};bool canvas=camera_.viewport.contains(mouse);
    if(mainMenu_) {
        if(!ui_->textActive()&&hasMap_&&IsKeyPressed(KEY_ESCAPE)) action(UIAction::Continue);
        previousMouse_=mouse;return;
    }
    if(renderer_.debug.enabled&&mouse.x>GetScreenWidth()-217&&mouse.y>=80&&mouse.y<=266) canvas=false;
    if(ui_->help) canvas=false;
    if(ui_->toyboxOpen) {previousMouse_=mouse;return;}
    if(canvas) {
        camera_.zoomAt(mouse,GetMouseWheelMove());
        if(IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) camera_.pan(mouse-previousMouse_);
    }
    if(!ui_->textActive()) {
        if(toyPreview_) {
            if(IsKeyPressed(KEY_ESCAPE)) {toyPreview_.reset();ui_->message="Добавление отменено.";}
            else if(canvas&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&state_.mode==Mode::Editor) {
                if(state_.editor.insert(state_.world,*toyPreview_,camera_.toWorld(mouse),ui_->message)) {toyPreview_.reset();state_.invalidateTimeline();}
            }
            previousMouse_=mouse;return;
        }
        if(IsKeyPressed(KEY_ESCAPE)&&!state_.editor.drawing()) {action(UIAction::Menu);return;}
        bool ctrl=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL);
        if(IsKeyPressed(KEY_SPACE)) action(state_.mode==Mode::Editor?UIAction::Start:UIAction::Pause);
        if(IsKeyPressed(KEY_F)) fitScene();
        if(IsKeyPressed(KEY_HOME)) camera_.reset();
        if(IsKeyPressed(KEY_F1)) ui_->help=!ui_->help;
        if(IsKeyPressed(KEY_F2)) renderer_.debug.enabled=!renderer_.debug.enabled;
        if(ctrl&&IsKeyPressed(KEY_S)) action(UIAction::Save);
        if(ctrl&&IsKeyPressed(KEY_O)) action(UIAction::Load);
        if(state_.hasTimeline()&&state_.mode!=Mode::Editor) {
            if(IsKeyPressed(KEY_LEFT)) state_.stepFrame(-1);
            if(IsKeyPressed(KEY_RIGHT)) state_.stepFrame();
        }
        if(state_.mode==Mode::Editor) {
            if(ctrl&&IsKeyPressed(KEY_Z)&&state_.editor.undo(state_.world)) state_.invalidateTimeline();
            if(ctrl&&IsKeyPressed(KEY_Y)&&state_.editor.redo(state_.world)) state_.invalidateTimeline();
            if(IsKeyPressed(KEY_ESCAPE)) state_.editor.cancelStroke(state_.world);
            if(!state_.editor.drawing()) for(int i=0;i<7;++i) if((i!=3||state_.editor.layer==EditorLayer::Objects)&&IsKeyPressed(KEY_ONE+i)) state_.editor.tool=Tool(i);
        }
    }
    if(state_.mode==Mode::Editor) {
        Vec2 world=camera_.toWorld(mouse);
        if(canvas&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {strokeMouse_=world;state_.editor.beginStroke(state_.world,world);if(!state_.editor.drawing()) state_.invalidateTimeline();}
        if(state_.editor.drawing()&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&canvas) {strokeMouse_=world;state_.editor.continueStroke(state_.world,world);}
        if(state_.editor.drawing()&&IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {state_.editor.endStroke(state_.world,canvas?world:strokeMouse_);state_.invalidateTimeline();}
        if(state_.world.particles.size()>=PhysicsConfig::maxParticles) ui_->message="Достигнут предел: 80 000 частиц. Удалите часть ячеек ластиком.";
    }
    previousMouse_=mouse;
}
int Application::run(int smokeFrames,const std::string& scene,const std::string& screenshot,bool uiTest,bool earthTest,bool thermalTest,int renderBenchmark,bool workflowTest) {
    legacyUiTest_=uiTest&&!workflowTest;
    if(!scene.empty()) {ui_->scenePath=scene;if(!loadScene(scene,state_,ui_->message)) throw std::runtime_error(ui_->message);mainMenu_=false;hasMap_=true;}
    if(uiTest) {ui_->scenePath=earthTest?"build/earth-validation.scene":"build/ui-validation.scene";smokeFrames=160;}
    if(thermalTest) ui_->scenePath="build/v2-thermal-validation.scene";
    if(renderBenchmark>=0) {
        state_.newMap(MapType::Void);mainMenu_=false;hasMap_=true;smokeFrames=120;SetTargetFPS(0);
        for(int y=0;y<100;++y) for(int x=0;x<100;++x) {
            auto id=state_.world.addCell({(x-50)*.25f,(y-50)*.25f},renderBenchmark==0?MaterialType::Steel:MaterialType::Plastic,false,.115f);
            if(renderBenchmark==1) state_.world.setTemperature(id,500);
        }
        state_.world.rebuildBonds();
    }
    std::size_t workflowCells=0;Particle workflowFrame;
    if(workflowTest) {smokeFrames=180;ui_->toyName="__workflow_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());ui_->scenePath="build/workflow-validation.scene";}
    auto benchmarkStart=std::chrono::steady_clock::now();
    int frames=0;
    while(!WindowShouldClose()&&!exitRequested_) {
        camera_.viewport={270,64,float(GetScreenWidth()-270-(GetScreenWidth()>=1380?280:0)),float(GetScreenHeight()-218)};
        if(frames==0&&!scene.empty()) fitScene();
        if(uiTest) {
            // raylib's public automation API feeds the same mouse/keyboard path as user input.
            // Event ids below are raylib 5.5 INPUT_* ids in rcore.c.
            auto event=[](unsigned int type,int a,int b=0){AutomationEvent e{};e.type=type;e.params[0]=a;e.params[1]=b;PlayAutomationEvent(e);};
            auto move=[&](int x,int y){event(7,x,y);};
            auto click=[&](int x,int y){move(x,y);event(6,MOUSE_BUTTON_LEFT);};
            auto up=[&](){event(5,MOUSE_BUTTON_LEFT);};
            if(workflowTest) switch(frames) {
            case 1:click(800,410);break;case 2:up();break;
            case 4:state_.timelineDuration=.2f;break;
            case 5:click(22,495);break;case 6:up();break;
            case 10:click(620,330);break;case 11:move(660,330);break;case 12:up();break;
            case 15:click(211,560);break;case 16:up();break; // km/h
            case 20:event(2,KEY_FOUR);break;case 21:event(1,KEY_FOUR);break;
            case 22:click(630,330);break;case 23:up();break;
            case 25:click(550,32);break;case 26:up();break;
            case 28:click(965,238);break;case 29:up();break;
            case 32:click(900,566);break;case 33:up();break;
            case 35:click(1070,470);break;case 36:up();break;
            case 40:
                if(toyPreview_||ui_->toyPath.empty()||!std::filesystem::exists(ui_->toyPath)) throw std::runtime_error("Toy Box save / placement failed");
                workflowCells=state_.world.particles.size();
                if(workflowCells<10) throw std::runtime_error("Toy Box did not add construction");
                break;
            case 45:click(230,768);break;case 46:up();break;
            case 75:
                if(!state_.hasTimeline()||state_.baking()||state_.calculatedSeconds()<.19) throw std::runtime_error("Timeline GUI calculation failed");
                break;
            case 80:click(815,765);break;case 81:up();break;
            case 85:
                if(state_.mode!=Mode::Paused||state_.world.time<.09) throw std::runtime_error("Timeline scrub failed");
                workflowFrame=state_.world.particles[0];break;
            case 90:click(320,765);break;case 91:up();break;
            case 95:if(state_.world.time!=0) throw std::runtime_error("Timeline rewind failed");break;
            case 100:click(815,765);break;case 101:up();break;
            case 105:if(length(workflowFrame.position-state_.world.particles[0].position)>1e-6f) throw std::runtime_error("Cached GUI replay differs");break;
            case 110:click(410,835);break;case 111:up();break;
            case 115:event(2,KEY_ONE);break;case 116:event(1,KEY_ONE);break;
            case 118:click(1000,560);break;case 119:up();break;
            case 125:if(state_.mode!=Mode::Editor||state_.world.particles.size()<=workflowCells||!state_.hasTimeline()) throw std::runtime_error("Editor change did not recalculate timeline");break;
            case 130:click(320,765);break;case 131:up();break;
            case 140:if(state_.world.particles.size()<=workflowCells) throw std::runtime_error("Timeline retained stale edited scene");break;
            case 150:click(230,768);break;case 151:up();break;
            case 160:event(2,KEY_LEFT_CONTROL);event(2,KEY_S);break;case 161:event(1,KEY_LEFT_CONTROL);event(1,KEY_S);break;
            default:break;
            }
            else if(thermalTest) switch(frames) {
            case 1:click(800,410);break;case 2:up();break;
            case 5:click(75,328);break;case 6:up();break;
            case 10:click(600,430);break;case 11:move(640,430);break;case 12:up();break;
            case 15:click(75,362);break;case 16:up();break;
            case 20:event(2,KEY_TWO);break;case 21:event(1,KEY_TWO);break;
            case 25:click(850,310);break;case 26:move(850,550);break;case 27:up();break;
            case 30:click(1390,284);break;case 31:up();break; // heater in inspector
            case 32:click(620,430);break;case 33:up();break;
            case 34:click(1520,579);break;case 35:up();break; // thermal visualization
            case 36:event(2,KEY_FOUR);break;case 37:event(1,KEY_FOUR);break;
            case 38:click(620,430);break;case 39:move(760,430);break;case 40:up();break;
            case 42:click(675,845);break;case 43:up();break;
            case 45:click(120,835);break;case 46:up();break;
            case 110:event(2,KEY_SPACE);break;case 111:event(1,KEY_SPACE);break;
            case 120:event(2,KEY_LEFT_CONTROL);event(2,KEY_S);break;
            case 121:event(1,KEY_LEFT_CONTROL);event(1,KEY_S);break;
            default:break;
            }
            else if(earthTest) switch(frames) {
            case 1:click(1050,240);break;case 2:up();break;
            case 3:click(800,410);break;case 4:up();break;
            case 8:click(75,430);break;case 9:up();break; // editor bedrock
            case 10:click(650,500);break;case 11:move(670,500);break;case 12:up();break;
            case 15:click(75,328);break;case 16:up();break;
            case 17:click(1100,250);break;case 18:up();break; // separate freely falling body
            case 20:click(935,250);break;case 21:move(950,270);break;case 22:up();break;
            case 25:click(195,109);break;case 26:up();break; // World category defaults to soil
            case 28:click(650,350);break;case 29:move(670,330);break;case 30:up();break;
            case 32:click(22,495);break;case 33:up();break; // smallest fixation brush
            case 34:click(55,265);break;case 35:up();break; // absolute mode
            case 36:click(935,250);break;case 37:up();break;
            case 38:click(132,265);break;case 39:up();break; // until contact
            case 40:click(950,270);break;case 41:up();break;
            case 45:click(120,835);break;case 46:up();break; // gravity, no selected launch
            case 110:event(2,KEY_SPACE);break;case 111:event(1,KEY_SPACE);break;
            case 120:event(2,KEY_LEFT_CONTROL);event(2,KEY_S);break;
            case 121:event(1,KEY_LEFT_CONTROL);event(1,KEY_S);break;
            case 125:event(2,KEY_ESCAPE);break;case 126:event(1,KEY_ESCAPE);break;
            case 130:click(1040,615);break;case 131:up();break; // load saved Earth from main menu
            default:break;
            }
            else switch(frames) {
            case 1:click(800,410);break;case 2:up();break; // create Void from main menu
            case 5:click(195,328);break;case 6:up();break; // dense material
            case 10:click(620,430);break;case 11:move(660,430);break;case 12:up();break;
            case 15:click(75,362);break;case 16:up();break; // plastic
            case 20:event(2,KEY_TWO);break;case 21:event(1,KEY_TWO);break;
            case 25:click(970,310);break;case 26:move(970,590);break;case 27:up();break;
            case 30:event(2,KEY_FOUR);break;case 31:event(1,KEY_FOUR);break;
            case 35:click(640,430);break;case 36:move(800,430);break;case 37:up();break;
            case 40:click(105,590);break;case 41:up();break; // speed slider
            case 42:click(705,845);break;case 43:up();break; // slow time, about 0.25x
            case 45:click(120,835);break;case 46:up();break; // start
            case 110:event(2,KEY_SPACE);break;case 111:event(1,KEY_SPACE);break;
            case 115:event(2,KEY_F2);break;case 116:event(1,KEY_F2);break;
            case 120:event(2,KEY_LEFT_CONTROL);event(2,KEY_S);break;
            case 121:event(1,KEY_LEFT_CONTROL);event(1,KEY_S);break;
            case 130:event(2,KEY_F1);break;case 131:event(1,KEY_F1);break;
            default:break;
            }
            if(earthTest&&frames==119) {
                bool falling=false;const auto& original=state_.editableWorld();
                for(std::size_t i=0;i<state_.world.particles.size();++i) {
                    const auto& p=state_.world.particles[i];
                    if(p.material==MaterialType::Steel&&p.position.y>original.particles[i].position.y+1&&p.velocity.y>1) falling=true;
                    if(p.material==MaterialType::Bedrock&&length(p.position-original.particles[i].position)>1e-6f) throw std::runtime_error("Бедрок сдвинулся в проверке карты");
                    if(original.particles[i].pinned&&length(p.position-original.particles[i].position)>1e-6f) throw std::runtime_error("Абсолютная фиксация сдвинулась");
                    if(p.untilContact&&p.worldCell&&length(p.position-original.particles[i].position)>1e-6f) throw std::runtime_error("Грунт сдвинулся до контакта");
                }
                if(!falling) throw std::runtime_error("Объекты не падают на карте «Земля»");
            }
        }
        handleInput();if(!mainMenu_) state_.advance(uiTest?1.0/60:GetFrameTime(),!uiTest);
        BeginDrawing();ClearBackground({13,19,29,255});
        UIAction requested;
        if(mainMenu_) requested=ui_->drawMenu(hasMap_);
        else {
            renderer_.drawWorld(state_,camera_,{float(GetMouseX()),float(GetMouseY())});
            if(toyPreview_) {
                auto mouse=Vec2{float(GetMouseX()),float(GetMouseY())};
                BeginScissorMode(int(camera_.viewport.x),int(camera_.viewport.y),int(camera_.viewport.width),int(camera_.viewport.height));
                for(auto& p:toyPreview_->particles) {auto rgb=material(p.material).color;auto position=mouse+p.position*camera_.zoom;DrawCircleV({position.x,position.y},std::max(1.f,p.radius*camera_.zoom),{rgb[0],rgb[1],rgb[2],110});}
                EndScissorMode();
            }
            auto settings=[&](){const auto& c=state_.world.config;return std::tuple{state_.editor.launchSpeed,state_.editor.launchAngle,c.gravity,c.airDrag,c.ambientTemperature,c.thermalEnabled,c.adaptiveDetail};};
            auto before=settings();requested=ui_->draw(state_,camera_,renderer_.debug);
            if(state_.mode==Mode::Editor&&settings()!=before) state_.invalidateTimeline();
        }
        EndDrawing();action(requested);
        ++frames;
        if(smokeFrames>0&&frames>=smokeFrames) {
            if(!screenshot.empty()) {Image shot=LoadImageFromScreen();bool ok=ExportImage(shot,screenshot.c_str());UnloadImage(shot);if(!ok) throw std::runtime_error("Не удалось сохранить снимок окна");}
            break;
        }
    }
    if(uiTest) {
        if(workflowTest) {
            if(state_.baking()||state_.timelineFailed()||!std::filesystem::exists(ui_->scenePath)) throw std::runtime_error("Workflow GUI final validation failed");
            SimulationState loaded;std::string message;
            if(!loadScene(ui_->scenePath,loaded,message)||loaded.timelineDuration!=.2f) throw std::runtime_error("Workflow settings save failed");
            std::filesystem::remove(ui_->toyPath);
            TraceLog(LOG_INFO,"WORKFLOW VALIDATION PASSED: metric UI / toybox save-place / calculate / scrub / cached replay / automatic and manual recalculation / save");
            return 0;
        }
        if(earthTest) {
            if(mainMenu_||state_.mapType!=MapType::Earth||state_.mode!=Mode::Editor||state_.world.particles.size()<=1120) throw std::runtime_error("Проверка карты «Земля» и загрузки из меню не пройдена");
            bool absolute=false,temporary=false,landscape=false;
            for(const auto& p:state_.world.particles) {
                absolute|=p.material==MaterialType::Steel&&p.pinned;
                temporary|=p.material==MaterialType::Steel&&p.untilContact;
                landscape|=p.material==MaterialType::Soil&&p.worldCell&&p.untilContact&&p.position.y<MapConfig::surfaceY-3;
            }
            if(!absolute||!temporary||!landscape||state_.editor.layer!=EditorLayer::World) throw std::runtime_error("Проверка кисти фиксации и редактора мира не пройдена");
        } else if(mainMenu_||state_.mode!=Mode::Paused||state_.editor.selection.empty()||state_.world.time<=0||state_.world.particles.size()<100||!std::filesystem::exists(ui_->scenePath)) throw std::runtime_error("Проверка ввода в интерфейсе не пройдена");
        SimulationState reloaded;std::string msg;
        if(!loadScene(ui_->scenePath,reloaded,msg)||(!earthTest&&reloaded.editor.selection.empty())) throw std::runtime_error("Проверка сохранения из интерфейса не пройдена: "+msg);
        if(thermalTest) {
            bool melted=false,hot=false;
            for(const auto& p:state_.world.particles) {melted|=p.liquidFraction>.1f;hot|=p.temperature>300;}
            if(!melted||!hot||!renderer_.debug.temperature||reloaded.editor.heaterTemperature!=1800) throw std::runtime_error("Проверка термомеханики в интерфейсе не пройдена");
        }
        TraceLog(LOG_INFO,"GUI VALIDATION PASSED: paint / line / materials / select / launch / pause / debug / save");
    }
    if(renderBenchmark>=0) {double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-benchmarkStart).count();TraceLog(LOG_INFO,"RENDER BENCHMARK: scene=%d frames=%d fps=%.2f ms/frame=%.2f",renderBenchmark,frames,frames/elapsed,elapsed*1000/frames);}
    return 0;
}
}
