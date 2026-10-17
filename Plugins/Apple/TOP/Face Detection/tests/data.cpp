#include "FaceData.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace FaceDetection;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
bool close(double a,double b) { return std::abs(a-b)<1e-10; }
int main()
{
    try
    {
        Face face;
        face.box={.1,.2,.4,.6}; face.confidence=.9;
        Point p=imagePoint(face.box,{.25,.75});
        require(close(p.x,.2) && close(p.y,.65),"face-local to image coordinates");
        face.landmarks={{"left_eye",0,p},{"nose",0,imagePoint(face.box,{.5,.5})}};
        InfoTable table;
        table.assign({face});
        require(table.rowCount()==3 && std::string(table.cell(0,0))=="face" && std::string(table.cell(0,4))=="v","table schema");
        require(std::string(table.cell(1,1))=="left_eye" && close(std::stod(table.cell(1,3)),.2)
            && close(std::stod(table.cell(1,4)),.65),"normalized landmark row");
        require(std::string(table.cell(1,6))=="0.10000000000000001" && close(std::stod(table.cell(1,7)),.2),"box origin");
        require(std::string(table.cell(-1,0)).empty() && std::string(table.cell(5,10)).empty(),"safe DAT bounds");
        table.assign({}); require(table.rowCount()==1,"empty frame clears rows");

        Face left=face,right=face;
        right.box={.7,.3,.2,.2}; right.landmarks={{"nose",0,{.8,.4}}};
        auto selected=selectFaces({right,left},.5,false);
        require(selected.size()==2 && close(selected[0].box.x,.1),"spatial face ordering");
        table.assign(selected);
        require(std::string(table.cell(3,0))=="1","multi-face table indices");
        selected=selectFaces({right,left},.5,true);
        require(selected.size()==1 && close(selected[0].box.width,.4),"largest-face selection");
        right.confidence=.2;
        require(selectFaces({right,left},.5,false).size()==1,"confidence filtering");
        left.box.width=std::numeric_limits<double>::quiet_NaN();
        require(selectFaces({right,left},.5,false).empty(),"invalid detections excluded");

        face.box={0,0,.5,.5};
        face.allPoints={{0,0},{.5,0},{0,.5},{.1,.1},{0,0}};
        auto box=renderMask({face},8,8,MaskShape::BoundingBox);
        for(size_t y=0;y<8;++y) for(size_t x=0;x<8;++x)
            require(box[y*8+x]==float(x<4 && y<4),"bounding box must occupy bottom-left quarter");
        auto oval=renderMask({face},200,200,MaskShape::Oval);
        double area=std::accumulate(oval.begin(),oval.end(),0.0)/40000;
        require(std::abs(area-std::acos(-1.0)/16)<.001,"oval area");
        require(oval[0]==0 && oval[50*200+50]==1 && oval[150*200+50]==0,"oval geometry and vertical orientation");
        auto triangle=renderMask({face},200,200,MaskShape::LandmarkHull);
        area=std::accumulate(triangle.begin(),triangle.end(),0.0)/40000;
        require(std::abs(area-.125)<.001,"landmark convex hull area");
        require(triangle[10*200+10]==1 && triangle[90*200+90]==0,"hull shape differs from box");
        face.allPoints={{0,0},{.1,.1},{.2,.2}};
        require(renderMask({face},200,200,MaskShape::LandmarkHull)==oval,"degenerate landmarks fall back to oval");
        require(renderMask({face,face},200,200,MaskShape::Oval)==oval,"overlaps use a bounded mask union");
        face.box={-.25,-.25,.5,.5};
        auto clipped=renderMask({face},8,8,MaskShape::BoundingBox);
        require(std::accumulate(clipped.begin(),clipped.end(),0.0)==4,"off-image clipping");
        auto empty=renderMask({},8,8,MaskShape::Oval);
        require(std::accumulate(empty.begin(),empty.end(),0.0)==0,"no faces gives a black mask");
        bool rejected=false;
        try { imageBytes(UINT32_MAX,UINT32_MAX,16); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"image byte overflow");
        std::cout<<"Coordinates, face selection, DAT lifecycle, oval/box/hull geometry, clipping and overflow passed\n";
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
