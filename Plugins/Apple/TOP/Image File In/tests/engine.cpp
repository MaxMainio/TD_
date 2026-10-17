#include "ImageData.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ImageFile;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
template<class F>void until(F f){for(int i=0;i<600;++i){if(f())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("Async test timed out");}
int main(int argc,char** argv){try{
    require(argc==2,"Fixture directory required");std::filesystem::create_directories(argv[1]);auto file=std::filesystem::path(argv[1])/"changing";std::ofstream(file)<<"initial";
    std::atomic<int> calls{0},cancelled{0};std::atomic<bool> slow{false};
    Engine engine([&](const Request& r,const Cancel& stop){++calls;if(slow){for(int n=0;n<200&&!stop();++n)std::this_thread::sleep_for(std::chrono::milliseconds(2));if(stop()){++cancelled;throw std::runtime_error("cancelled");}}
        auto result=std::make_shared<Result>();result->path=r.path;Table table(*result);table.add("image",r.image,"Test","Test");return result;});
    Request r;r.path=file.string();r.automatic=false;engine.request(r);until([&]{return engine.result()->status=="ready";});require(calls==1,"Initial read");
    for(int i=0;i<100;++i)engine.request(r);require(calls==1,"Cache unchanged inputs");
    auto first=engine.result();engine.request(r,true);until([&]{return engine.result()->reads==2;});require(engine.result()->revision>first->revision,"Refresh bypasses cache");
    std::ofstream(file)<<"changed while refresh off";std::this_thread::sleep_for(std::chrono::milliseconds(150));require(calls==2,"Auto refresh off");
    r.automatic=true;r.interval=.1;engine.request(r);until([&]{return calls>=3&&!engine.reading();});
    slow=true;engine.request(r,true);until([&]{return engine.reading()&&calls>=4;});auto old=engine.result();require(old->status=="ready","Automatic/forced reread keeps completed snapshot");
    r.image="1";engine.request(r);require(engine.result()->status=="reading"&&engine.result()->rows.empty(),"Selection change clears old metadata");
    slow=false;until([&]{return engine.result()->status=="ready"&&engine.result()->rows.back()[0]=="image"&&engine.result()->rows.back()[1]=="1";});require(cancelled>=1,"Superseded read cancelled");
    engine.request(Request{});require(engine.result()->status=="idle"&&!engine.reading(),"Empty path idle");
    {Engine failure([](const Request&,const Cancel&)->std::shared_ptr<Result>{throw std::runtime_error("decode failed");});failure.request(r);until([&]{return failure.result()->status=="error";});require(failure.result()->rows.empty(),"Failure clears result");}
    {
        std::atomic<int> loads{0},prepares{0};
        Engine lazy([&](const Request&,const Cancel&){
            ++loads;auto result=std::make_shared<Result>();
            result->planes[0]={1,1,4,std::make_shared<const std::vector<float>>(std::initializer_list<float>{1,2,3,1})};
            result->planes[2]={1,1,1,{}};
            result->prepare[2]=[&](const Cancel&){++prepares;return Plane{1,1,1,std::make_shared<const std::vector<float>>(1,42)};};
            result->planes[3]={1,1,1,{}};
            result->prepare[3]=[](const Cancel&)->Plane{throw std::runtime_error("Corrupt deferred matte");};
            return result;
        });
        Request request;request.path=file.string();request.automatic=false;request.auxiliaryMask=0;
        lazy.request(request);until([&]{return lazy.result()->status=="ready";});
        auto original=lazy.result();require(prepares==0,"No inactive preparation");
        request.auxiliaryMask=4;lazy.request(request);until([&]{return lazy.result()->planes[2].present();});
        require(loads==1&&prepares==1,"Lazy resume does not read file again");
        require(lazy.result()->planes[0].pixels==original->planes[0].pixels,"Lazy resume shares immutable color storage");
        require(lazy.result()->sourceRevision==original->sourceRevision&&lazy.result()->revision>original->revision,"Preparation preserves source identity and advances publication");
        request.auxiliaryMask=0;lazy.request(request);request.auxiliaryMask=4;lazy.request(request);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));require(prepares==1,"Prepared data reused after suspension");
        request.auxiliaryMask=12;lazy.request(request);until([&]{return lazy.result()->status=="partial";});
        require(lazy.result()->planes[0].present()&&!lazy.result()->planes[3].available(),"Deferred failure retains color and removes failed availability");
        request.auxiliaryMask=0;lazy.request(request,true);until([&]{return lazy.result()->reads==2;});
        require(!lazy.result()->planes[2].present()&&prepares==1,"Refresh validates but does not prepare inactive maps");
    }
    {Engine stop([](const Request&,const Cancel& cancelled){while(!cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));return std::make_shared<Result>();});stop.request(r);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    std::cout<<"PASS caching, refresh, supersession, replacement, empty input, failure and shutdown\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
