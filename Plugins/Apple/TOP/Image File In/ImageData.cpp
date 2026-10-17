#include "ImageData.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <cerrno>

namespace ImageFile {
bool Request::sameContent(const Request& o) const { return path==o.path && image==o.image && decode==o.decode; }
size_t imageElements(uint32_t w,uint32_t h,uint32_t c) {
    if(!w || !h || !c || c>4 || w>65536 || h>65536 || uint64_t(w)*h*c>MaxImageBytes/sizeof(float))
        throw std::runtime_error("Image dimensions exceed the 1 GiB decoded-plane limit");
    return size_t(w)*h*c;
}
std::string cleanUTF8(const std::string& text) {
    std::string out;
    for(size_t i=0;i<text.size();) {
        unsigned char c=text[i];
        size_t n=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        bool valid=n && i+n<=text.size();
        for(size_t j=1;valid&&j<n;++j) valid=(static_cast<unsigned char>(text[i+j])&0xc0)==0x80;
        if(valid&&n>=3) { unsigned char b=text[i+1]; valid=!(c==0xe0&&b<0xa0)&&!(c==0xed&&b>=0xa0)&&!(c==0xf0&&b<0x90)&&!(c==0xf4&&b>=0x90); }
        if(!valid) { out+="\xef\xbf\xbd"; ++i; }
        else if(c==0) { out+="\\u0000"; ++i; }
        else { out.append(text,i,n); i+=n; }
    }
    return out;
}
std::string escaped(const std::string& key) {
    std::string s; for(char c:key) { if(c=='.'||c=='['||c==']'||c=='\\') s+='\\'; s+=c; } return s;
}
void Table::warning(const std::string& text) {
    auto s=cleanUTF8(text.substr(0,2048));
    if(result_.warnings.size()<64 && std::find(result_.warnings.begin(),result_.warnings.end(),s)==result_.warnings.end()) result_.warnings.push_back(s);
}
void Table::add(std::string k,std::string v,std::string g,std::string s) {
    if(result_.rows.size()>=20000 || bytes_>=16*1024*1024) {
        if(!capped_) warning("Metadata truncated at 20000 rows or 16 MiB"); capped_=true; return;
    }
    Row row={cleanUTF8(k),cleanUTF8(v),cleanUTF8(g),cleanUTF8(s)};
    size_t bytes=0;
    for(auto& cell:row) {
        if(cell.size()>65536) {
            size_t end=65536;
            while(end && (static_cast<unsigned char>(cell[end])&0xc0)==0x80) --end;
            cell.resize(end); warning("Metadata cell truncated at 64 KiB");
        }
        bytes+=cell.size();
    }
    if(bytes_+bytes>16*1024*1024) { warning("Metadata truncated at 16 MiB"); capped_=true; return; }
    bytes_+=bytes; result_.rows.push_back(std::move(row));
}
void Table::finish() {
    std::stable_sort(result_.rows.begin(),result_.rows.end(),[](const Row& a,const Row& b){return a[0]<b[0];});
    std::set<std::string> seen;
    for(auto& row:result_.rows) { auto key=row[0]; size_t n=1; while(!seen.insert(row[0]).second) row[0]=key+"#"+std::to_string(++n); }
    std::sort(result_.rows.begin(),result_.rows.end(),[](const Row& a,const Row& b){return a[0]<b[0];});
}
void orientTopLeft(Plane& p,int orientation) {
    if(orientation<1||orientation>8) throw std::runtime_error("Invalid EXIF orientation");
    if(!p.pixels || p.pixels->size()!=imageElements(p.width,p.height,p.channels)) throw std::runtime_error("Invalid pixel storage");
    const uint32_t w=p.width,h=p.height,c=p.channels;
    const bool swap=orientation>=5;
    const uint32_t ow=swap?h:w,oh=swap?w:h;
    std::vector<float> next(p.pixels->size());
    for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x) {
        uint32_t dx=x,dy=y;
        switch(orientation) {
            case 2:dx=w-1-x;break; case 3:dx=w-1-x;dy=h-1-y;break;
            case 4:dy=h-1-y;break; case 5:dx=y;dy=x;break;
            case 6:dx=h-1-y;dy=x;break; case 7:dx=h-1-y;dy=w-1-x;break;
            case 8:dx=y;dy=w-1-x;break;
        }
        // Convert the oriented top-left raster to TouchDesigner's bottom-left raster.
        std::copy_n(p.pixels->data()+(size_t(y)*w+x)*c,c,next.data()+(size_t(oh-1-dy)*ow+dx)*c);
    }
    p.width=ow;p.height=oh;p.pixels=std::make_shared<const std::vector<float>>(std::move(next));
}
std::string fileIdentity(const std::string& path) {
    struct stat s{};
    if(stat(path.c_str(),&s)) return "missing:"+std::to_string(errno);
    return std::to_string(s.st_dev)+":"+std::to_string(s.st_ino)+":"+std::to_string(s.st_size)+":"+
        std::to_string(s.st_mtimespec.tv_sec)+":"+std::to_string(s.st_mtimespec.tv_nsec)+":"+
        std::to_string(s.st_ctimespec.tv_sec)+":"+std::to_string(s.st_ctimespec.tv_nsec);
}
namespace {
bool needsPreparation(const Result& result,uint32_t mask) {
    for(size_t slot=1;slot<BufferCount;++slot)
        if((mask&(1u<<slot)) && result.planes[slot].available() && !result.planes[slot].present() && result.prepare[slot])return true;
    return false;
}
}
Engine::Engine(Loader loader):loader_(std::move(loader)),worker_([this]{work();}) {}
Engine::~Engine() { stop_=true; ++generation_; wake_.notify_all(); worker_.join(); }
void Engine::request(const Request& r,bool refresh) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed=!request_.sameContent(r);
    const bool scheduling=request_.automatic!=r.automatic || request_.interval!=r.interval;
    const bool demand=request_.auxiliaryMask!=r.auxiliaryMask;
    request_=r;
    if(changed||refresh) {
        ++generation_; pending_=!r.path.empty(); reading_=pending_;
        if(changed||r.path.empty()) {
            auto empty=std::make_shared<Result>(); empty->path=r.path;
            empty->status=r.path.empty()?"idle":"reading"; empty->revision=++revision_; empty->reads=reads_;
            result_=empty; identity_.clear();
        }
    }
    if(changed||refresh||scheduling||demand) wake_.notify_all();
}
std::shared_ptr<const Result> Engine::result() const { std::lock_guard<std::mutex> lock(mutex_); return result_; }
bool Engine::reading() const { std::lock_guard<std::mutex> lock(mutex_); return reading_; }
void Engine::work() {
    std::unique_lock<std::mutex> lock(mutex_);
    while(!stop_) {
        if(!pending_ && !needsPreparation(*result_,request_.auxiliaryMask)) {
            if(request_.automatic&&!request_.path.empty()) {
                double interval=std::isfinite(request_.interval)?std::clamp(request_.interval,.1,86400.):1.;
                if(wake_.wait_for(lock,std::chrono::duration<double>(interval))!=std::cv_status::timeout) continue;
                Request r=request_; uint64_t g=generation_;
                lock.unlock(); auto identity=fileIdentity(r.path); lock.lock();
                if(g!=generation_||stop_) continue;
                if(identity==identity_) continue;
                pending_=true;
            } else { wake_.wait(lock); continue; }
        }
        if(stop_) break;
        auto request=request_; const uint64_t g=generation_;
        const bool prepareOnly=!pending_;
        auto previous=result_;auto identity=identity_;
        pending_=false; reading_=true;
        lock.unlock();
        auto before=fileIdentity(request.path);
        const auto start=std::chrono::steady_clock::now();
        std::shared_ptr<Result> next;
        const bool cached=prepareOnly && before==identity;
        auto cancelled=[this,g]{return stop_||generation_!=g;};
        try {
            if(cached) {
                next=std::make_shared<Result>(*previous);
                for(size_t slot=1;slot<BufferCount;++slot) {
                    if(cancelled())throw std::runtime_error("Cancelled");
                    if(!(request.auxiliaryMask&(1u<<slot)) || !next->prepare[slot] || next->planes[slot].present())continue;
                    try {
                        auto plane=next->prepare[slot](cancelled);
                        if(!plane.present() || plane.width!=next->planes[slot].width || plane.height!=next->planes[slot].height ||
                            plane.channels!=next->planes[slot].channels || plane.pixels->size()!=imageElements(plane.width,plane.height,plane.channels))
                            throw std::runtime_error("Prepared auxiliary does not match its validated descriptor");
                        next->planes[slot]=std::move(plane);next->prepare[slot]={};++next->preparations;
                    } catch(const std::exception& e) {
                        next->planes[slot]={};next->prepare[slot]={};
                        const auto prefix=std::string("outputs.")+Kinds[slot]+".";
                        auto& rows=next->rows;rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Row& row){return row[0].compare(0,prefix.size(),prefix)==0;}),rows.end());
                        Table(*next).warning(std::string(Labels[slot])+": "+e.what());
                    }
                }
            } else next=loader_(request,cancelled);
        }
        catch(const std::exception& e) { next=std::make_shared<Result>(); next->error=cleanUTF8(e.what()); }
        catch(...) { next=std::make_shared<Result>(); next->error="Unexpected image decoder failure"; }
        if(!next) { next=std::make_shared<Result>(); next->error="Decoder returned no result"; }
        auto after=fileIdentity(request.path);
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(cached)next->prepareMs+=elapsed;else next->readMs=elapsed;
        next->status=!next->error.empty()?"error":next->warnings.empty()?"ready":"partial";
        if(next->error.empty()) {
            if(cached) {
                auto& rows=next->rows;
                rows.erase(std::remove_if(rows.begin(),rows.end(),[](const Row& row){return row[0]=="decode.status" || row[0]=="decode.read_ms" || row[0].find("decode.warnings[")==0;}),rows.end());
            }
            Table table(*next);
            table.add("decode.status",next->status,"Decode","Plugin");
            table.add("decode.read_ms",std::to_string(next->readMs),"Decode","Plugin");
            const auto warnings=next->warnings;
            for(size_t i=0;i<warnings.size();++i)table.add("decode.warnings["+std::to_string(i)+"]",warnings[i],"Decode","Plugin");
            table.finish();
        }
        lock.lock();
        if(g!=generation_||stop_) continue;
        if(before!=after) { pending_=true; continue; }
        identity_=after; reading_=false;
        next->path=request.path; next->revision=++revision_;
        if(!cached){next->reads=++reads_;next->sourceRevision=next->revision;}
        if(!next->error.empty()) { next->planes={}; next->prepare={}; next->rows.clear(); }
        result_=std::move(next);
    }
}
}
