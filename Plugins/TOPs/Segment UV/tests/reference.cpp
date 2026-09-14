#include "SegmentEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

cv::Mat legacyProcess(const cv::Mat&, double, MethodMenuItems, int);
using Segment::DATDetail;
using Segment::InfoTable;
static size_t cases = 0, measuredRows = 0;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(double a, double b) { return std::abs(a - b) < 2e-14; }

// Independent eight-neighbor flood fill; does not use OpenCV labels or stats.
std::vector<std::vector<cv::Point>> components(const cv::Mat& image, double threshold)
{
    std::vector<std::vector<cv::Point>> parts;
    std::vector<bool> seen(image.total());
    for (int y = 0; y < image.rows; ++y) for (int x = 0; x < image.cols; ++x)
    {
        const size_t index = size_t(y) * image.cols + x;
        if (seen[index] || !(image.at<cv::Vec4f>(y,x)[3] > threshold)) continue;
        seen[index] = true;
        std::vector<cv::Point> points{{x,y}};
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto p = points[i];
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            {
                const int xx = p.x + dx, yy = p.y + dy;
                if (xx < 0 || yy < 0 || xx >= image.cols || yy >= image.rows) continue;
                const size_t at = size_t(yy) * image.cols + xx;
                if (!seen[at] && image.at<cv::Vec4f>(yy,xx)[3] > threshold)
                { seen[at] = true; points.push_back({xx,yy}); }
            }
        }
        parts.push_back(std::move(points));
    }
    return parts;
}

void check(const cv::Mat& image, double threshold, int seed)
{
    const auto parts = components(image, threshold);
    InfoTable table;
    for (int method = 0; method < 5; ++method)
    {
        auto expected = legacyProcess(image, threshold, MethodMenuItems(method), seed);
        // Keep the frozen reference unchanged. Recover its selected integer row
        // on these small fixtures, then express that same pixel from the bottom.
        // This preserves exact checks for U/B/A, Random, selection and backgrounds.
        if (MethodMenuItems(method) != MethodMenuItems::Random && image.rows > 1)
            for (int y = 0; y < image.rows; ++y) for (int x = 0; x < image.cols; ++x)
            {
                auto& color = expected.at<cv::Vec4f>(y,x);
                if (color[3] == 0) continue;
                const int selectedY = int(std::lround(double(color[1]) * (image.rows - 1)));
                color[1] = float(image.rows - 1 - selectedY) / float(image.rows - 1);
            }
        for (auto detail : {DATDetail::Basic, DATDetail::Medium, DATDetail::Maximum})
        {
            table.setDetail(detail);
            const auto output = Segment::process(image, threshold, MethodMenuItems(method), seed, &table);
            require(output.type() == expected.type() && output.size() == expected.size(), "output dimensions/type");
            for (int y = 0; y < output.rows; ++y)
                require(std::memcmp(output.ptr(y), expected.ptr(y), size_t(output.cols)*16) == 0, "legacy pixel parity with corrected V origin");
            require(table.rowCount() == int(parts.size()) + 1, "component count");
            const int columns = detail == DATDetail::Basic ? 5 : detail == DATDetail::Medium ? 9 : 15;
            require(table.columnCount() == columns, "detail column count");
            const char* names[] = {"id","u","v","width","height","left","top","right","bottom",
                "selected_pixels","centroid_u","centroid_v","output_r","output_g","output_b"};
            for (int col = 0; col < columns; ++col) require(std::string(table.cell(0,col)) == names[col], "header order");
            std::vector<bool> matched(parts.size());
            for (int row = 1; row < table.rowCount(); ++row)
            {
                require(std::stoi(table.cell(row,0)) == row-1, "frame-local row identity");
                bool found = false;
                for (size_t index = 0; index < parts.size() && !found; ++index)
                {
                    if (matched[index]) continue;
                    const auto& points = parts[index];
                    int left = image.cols, right = 0, top = image.rows, bottom = 0;
                    double sx = 0, sy = 0;
                    for (auto p : points)
                    {
                        left = std::min(left,p.x); right = std::max(right,p.x+1);
                        top = std::min(top,p.y); bottom = std::max(bottom,p.y+1);
                        sx += p.x + .5; sy += image.rows - p.y - .5;
                    }
                    const double w = image.cols, h = image.rows;
                    const auto color = expected.at<cv::Vec4f>(points[0]);
                    const double values[] = {double(row-1), (left+right)*.5/w, (2*h-top-bottom)*.5/h,
                        (right-left)/w, (bottom-top)/h, left/w, (h-top)/h, right/w, (h-bottom)/h,
                        double(points.size()), sx/points.size()/w, sy/points.size()/h,
                        color[0],color[1],color[2]};
                    bool same = true;
                    for (int col = 1; col < columns; ++col)
                        same = same && near(std::stod(table.cell(row,col)), values[col]);
                    if (same) { found = true; matched[index] = true; }
                }
                require(found, "independent geometry / output values");
                ++measuredRows;
            }
            require(std::string(table.cell(-1,0)).empty() && std::string(table.cell(table.rowCount(),0)).empty()
                && std::string(table.cell(0,columns)).empty(), "DAT bounds");
            ++cases;
        }
    }
}

size_t checkBottomLeftOrigin()
{
    struct Sample { int width, height, x, y; float u, v; };
    const Sample samples[] = {
        {5,5,0,4,0,0}, {5,5,4,4,1,0}, {5,5,0,0,0,1}, {5,5,4,0,1,1},
        {5,5,1,1,.25f,.75f}, {5,5,3,3,.75f,.25f},
        {1,5,0,3,0,.25f}, {5,1,3,0,.75f,0}, {1,1,0,0,0,0}
    };
    size_t checks = 0;
    for (auto sample : samples) for (int method = 0; method < 4; ++method)
        for (auto detail : {DATDetail::Basic,DATDetail::Medium,DATDetail::Maximum})
    {
        cv::Mat input(sample.height,sample.width,CV_32FC4,cv::Scalar(0));
        input.at<cv::Vec4f>(sample.y,sample.x)[3] = 1;
        InfoTable table;
        table.setDetail(detail);
        const auto output = Segment::process(input,.5,MethodMenuItems(method),1,&table);
        const auto color = output.at<cv::Vec4f>(sample.y,sample.x);
        require(color[0] == sample.u && color[1] == sample.v && color[2] == 0 && color[3] == 1,
            "bottom-left UV origin: known pixel coordinate");
        for (int y = 0; y < input.rows; ++y) for (int x = 0; x < input.cols; ++x)
            if (x != sample.x || y != sample.y)
                require(output.at<cv::Vec4f>(y,x) == cv::Vec4f(0,0,0,0), "UV fix moved the segment/background");
        require(table.rowCount() == 2, "single-pixel segment row");
        if (detail == DATDetail::Maximum)
        {
            require(std::stod(table.cell(1,12)) == sample.u && std::stod(table.cell(1,13)) == sample.v,
                "DAT output coordinates disagree with bottom-left UV");
            require(near(std::stod(table.cell(1,2)), (sample.height-sample.y-.5)/sample.height),
                "DAT geometric origin changed");
        }
        ++checks;
    }
    return checks;
}

int main()
{
    try
    {
        cv::setNumThreads(1);
        const auto originChecks = checkBottomLeftOrigin();
        for (unsigned mask = 0; mask < 512; ++mask)
        {
            cv::Mat image(3,3,CV_32FC4,cv::Scalar(0,0,0,0));
            for (int i = 0; i < 9; ++i) image.at<cv::Vec4f>(i/3,i%3)[3] = (mask>>i)&1;
            check(image,.5,1);
        }
        std::mt19937 rng(3472);
        for (int i = 0; i < 160; ++i)
        {
            int w = 1 + rng()%41, h = 1 + rng()%29;
            if (i%8 == 0) w = 1;
            if (i%8 == 1) h = 1;
            cv::Mat backing(h+2,w+2,CV_32FC4,cv::Scalar(0));
            cv::Mat image = backing(cv::Rect(1,1,w,h)); // exercise non-contiguous rows
            const float values[] = {0,.25f,.5f,1,-1,2,std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
            for (int y=0;y<h;++y) for (int x=0;x<w;++x)
                image.at<cv::Vec4f>(y,x) = {float(x),float(y),.8f,values[rng()%9]};
            check(image, (i%3)*.5, 1+rng()%9999);
        }
        // Asymmetric ring and a separate interior island; centroid may lie in a hole.
        cv::Mat ring(13,19,CV_32FC4,cv::Scalar(0));
        for (int y=1;y<10;++y) for (int x=2;x<17;++x)
            if (y==1 || y==9 || x==2 || x==16 || (x==7 && y==5)) ring.at<cv::Vec4f>(y,x)[3]=1;
        check(ring,.5,9999);
        InfoTable table;
        for (auto detail : {DATDetail::Maximum,DATDetail::Basic,DATDetail::Medium})
        {
            table.setDetail(detail);
            Segment::process(ring,.5,MethodMenuItems::Centroid,1,&table);
            try { Segment::process({},.5,MethodMenuItems::Centroid,1,&table); require(false,"missing error"); }
            catch (const std::invalid_argument&) {}
            require(table.rowCount()==1,"error clears previous rows");
            cv::Mat empty(4,9,CV_32FC4,cv::Scalar(0));
            Segment::process(empty,.5,MethodMenuItems::Random,1,&table);
            require(table.rowCount()==1,"empty mask keeps only headers");
        }
        // Counts remain exact above float's consecutive-integer precision.
        table.setDetail(DATDetail::Maximum);
        table.append({0,0,8192,8192,16777217,0,0,{0,0,0}},8192,8192);
        require(std::string(table.cell(1,9))=="16777217","pixel count precision");
        table.setDetail(DATDetail::Basic);
        require(table.rowCount()==1 && table.columnCount()==5,"detail clears incompatible snapshot");
        std::cout << cases << " image/detail comparisons and " << measuredRows << " independent measurement rows passed; "
            << originChecks << " bottom-left origin regressions passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
