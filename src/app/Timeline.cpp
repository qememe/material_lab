#include "Timeline.h"
#include <chrono>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <cstring>
namespace lab {
namespace {
template<class T> void write(std::fstream& file,const T& value) {static_assert(std::is_trivially_copyable_v<T>);file.write(reinterpret_cast<const char*>(&value),sizeof(T));}
template<class T> void read(std::fstream& file,T& value) {static_assert(std::is_trivially_copyable_v<T>);file.read(reinterpret_cast<char*>(&value),sizeof(T));}
template<class T> void writeVector(std::fstream& file,const std::vector<T>& v) {write(file,v.size());file.write(reinterpret_cast<const char*>(v.data()),std::streamsize(v.size()*sizeof(T)));}
template<class T> void readVector(std::fstream& file,std::vector<T>& v) {std::size_t n;read(file,n);if(!file||n>PhysicsConfig::maxParticles*8) throw std::runtime_error("Повреждён временный кэш");v.resize(n);file.read(reinterpret_cast<char*>(v.data()),std::streamsize(n*sizeof(T)));}
template<class T> bool changed(const std::vector<T>& values,const std::vector<T>& base,std::size_t i) {return i>=base.size()||std::memcmp(&values[i],&base[i],sizeof(T))!=0;}
template<class T> std::size_t changeCount(const std::vector<T>& values,const std::vector<T>& base) {std::size_t count=0;for(std::size_t i=0;i<values.size();++i) count+=changed(values,base,i);return count;}
template<class T> void writeDelta(std::fstream& file,const std::vector<T>& values,const std::vector<T>& base,std::size_t count) {
    write(file,values.size());write(file,count);
    for(std::size_t i=0;i<values.size();++i) if(changed(values,base,i)) {write(file,i);write(file,values[i]);}
}
template<class T> void readDelta(std::fstream& file,std::vector<T>& values,const std::vector<T>& base) {
    std::size_t n,count;read(file,n);read(file,count);
    if(!file||n>PhysicsConfig::maxParticles*8||count>n) throw std::runtime_error("Повреждён временный кэш");
    values=base;values.resize(n);
    for(std::size_t i=0;i<count;++i) {std::size_t id;read(file,id);if(!file||id>=n) throw std::runtime_error("Повреждён временный кэш");read(file,values[id]);}
}
}
Timeline::Timeline(const PhysicsWorld& launched,double duration):calculation_(launched),baseParticles_(launched.particles),baseBonds_(launched.bonds),totalFrames_(std::size_t(std::llround(duration*framesPerSecond))) {
    auto root=std::filesystem::temp_directory_path();
    std::random_device random;
    // Atomically reserve a unique directory; no collision with other instances.
    for(int attempt=0;attempt<16;++attempt) {
        auto directory=root/("material-lab-timeline-"+std::to_string(random())+"-"+std::to_string(random()));
        if(std::filesystem::create_directory(directory)) {path_=directory/"frames.bin";break;}
    }
    if(path_.empty()) throw std::runtime_error("Не удалось создать папку кэша");
    file_.open(path_,std::ios::in|std::ios::out|std::ios::binary|std::ios::trunc);
    try {if(!file_) throw std::runtime_error("Не удалось открыть кэш таймлайна");record();}
    catch(...) {file_.close();std::error_code ec;std::filesystem::remove(path_,ec);std::filesystem::remove(path_.parent_path(),ec);throw;}
}
Timeline::~Timeline() {file_.close();std::error_code ec;std::filesystem::remove(path_,ec);std::filesystem::remove(path_.parent_path(),ec);}
void Timeline::record() {
    auto& w=calculation_;
    auto np=changeCount(w.particles,baseParticles_),nb=changeCount(w.bonds,baseBonds_);
    // Each frame references the immutable source, so seeking never decodes
    // preceding frames. Frozen terrain costs only the source arrays in RAM.
    std::uint64_t size=sizeof(PhysicsConfig)+sizeof(WorldStats)+sizeof(double)+6*sizeof(std::size_t)+np*(sizeof(Particle)+sizeof(std::size_t))+nb*(sizeof(Bond)+sizeof(std::size_t))+w.blasts.size()*sizeof(Blast)+w.debugContacts.size()*sizeof(Contact);
    if(bytes_+size>maxBytes) {error="Кэш достиг 1 ГБ. Сократите длительность или число частиц и пересчитайте.";return;}
    file_.clear();file_.seekp(0,std::ios::end);auto offset=file_.tellp();
    write(file_,w.config);write(file_,w.stats);write(file_,w.time);writeDelta(file_,w.particles,baseParticles_,np);writeDelta(file_,w.bonds,baseBonds_,nb);writeVector(file_,w.blasts);writeVector(file_,w.debugContacts);file_.flush();
    if(!file_) throw std::runtime_error("Не удалось записать кэш: проверьте свободное место на диске");
    frames_.push_back({offset});bytes_+=size;
}
void Timeline::advance(bool limitWork) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(8);
    try {
        while(baking()) {
            calculation_.step();
            if(++steps_==stepsPerFrame) {steps_=0;record();}
            if(limitWork&&std::chrono::steady_clock::now()>=deadline) break;
        }
    } catch(const std::exception& ex) {error=ex.what();}
}
void Timeline::restore(std::size_t frame,PhysicsWorld& destination) {
    if(frame>=frames_.size()) throw std::out_of_range("Кадр ещё не рассчитан");
    file_.clear();file_.seekg(frames_[frame].offset);
    PhysicsWorld next;
    read(file_,next.config);read(file_,next.stats);read(file_,next.time);readDelta(file_,next.particles,baseParticles_);readDelta(file_,next.bonds,baseBonds_);readVector(file_,next.blasts);readVector(file_,next.debugContacts);
    if(!file_) throw std::runtime_error("Не удалось прочитать кэш таймлайна");
    destination=std::move(next);
}
}
