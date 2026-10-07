#include "app/Application.h"
#include <iostream>
int main(int argc,char** argv) {
    try {
        int frames=0,renderBenchmark=-1;std::string scene,screenshot;bool uiTest=false,earthTest=false,thermalTest=false,workflowTest=false;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--smoke"&&i+1<argc) frames=std::stoi(argv[++i]);
            else if(arg=="--scene"&&i+1<argc) scene=argv[++i];
            else if(arg=="--screenshot"&&i+1<argc) screenshot=argv[++i];
            else if(arg=="--ui-test") uiTest=true;
            else if(arg=="--earth-test") {uiTest=true;earthTest=true;}
            else if(arg=="--workflow-test") {uiTest=true;workflowTest=true;}
            else if(arg=="--thermal-test") {uiTest=true;thermalTest=true;}
            else if(arg=="--render-benchmark"&&i+1<argc) {std::string type=argv[++i];if(type!="solid"&&type!="liquid") throw std::runtime_error("Выберите solid или liquid");renderBenchmark=type=="solid"?0:1;}
            else if(arg=="--help") {std::cout<<"Использование: material_lab [--scene ПУТЬ] [--smoke КАДРЫ] [--screenshot ПУТЬ] [--ui-test] [--earth-test] [--thermal-test] [--workflow-test] [--render-benchmark solid|liquid]\n";return 0;}
            else throw std::runtime_error("Неизвестный параметр: "+arg);
        }
        lab::Application app;return app.run(frames,scene,screenshot,uiTest,earthTest,thermalTest,renderBenchmark,workflowTest);
    }catch(const std::exception& e) {std::cerr<<"Material Lab: "<<e.what()<<'\n';return 1;}
}
