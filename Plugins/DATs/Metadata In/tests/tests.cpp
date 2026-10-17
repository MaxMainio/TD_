#include "Metadata.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <csignal>
#include <cerrno>

using namespace Metadata;
namespace fs = std::filesystem;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void put(const fs::path& path, const std::string& value) { std::ofstream f(path,std::ios::binary); f << value; }
std::string value(const Result& r, const std::string& key) {
    for (const auto& row:r.rows) if(row.key==key) return row.value;
    return "";
}
Context context(double seconds=10) { return {[] { return false; }, Clock::now()+std::chrono::milliseconds(int(seconds*1000))}; }
std::shared_ptr<const Result> wait(Engine& engine, uint64_t after=0) {
    auto until=Clock::now()+std::chrono::seconds(10);
    while(Clock::now()<until) {
        auto r=engine.result();
        if(r->revision>after && r->status!="reading" && r->status!="idle") return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Engine test timed out");
}
int main(int argc,char** argv) {
 try {
    require(argc==3,"arguments"); fs::path root=fs::absolute(argv[1]); fs::create_directories(root);
    Request r; r.resources=argv[2];
    auto read=[&](const std::string& name,const std::string& data,bool deep=false) {
        r.path=(root/name).string();r.deep=deep;put(r.path,data);return extract(r,context());
    };
    auto obj=read("mesh.obj","# owned test\no triangle\nv -1 2 3\nv 4 -5 6\nv 7 8 -9\nf 1 2 3\n",true);
    require(obj.status=="ready","OBJ ready");
    require(value(obj,"geometry.calculated.vertex_count")=="3","OBJ vertex count");
    require(value(obj,"geometry.calculated.bounds.min_z")=="-9","OBJ bounds");
    auto shallow=extract(Request{r.path,r.resources,false},context());
    require(value(shallow,"geometry.calculated.vertex_count").empty(),"shallow does not derive counts");
    auto csv=read("points.csv","z,x,y,label\n3,1,2,\"a,b\"\n-6,4,5,simple\n",true);
    require(value(csv,"geometry.calculated.point_count")=="2","CSV count");
    require(value(csv,"geometry.calculated.bounds.min_z")=="-6","CSV named coordinate mapping");
    auto spaced=read("spaced.csv"," x, y, z\n1, 2, 3\n",true);
    require(value(spaced,"geometry.calculated.bounds.max_z")=="3","spaced CSV coordinate mapping");
    auto tabs=read("tabs.txt","z\tx\ty\n3\t1\t2\n",true);
    require(value(tabs,"geometry.calculated.bounds.min_x")=="1","tab text coordinate mapping");
    auto lastLine=read("last.xyz","1 2 3",true);
    require(value(lastLine,"geometry.calculated.point_count")=="1","unterminated last line");
    auto xyz=read("points.xyz","1 2 3\n4 5 6\n",true);
    require(value(xyz,"geometry.calculated.bounds.max_x")=="4","XYZ positional mapping");
    auto pts=read("points.pts","2\n1 2 3\n4 5 6\n",true);
    require(value(pts,"geometry.declared_point_count")=="2","PTS declaration");
    auto unknownColumns=read("columns.csv","1,2,3\n",true);
    require(value(unknownColumns,"geometry.calculated.bounds.min_x").empty(),"generic CSV does not guess coordinates");
    std::string ply="ply\nformat ascii 1.0\ncomment splat fixture\nelement vertex 2\nproperty float x\nproperty float y\nproperty float z\nproperty float f_dc_0\nend_header\n1 2 3 0.5\n4 5 6 0.2\n";
    auto p=read("points.ply",ply,true);
    require(value(p,"geometry.elements[0].count")=="2","PLY count");
    require(value(p,"geometry.calculated.bounds.max_z")=="6","PLY bounds");
    require(value(p,"geometry.elements[0].properties[3].name")=="f_dc_0","splat property");
    for(bool big:{false,true}) {
        std::string bytes="ply\nformat "+std::string(big?"binary_big_endian":"binary_little_endian")+" 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n";
        for(uint32_t bits:{0x3f800000u,0x40000000u,0x40400000u}) for(int i=0;i<4;++i) bytes+=char(bits>>(8*(big?3-i:i)));
        auto binary=read(big?"big.ply":"little.ply",bytes,true);
        require(value(binary,"geometry.calculated.bounds.max_z")=="3","binary PLY endian");
    }
    auto bad=read("bad.ply","ply\nformat ascii 1.0\nelement vertex -1\nend_header\n");
    require(bad.status=="partial" && !value(bad,"file.size_bytes").empty(),"malformed retains filesystem");
    auto corrupt=read("bad.csv","x,y,z\n1,2\n",true);
    require(corrupt.status=="partial","bad column count");
    auto strange=read("literal '$` file.xyz","1 2 3\n");
    require(strange.status=="ready","literal unusual filename");
    r.path=root.string();require(extract(r,context()).status=="error","directory rejected");
    r.path=(root/"missing*.png").string();require(extract(r,context()).status=="error","pattern missing rejected");
    auto unsupported=read("unknown.zzzz",std::string(100,'\xff'));
    require(unsupported.status=="partial" && value(unsupported,"file.size_bytes")=="100","unknown fallback");
    r.resources=(root/"absent-readers").string();
    auto missing=read("missing-tools.wav","RIFF\x10\x00\x00\x00WAVE");
    require(missing.status=="partial" && !missing.rows.empty(),"missing readers fallback");
    r.resources=argv[2];
    Result formatted;Table table(formatted);
    table.flatten(JSON::parse("{\"array\":[1,2],\"nested\":{\"x\":\"\\u00e9\"},\"large\":18446744073709551615}"),"test","group","test");
    table.add("test.duplicate","a","group","test");table.add("test.duplicate","b","group","test");table.finish();
    require(value(formatted,"test.large")=="18446744073709551615","uint64 precision");
    require(value(formatted,"test.array[1]")=="2","indexed array");
    std::set<std::string> keys;for(const auto& row:formatted.rows) require(keys.insert(row.key).second,"unique keys");
    require(cleanUTF8(std::string("a\xff",2))=="a\xef\xbf\xbd","UTF8 repair");
    Result capped;Table cap(capped);cap.add("huge",std::string(70000,'a'),"g","s");
    for(int i=0;i<21000;++i)cap.add(std::to_string(i),"v","g","s");
    require(capped.rows.size()==20000 && capped.warnings.size()==2,"limits reported");
    auto process=runProcess({"/usr/bin/printf","%s","literal '$`"},context());
    require(process.out=="literal '$`" && process.exitCode==0,"argv literal process");
    bool timed=false;auto start=Clock::now();
    try{runProcess({"/bin/sleep","10"},context(.1));}catch(const std::exception&){timed=true;}
    require(timed && Clock::now()-start<std::chrono::seconds(2),"timeout kills child");
    bool overflow=false;
    try{runProcess({"/usr/bin/yes"},context(5));}catch(const std::exception&){overflow=true;}
    require(overflow,"output limit kills writer");
    Engine engine;r.path=(root/"watch.xyz").string();r.deep=true;r.automatic=true;r.interval=.1;put(r.path,"1 2 3\n");engine.request(r);
    auto first=wait(engine);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(engine.result()->reads==first->reads,"unchanged file cached");
    put(r.path,"4 5 6\n7 8 9\n");auto second=wait(engine,first->revision);
    require(value(*second,"geometry.calculated.point_count")=="2","automatic changed-file read");
    engine.request(r,true);auto forced=wait(engine,second->revision);
    require(forced->reads>second->reads,"refresh bypasses cache");
    r.automatic=false;engine.request(r);put(r.path,"1 1 1\n");std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(engine.result()->reads==forced->reads,"auto refresh off");
    r.path=(root/"second.xyz").string();put(r.path,"9 9 9\n");engine.request(r);
    require(engine.result()->rows.empty(),"old rows cleared synchronously");
    auto switched=wait(engine);require(value(*switched,"file.name")=="second.xyz","latest source");
    fs::remove(r.path);r.automatic=true;engine.request(r);auto deleted=wait(engine,switched->revision);
    require(deleted->status=="error" && deleted->rows.empty(),"deleted file clears data");
    put(r.path,"2 2 2\n");auto recovered=wait(engine,deleted->revision);require(recovered->status=="ready","automatic recovery");
    for(int i=0;i<30;++i){r.path=(root/(i%2?"points.xyz":"second.xyz")).string();engine.request(r,true);}
    auto latest=wait(engine);require(latest->path==r.path,"rapid source changes");
    fs::path slowResources=root/"slow-readers";
    fs::create_directories(slowResources/"exiftool");
    put(slowResources/"versions.json","{\"exiftool\":\"test\",\"ffmpeg\":\"test\"}");
    put(slowResources/"exiftool/exiftool","open(my $f, '>', $ARGV[-1].'.started') or die; print $f $$; close($f); sleep 30;\n");
    r.resources=slowResources.string();r.path=(root/"slow.bin").string();put(r.path,"fixture");r.timeout=.1;
    auto timeoutResult=extract(r,context(.1));
    require(timeoutResult.status=="partial" && value(timeoutResult,"file.name")=="slow.bin","timeout retains filesystem");
    r.timeout=30;
    for (bool destroy : {false,true}) {
        fs::remove(r.path+".started");
        auto dying=std::make_unique<Engine>();dying->request(r);
        const auto deadline=Clock::now()+std::chrono::seconds(3);
        int pid=0;
        while (!pid && Clock::now()<deadline) {
            std::ifstream marker(r.path+".started"); marker>>pid;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(pid>0,"slow child actually started");
        const auto shutdown=Clock::now();
        if (destroy) dying.reset();
        else {
            Request next=r;next.path=(root/"points.xyz").string();dying->request(next);
            require(wait(*dying)->path==next.path,"superseded process discarded");
        }
        require(Clock::now()-shutdown<std::chrono::seconds(2),"worker cancellation responsive");
        require(kill(pid,0)<0 && errno==ESRCH,"cancelled child reaped");
    }
    r.path=(root/"scan.obj").string();r.deep=true;
    std::string scan;for(int i=0;i<10000;++i) scan+="v 1 2 3\n";put(r.path,scan);
    int checks=0;bool cancelled=false;
    try { extract(r,Context{[&]{return ++checks>100;},Clock::now()+std::chrono::seconds(5)}); }
    catch(const Cancelled&){cancelled=true;}
    require(cancelled,"deep scan cancellation");
    std::cout<<"PASS geometry, formatting, limits, processes, caching and lifecycle\n";
    return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
