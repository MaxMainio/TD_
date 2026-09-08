#include "Parameters.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace Hull
{
namespace
{
void check(TD::OP_ParAppendResult result) { assert(result == TD::OP_ParAppendResult::Success); (void)result; }
void menu(TD::OP_ParameterManager* manager, const char* name, const char* label, const char* page,
          std::initializer_list<const char*> names, std::initializer_list<const char*> labels)
{
    TD::OP_StringParameter p(name);
    p.label = label; p.page = page; p.defaultValue = *names.begin();
    std::vector<const char*> n(names), l(labels);
    check(manager->appendMenu(p, int32_t(n.size()), n.data(), l.data()));
}
void number(TD::OP_ParameterManager* manager, const char* name, const char* label, const char* page,
            double value, double minimum, double sliderMaximum, bool integer = false)
{
    TD::OP_NumericParameter p(name);
    p.label = label; p.page = page; p.defaultValues[0] = value;
    p.minValues[0] = p.minSliders[0] = minimum;
    p.maxValues[0] = integer ? INT32_MAX : sliderMaximum;
    p.maxSliders[0] = sliderMaximum;
    p.clampMins[0] = p.clampMaxes[0] = true;
    check(integer ? manager->appendInt(p) : manager->appendFloat(p));
}
}

void setupParameters(TD::OP_ParameterManager* m)
{
    menu(m,"Source","Source","Selection",{"Luminance","Red","Green","Blue","Alpha","RGB","RGBA"},
         {"Luminance","Red","Green","Blue","Alpha","RGB Separate","RGBA Separate"});
    menu(m,"Thresholdmode","Threshold Mode","Selection",{"Shared","Independent"},{"Shared","Independent"});
    number(m,"Threshold","Threshold","Selection",.5,0,1);
    number(m,"Thresholdr","Red Threshold","Selection",.5,0,1);
    number(m,"Thresholdg","Green Threshold","Selection",.5,0,1);
    number(m,"Thresholdb","Blue Threshold","Selection",.5,0,1);
    number(m,"Thresholda","Alpha Threshold","Selection",.5,0,1);
    menu(m,"Direction","Threshold Direction","Selection",{"Above","Below"},{"Above","Below"});
    TD::OP_NumericParameter transparent("Ignoretransparent");
    transparent.label = "Ignore Transparent Pixels"; transparent.page = "Selection";
    check(m->appendToggle(transparent));
    number(m,"Alphacutoff","Alpha Cutoff","Selection",0,0,1);
    menu(m,"Grouping","Hull Grouping","Hull",{"Global","Perblob","Largestblob"},{"Global","Per Blob","Largest Blob"});
    number(m,"Minarea","Minimum Blob Area","Hull",1,1,1000,true);
    menu(m,"View","Output View","Output",{"Filled","Outline","Selection"},{"Filled Hull","Outline","Selection Mask"});
    number(m,"Outlinewidth","Outline Width","Output",1,1,32,true);
    menu(m,"Outputalpha","Output Alpha","Output",{"Opaque","Preserve","Result"},{"Opaque","Preserve Input","Result Mask"});
    menu(m,"Datdetail","DAT Detail","Output",{"Basic","Medium","Maximum"},{"Basic","Medium","Maximum"});
    TD::OP_NumericParameter repair("Repairhulldat");
    repair.label = "Create/Repair Hull DAT"; repair.page = "Output";
    check(m->appendPulse(repair));
}

DATDetail evaluateDATDetail(const TD::OP_Inputs* inputs)
{
    return inputs ? DATDetail(std::clamp(inputs->getParInt("Datdetail"), 0, 2)) : DATDetail::Basic;
}

Settings evaluateParameters(const TD::OP_Inputs* in)
{
    Settings s;
    s.source = Source(std::clamp(in->getParInt("Source"),0,6));
    s.grouping = Grouping(std::clamp(in->getParInt("Grouping"),0,2));
    s.view = View(std::clamp(in->getParInt("View"),0,2));
    s.alpha = Alpha(std::clamp(in->getParInt("Outputalpha"),0,2));
    const bool separate = s.source == Source::RGB || s.source == Source::RGBA;
    s.independent = separate && in->getParInt("Thresholdmode") == 1;
    s.threshold = in->getParDouble("Threshold");
    const char* thresholds[] = {"Thresholdr","Thresholdg","Thresholdb","Thresholda"};
    for (int i = 0; i < 4; ++i)
    {
        s.thresholds[i] = in->getParDouble(thresholds[i]);
        in->enablePar(thresholds[i],s.independent && (i != 3 || s.source == Source::RGBA));
    }
    s.below = in->getParInt("Direction") == 1;
    s.ignoreTransparent = in->getParInt("Ignoretransparent") != 0;
    s.alphaCutoff = in->getParDouble("Alphacutoff");
    s.minimumArea = uint64_t(std::max(1,in->getParInt("Minarea")));
    s.outlineWidth = std::max(1,in->getParInt("Outlinewidth"));
    in->enablePar("Thresholdmode",separate);
    in->enablePar("Threshold",!s.independent);
    in->enablePar("Alphacutoff",s.ignoreTransparent);
    in->enablePar("Outlinewidth",s.view == View::Outline);
    in->enablePar("Outputalpha",s.source != Source::RGBA);
    return s;
}
}
