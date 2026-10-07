#include "Timeline.h"
#include <chrono>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <cstring>
#include <sstream>
namespace lab {
namespace {
template<class T> void write(std::ostream& file,const T& value) {static_assert(std::is_trivially_copyable_v<T>);file.write(reinterpret_cast<const char*>(&value),sizeof(T));}
template<class T> void read(std::istream& file,T& value) {static_assert(std::is_trivially_copyable_v<T>);file.read(reinterpret_cast<char*>(&value),sizeof(T));}
template<class T> void writeVector(std::ostream& file,const std::vector<T>& v) {write(file,v.size());file.write(reinterpret_cast<const char*>(v.data()),std::streamsize(v.size()*sizeof(T)));}
template<class T> void readVector(std::istream& file,std::vector<T>& v) {std::size_t n;read(file,n);if(!file||n>PhysicsConfig::maxParticles*8) throw std::runtime_error("Повреждён временный кэш");v.resize(n);file.read(reinterpret_cast<char*>(v.data()),std::streamsize(n*sizeof(T)));}
template<class T> bool changed(const std::vector<T>& values,const std::vector<T>& base,std::size_t i) {return i>=base.size()||std::memcmp(&values[i],&base[i],sizeof(T))!=0;}
template<class T> std::size_t changeCount(const std::vector<T>& values,const std::vector<T>& base) {std::size_t count=0;for(std::size_t i=0;i<values.size();++i) count+=changed(values,base,i);return count;}
template<class T> void writeDelta(std::ostream& file,const std::vector<T>& values,const std::vector<T>& base,std::size_t count) {
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
Timeline::~Timeline() {if(worker_.joinable()) {worker_.request_stop();worker_.join();}file_.close();std::error_code ec;std::filesystem::remove(path_,ec);std::filesystem::remove(path_.parent_path(),ec);}
bool Timeline::baking() const {std::lock_guard lock(mutex_);return frames_.size()<=totalFrames_&&error_.empty();}
double Timeline::calculatedSeconds() const {std::lock_guard lock(mutex_);return double(frames_.size()-1)/framesPerSecond;}
std::size_t Timeline::lastFrame() const {std::lock_guard lock(mutex_);return frames_.size()-1;}
std::string Timeline::error() const {std::lock_guard lock(mutex_);return error_;}
void Timeline::record() {
    auto& w=calculation_;
    std::ostringstream buffer(std::ios::out|std::ios::binary);
    write(buffer,w.config);write(buffer,w.stats);write(buffer,w.time);
    auto delta=[&]<class T>(const std::vector<T>& values,const std::vector<T>& base) {
        write(buffer,values.size());auto countPosition=buffer.tellp();std::size_t count=0;write(buffer,count);
        for(std::size_t i=0;i<values.size();++i) if(changed(values,base,i)) {write(buffer,i);write(buffer,values[i]);++count;}
        auto end=buffer.tellp();buffer.seekp(countPosition);write(buffer,count);buffer.seekp(end);
    };
    if(w.config.optimizations.bufferedTimeline) {delta(w.particles,baseParticles_);delta(w.bonds,baseBonds_);}
    else {writeDelta(buffer,w.particles,baseParticles_,changeCount(w.particles,baseParticles_));writeDelta(buffer,w.bonds,baseBonds_,changeCount(w.bonds,baseBonds_));}
    writeVector(buffer,w.blasts);writeVector(buffer,w.debugContacts);
    auto payload=buffer.str();
    std::lock_guard lock(mutex_);
    if(bytes_+payload.size()>maxBytes) {error_="Кэш достиг 1 ГБ. Сократите длительность или число частиц и пересчитайте.";return;}
    file_.clear();file_.seekp(0,std::ios::end);auto offset=file_.tellp();
    file_.write(payload.data(),std::streamsize(payload.size()));
    if(!w.config.optimizations.bufferedTimeline) file_.flush();
    if(!file_) throw std::runtime_error("Не удалось записать кэш: проверьте свободное место на диске");
    frames_.push_back({offset});bytes_+=payload.size();
}
void Timeline::bake(bool limitWork,std::stop_token stop) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(8);
    try {
        while(!stop.stop_requested()&&baking()) {
            if(stop.stop_possible()) {
                std::unique_lock lock(mutex_);
                if(!resume_.wait(lock,stop,[this]{return !paused_;})) break;
            }
            calculation_.step();
            if(++steps_==stepsPerFrame) {steps_=0;record();}
            if(limitWork&&std::chrono::steady_clock::now()>=deadline) break;
        }
    } catch(const std::exception& ex) {std::lock_guard lock(mutex_);error_=ex.what();}
}
void Timeline::pauseCalculation(bool paused) {
    {std::lock_guard lock(mutex_);paused_=paused;}
    resume_.notify_all();
}
void Timeline::advance(bool limitWork) {
    if(!limitWork) pauseCalculation(false);
    if(worker_.joinable()) {if(!limitWork) worker_.join();return;}
    if(!calculation_.config.optimizations.backgroundCalculation||!limitWork) {bake(limitWork);return;}
    worker_=std::jthread([this](std::stop_token stop){bake(false,stop);});
}
void Timeline::restore(std::size_t frame,PhysicsWorld& destination) {
    std::lock_guard lock(mutex_);
    if(restored_&&restoredFrame_==frame) {destination=*restored_;return;}
    if(frame>=frames_.size()) throw std::out_of_range("Кадр ещё не рассчитан");
    file_.clear();file_.seekg(frames_[frame].offset);
    PhysicsWorld next;
    read(file_,next.config);read(file_,next.stats);read(file_,next.time);readDelta(file_,next.particles,baseParticles_);readDelta(file_,next.bonds,baseBonds_);readVector(file_,next.blasts);readVector(file_,next.debugContacts);
    if(!file_) throw std::runtime_error("Не удалось прочитать кэш таймлайна");
    if(next.config.optimizations.bufferedTimeline) {restored_=next;restoredFrame_=frame;}
    else {restored_.reset();restoredFrame_=std::size_t(-1);}
    destination=std::move(next);
}
}
