#include "FaceData.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace FaceDetection
{
namespace
{
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool valid(const Box& b)
{
    return finite({b.x,b.y}) && finite({b.width,b.height}) && b.width > 0 && b.height > 0
        && finite({b.x+b.width,b.y+b.height});
}
double cross(Point a, Point b, Point c)
{ return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x); }
std::vector<Point> hull(std::vector<Point> points)
{
    points.erase(std::remove_if(points.begin(),points.end(),[](Point p){return !finite(p);}),points.end());
    std::sort(points.begin(),points.end(),[](Point a,Point b){return std::tie(a.x,a.y)<std::tie(b.x,b.y);});
    points.erase(std::unique(points.begin(),points.end(),[](Point a,Point b){return a.x==b.x && a.y==b.y;}),points.end());
    if (points.size()<3) return {};
    std::vector<Point> result(points.size()*2);
    size_t k=0;
    for (Point p:points)
    {
        while(k>=2 && cross(result[k-2],result[k-1],p)<=0) --k;
        result[k++]=p;
    }
    const size_t lower=k+1;
    for(size_t i=points.size()-1;i>0;--i)
    {
        Point p=points[i-1];
        while(k>=lower && cross(result[k-2],result[k-1],p)<=0) --k;
        result[k++]=p;
    }
    result.resize(k-1);
    return result.size()>=3 ? result : std::vector<Point>{};
}
bool inside(const std::vector<Point>& polygon, Point p)
{
    for(size_t i=0;i<polygon.size();++i)
        if(cross(polygon[i],polygon[(i+1)%polygon.size()],p)<0) return false;
    return true;
}
std::string number(double value)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value;
    return s.str();
}
}

Point imagePoint(const Box& box, Point local)
{ return {box.x + local.x*box.width, box.y + local.y*box.height}; }

size_t imageBytes(uint32_t width, uint32_t height, size_t bytesPerPixel)
{
    if(!width || !height || !bytesPerPixel || size_t(width)>SIZE_MAX/height/bytesPerPixel)
        throw std::invalid_argument("Invalid or oversized image dimensions");
    return size_t(width)*height*bytesPerPixel;
}

std::vector<Face> selectFaces(std::vector<Face> faces, double minimumConfidence, bool largestOnly)
{
    if(!std::isfinite(minimumConfidence)) throw std::invalid_argument("Minimum confidence must be finite");
    minimumConfidence=std::clamp(minimumConfidence,0.0,1.0);
    faces.erase(std::remove_if(faces.begin(),faces.end(),[&](const Face& f){
        return !valid(f.box) || !std::isfinite(f.confidence) || f.confidence<minimumConfidence;
    }),faces.end());
    // Per-frame spatial indices, deliberately not identity/tracking IDs.
    std::stable_sort(faces.begin(),faces.end(),[](const Face& a,const Face& b){
        return std::tie(a.box.x,a.box.y,a.box.width,a.box.height)<std::tie(b.box.x,b.box.y,b.box.width,b.box.height);
    });
    if(largestOnly && faces.size()>1)
    {
        auto largest=std::max_element(faces.begin(),faces.end(),[](const Face& a,const Face& b){
            return a.box.width*a.box.height < b.box.width*b.box.height;
        });
        Face selected=std::move(*largest);
        faces.clear();
        faces.push_back(std::move(selected));
    }
    return faces;
}

std::vector<float> renderMask(const std::vector<Face>& faces, uint32_t width, uint32_t height, MaskShape shape)
{
    if(shape!=MaskShape::Oval && shape!=MaskShape::LandmarkHull && shape!=MaskShape::BoundingBox)
        throw std::invalid_argument("Invalid mask shape");
    std::vector<float> mask(imageBytes(width,height,sizeof(float))/sizeof(float),0);
    for(const Face& face:faces)
    {
        if(!valid(face.box)) continue;
        const auto polygon=shape==MaskShape::LandmarkHull ? hull(face.allPoints) : std::vector<Point>{};
        // A face can be detected without landmarks. Keep its mask via an oval.
        Box bounds=face.box;
        if(!polygon.empty())
        {
            double right=polygon[0].x, top=polygon[0].y;
            bounds.x=right; bounds.y=top;
            for(Point p:polygon)
            { bounds.x=std::min(bounds.x,p.x); bounds.y=std::min(bounds.y,p.y); right=std::max(right,p.x); top=std::max(top,p.y); }
            bounds.width=right-bounds.x; bounds.height=top-bounds.y;
        }
        const auto first=[](double v,uint32_t n){return uint32_t(std::floor(std::clamp(v,0.0,1.0)*n));};
        const auto last=[](double v,uint32_t n){return uint32_t(std::ceil(std::clamp(v,0.0,1.0)*n));};
        const uint32_t x0=first(bounds.x,width), x1=last(bounds.x+bounds.width,width);
        const uint32_t y0=first(bounds.y,height), y1=last(bounds.y+bounds.height,height);
        for(uint32_t y=y0;y<y1;++y) for(uint32_t x=x0;x<x1;++x)
        {
            // Four samples give predictable edge coverage without a blur pass.
            int covered=0;
            for(double dy:{.25,.75}) for(double dx:{.25,.75})
            {
                Point p{(x+dx)/width,(y+dy)/height};
                if(!polygon.empty()) covered+=inside(polygon,p);
                else if(shape==MaskShape::BoundingBox)
                    covered+=p.x>=bounds.x && p.x<=bounds.x+bounds.width && p.y>=bounds.y && p.y<=bounds.y+bounds.height;
                else
                {
                    double u=2*(p.x-bounds.x)/bounds.width-1, v=2*(p.y-bounds.y)/bounds.height-1;
                    covered+=u*u+v*v<=1;
                }
            }
            auto& pixel=mask[size_t(y)*width+x];
            pixel=std::max(pixel,covered*.25f);
        }
    }
    return mask;
}

void InfoTable::assign(const std::vector<Face>& faces)
{
    clear();
    try
    {
        for(size_t i=0;i<faces.size();++i) for(const auto& landmark:faces[i].landmarks)
        {
            if(!finite(landmark.position)) continue;
            if(rows_.size()>=size_t(INT32_MAX-1)) throw std::length_error("Too many landmarks for an Info DAT");
            const auto& f=faces[i];
            rows_.push_back({std::to_string(i),landmark.region,std::to_string(landmark.index),
                number(landmark.position.x),number(landmark.position.y),number(f.confidence),
                number(f.box.x),number(f.box.y),number(f.box.width),number(f.box.height)});
        }
    }
    catch(...) { clear(); throw; }
}

const char* InfoTable::cell(int32_t row,int32_t column) const noexcept
{
    static constexpr const char* headers[]={"face","region","point","u","v","confidence","left","bottom","width","height"};
    if(row<0 || column<0 || column>=columns || row>=rowCount()) return "";
    return row==0 ? headers[column] : rows_[size_t(row-1)][column].c_str();
}
}
