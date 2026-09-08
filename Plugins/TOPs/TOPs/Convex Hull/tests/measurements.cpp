#include "HullEngine.h"
#include "HullInfoDAT.h"
#include "OutputPacking.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Hull;
namespace {
size_t checked = 0;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(double a, double b) { require(std::abs(a-b) <= 1e-9 * std::max({1.,std::abs(a),std::abs(b)}), "measurement mismatch"); }
int64_t turn(Point a, Point b, Point c) {
    return int64_t(b.x-a.x)*(c.y-a.y)-int64_t(b.y-a.y)*(c.x-a.x);
}
// Independent flood fill over individual pixels, not the engine's scanline runs.
std::vector<std::vector<Point>> blobs(const std::vector<float>& input, int w, int h, int source) {
    std::vector<bool> seen(w*h);
    std::vector<std::vector<Point>> result;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        if(seen[y*w+x] || input[(y*w+x)*4+source] <= .5) continue;
        std::vector<Point> points{{x,y}}; seen[y*w+x]=true;
        for(size_t i=0;i<points.size();++i) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            const int nx=points[i].x+dx, ny=points[i].y+dy;
            if(nx<0||nx>=w||ny<0||ny>=h||seen[ny*w+nx]||input[(ny*w+nx)*4+source]<=.5) continue;
            seen[ny*w+nx]=true; points.push_back({nx,ny});
        }
        result.push_back(points);
    }
    return result;
}
// Gift wrapping using all selected pixel centers, independent of monotone chain.
std::vector<Point> boundary(const std::vector<Point>& points) {
    if(points.empty()) return {};
    const Point first=*std::min_element(points.begin(),points.end(),[](Point a,Point b){return a.x!=b.x?a.x<b.x:a.y<b.y;});
    std::vector<Point> result;
    Point current=first;
    do {
        result.push_back(current); Point next=current;
        for(Point p:points) {
            const auto distance=[&](Point v){return int64_t(v.x-current.x)*(v.x-current.x)+int64_t(v.y-current.y)*(v.y-current.y);};
            const auto t=turn(current,next,p);
            if(next==current||t<0||(t==0&&distance(p)>distance(next))) next=p;
        }
        current=next;
    } while(!(current==first));
    return result;
}
Measurement oracle(const std::vector<Point>& points,unsigned source,uint64_t count) {
    Measurement m; m.source=source; m.selectedPixels=points.size(); m.blobCount=count;
    m.left=m.right=points[0].x; m.top=m.bottom=points[0].y;
    for(Point p:points) {
        m.left=std::min(m.left,p.x); m.right=std::max(m.right,p.x);
        m.bottom=std::min(m.bottom,p.y); m.top=std::max(m.top,p.y);
    }
    ++m.right; ++m.top;
    const auto hull=boundary(points); m.vertexCount=hull.size();
    m.centroidX=(double(m.left)+m.right)/2; m.centroidY=(double(m.bottom)+m.top)/2;
    // Area-weighted triangle centroids, rather than the implementation's edge sum.
    double weightedX=0,weightedY=0;
    for(size_t i=1;i+1<hull.size();++i) {
        const double area=std::abs(double(turn(hull[0],hull[i],hull[i+1])))/2;
        m.area+=area;
        weightedX+=area*((double(hull[0].x)+hull[i].x+hull[i+1].x)/3+.5);
        weightedY+=area*((double(hull[0].y)+hull[i].y+hull[i+1].y)/3+.5);
    }
    if(m.area) {m.centroidX=weightedX/m.area;m.centroidY=weightedY/m.area;}
    for(size_t i=0;i<hull.size();++i) {
        const auto a=hull[i],b=hull[(i+1)%hull.size()]; m.perimeter+=std::hypot(b.x-a.x,b.y-a.y);
    }
    return m;
}
void compare(const Measurement& a,const Measurement& b) {
    require(a.source==b.source&&a.selectedPixels==b.selectedPixels&&a.blobCount==b.blobCount&&a.vertexCount==b.vertexCount,"count/source mismatch");
    require(a.left==b.left&&a.right==b.right&&a.bottom==b.bottom&&a.top==b.top,"bounds mismatch");
    close(a.area,b.area);close(a.perimeter,b.perimeter);close(a.centroidX,b.centroidX);close(a.centroidY,b.centroidY);++checked;
}
std::vector<Measurement> expected(const std::vector<float>& data,int w,int h,Settings s) {
    std::vector<Measurement> result;
    const int jobs=s.source==Source::RGBA?4:s.source==Source::RGB?3:1;
    for(int job=0;job<jobs;++job) {
        const unsigned source=jobs==1?unsigned(s.source):unsigned(job+1);
        auto parts=blobs(data,w,h,int(source)-1);
        parts.erase(std::remove_if(parts.begin(),parts.end(),[&](const auto& p){return p.size()<s.minimumArea;}),parts.end());
        if(parts.empty()) continue;
        if(s.grouping==Grouping::LargestBlob) {
            auto largest=std::max_element(parts.begin(),parts.end(),[](const auto& a,const auto& b){return a.size()<b.size();});
            result.push_back(oracle(*largest,source,1));
        } else if(s.grouping==Grouping::Global) {
            std::vector<Point> all;for(const auto& p:parts) all.insert(all.end(),p.begin(),p.end());
            result.push_back(oracle(all,source,parts.size()));
        } else for(const auto& p:parts) result.push_back(oracle(p,source,1));
    }
    return result;
}
void verify(Engine& engine,const std::vector<float>& data,int w,int h,Settings s) {
    engine.process({data.data(),size_t(w),size_t(h),size_t(w*4)},s);
    const auto want=expected(data,w,h,s);const auto& actual=engine.measurements();
    require(want.size()==actual.size(),"row count mismatch");
    InfoTable table;
    for(size_t i=0;i<want.size();++i) {
        compare(actual[i],want[i]);
    }
    for(DATDetail detail:{DATDetail::Basic,DATDetail::Medium,DATDetail::Maximum}) {
        table.assign(actual,w,h,detail);
        require(table.rowCount()==int(want.size()+1),"DAT row count");
        const int columns=detail==DATDetail::Basic?6:detail==DATDetail::Medium?10:15;
        require(table.columnCount()==columns,"DAT detail column count");
        for(size_t i=0;i<want.size();++i) {
            const int row=int(i+1); const auto& m=want[i];
            require(std::stoull(table.cell(row,0))==i,"frame-local id");
            close(std::stod(table.cell(row,2)),(double(m.left)+m.right)/2/w);
            close(std::stod(table.cell(row,3)),(double(m.bottom)+m.top)/2/h);
            close(std::stod(table.cell(row,4)),double(m.right-m.left)/w);
            close(std::stod(table.cell(row,5)),double(m.top-m.bottom)/h);
            if(columns>=10) {
                close(std::stod(table.cell(row,6)),double(m.left)/w);
                close(std::stod(table.cell(row,7)),double(m.top)/h);
                close(std::stod(table.cell(row,8)),double(m.right)/w);
                close(std::stod(table.cell(row,9)),double(m.bottom)/h);
            }
            if(columns==15) {
                require(std::stoull(table.cell(row,10))==m.selectedPixels,"DAT count precision");
                require(std::stoull(table.cell(row,11))==m.blobCount,"DAT blob count");
                require(std::stoull(table.cell(row,12))==m.vertexCount,"DAT vertex count");
                close(std::stod(table.cell(row,13)),m.area);
                close(std::stod(table.cell(row,14)),m.perimeter);
            }
            require(std::string(table.cell(row,columns)).empty(),"hidden columns leak data");
        }
    }
}
void knownGeometry() {
    auto rectangle=measureHull({{1,2},{4,2},{4,6},{1,6}},1,20,1);
    close(rectangle.area,12);close(rectangle.perimeter,14);close(rectangle.centroidX,3);close(rectangle.centroidY,4.5);
    require(rectangle.right-rectangle.left==4&&rectangle.top-rectangle.bottom==5,"inclusive pixel bounds");
    auto triangle=measureHull({{0,0},{6,0},{0,3}},1,3,3);
    close(triangle.area,9);close(triangle.centroidX,2.5);close(triangle.centroidY,1.5);
    auto point=measureHull({{7,9}},1,1,1);close(point.area,0);close(point.perimeter,0);close(point.centroidX,7.5);
    auto line=measureHull({{0,0},{3,4}},1,2,2);close(line.perimeter,10);close(line.area,0);close(line.centroidY,2.5);
    std::vector<Point> irregular{{1,1},{6,1},{7,3},{3,6},{1,4}};
    compare(measureHull(irregular,1,5,1),oracle(irregular,1,1));
    const auto shifted=measureHull({{1000000000,100},{1000000006,100},{1000000000,103}},1,3,3);
    close(shifted.area,9);close(shifted.centroidX,1000000002.5);
    InfoTable table;
    require(table.columnCount()==6&&table.rowCount()==1,"Basic default");
    point.selectedPixels=9007199254740993ULL;
    const std::vector<std::string> headers{"id","source","u","v","width","height","left","top","right","bottom",
        "selected_pixels","blob_count","vertex_count","hull_area","hull_perimeter"};
    for(DATDetail detail:{DATDetail::Maximum,DATDetail::Basic,DATDetail::Medium,DATDetail::Maximum}) {
        table.assign({point},10,10,detail);
        for(int col=0;col<table.columnCount();++col) require(table.cell(0,col)==headers[col],"schema/header order");
        require(std::string(table.cell(-1,0)).empty()&&std::string(table.cell(0,table.columnCount())).empty(),"bounds safety");
        if(detail==DATDetail::Maximum) require(std::string(table.cell(1,10))=="9007199254740993","integer precision loss");
        const int columns=table.columnCount();
        table.clear();require(table.rowCount()==1&&table.columnCount()==columns,"empty table preserves schema");
        try {table.assign({point},0,10,detail);require(false,"invalid dimensions accepted");} catch(const std::invalid_argument&) {}
        require(table.rowCount()==1&&table.columnCount()==columns,"failure preserves schema without stale rows");
    }
    table.assign({rectangle},10,10,DATDetail::Maximum);
    table.setDetail(DATDetail::Basic);
    require(table.rowCount()==1&&table.columnCount()==6,"detail change clears prior snapshot");
}
}
int main() {
    try {
        knownGeometry();Engine engine(0);Settings s;s.source=Source::Red;
        for(unsigned mask=0;mask<512;++mask) {
            std::vector<float> input(3*3*4);for(unsigned i=0;i<9;++i) input[i*4]=(mask>>i)&1;
            for(int group=0;group<3;++group) for(int view=0;view<3;++view) for(int minimum:{1,3}) {
                s.grouping=Grouping(group);s.view=View(view);s.minimumArea=minimum;verify(engine,input,3,3,s);
            }
        }
        Engine parallel; std::vector<float> input(67*65*4);
        // A ring encloses a disconnected center blob: their hulls overlap.
        std::vector<float> ring(9*9*4);
        for(int y=0;y<9;++y) for(int x=0;x<9;++x)
            ring[(y*9+x)*4]=(x==0||x==8||y==0||y==8||(x==4&&y==4));
        s.source=Source::Red;s.minimumArea=1;
        for(int group=0;group<3;++group) for(int view=0;view<3;++view) {
            s.grouping=Grouping(group);s.view=View(view);verify(engine,ring,9,9,s);
        }
        for(size_t i=0;i<input.size();++i) input[i]=(i*17+i/29)%19>12;
        for(int group=0;group<3;++group) for(int view=0;view<3;++view) {
            s.grouping=Grouping(group);s.view=View(view);s.minimumArea=2;s.source=Source::RGBA;
            verify(engine,input,67,65,s);verify(parallel,input,67,65,s);
            require(engine.measurements().size()==parallel.measurements().size(),"parallel row count");
            for(size_t i=0;i<engine.measurements().size();++i) compare(engine.measurements()[i],parallel.measurements()[i]);
        }
        const auto image=engine.process({input.data(),67,65,67*4},s);
        const auto snapshot=engine.measurements();
        std::vector<uint8_t> packed(23*17*4);packImage(image,TD::OP_PixelFormat::RGBA8Fixed,packed.data(),packed.size(),23,17);
        for(size_t i=0;i<snapshot.size();++i) compare(snapshot[i],engine.measurements()[i]);
        try {engine.process({nullptr,67,65,67*4},s);require(false,"invalid input accepted");} catch(const std::invalid_argument&) {}
        require(engine.measurements().empty(),"stale measurements after failure");
        std::fill(input.begin(),input.end(),0);verify(engine,input,67,65,s);
        std::cout<<checked<<" independent geometry/DAT comparisons passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
