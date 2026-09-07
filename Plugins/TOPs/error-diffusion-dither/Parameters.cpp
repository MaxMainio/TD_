#include "Parameters.h"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <vector>

namespace Dither
{
namespace
{
void menu(TD::OP_ParameterManager* manager, const char* name, const char* label,
          const char* page, std::initializer_list<const char*> names,
          std::initializer_list<const char*> labels)
{
    TD::OP_StringParameter p(name);
    p.label = label;
    p.page = page;
    p.defaultValue = *names.begin();
    std::vector<const char*> menuNames(names), menuLabels(labels);
    const auto result = manager->appendMenu(p, static_cast<int32_t>(names.size()), menuNames.data(), menuLabels.data());
    assert(result == TD::OP_ParAppendResult::Success);
    (void)result;
}
}

void setupParameters(TD::OP_ParameterManager* manager)
{
    menu(manager, "Algorithm", "Algorithm", "Dither",
         {"Floydsteinberg", "Jarvisjudiceninke", "Stucki", "Atkinson", "Burkes", "Sierra"},
         {"Floyd-Steinberg", "Jarvis-Judice-Ninke", "Stucki", "Atkinson", "Burkes", "Sierra"});
    menu(manager, "Alternate", "Sierra Variant", "Dither",
         {"Full", "Tworow", "Lite"}, {"Full", "Two-Row", "Lite"});
    TD::OP_NumericParameter depth("Bitdepth");
    depth.label = "Bit Depth";
    depth.page = "Dither";
    depth.defaultValues[0] = depth.minValues[0] = depth.minSliders[0] = 1;
    depth.maxValues[0] = depth.maxSliders[0] = 8;
    depth.clampMins[0] = depth.clampMaxes[0] = true;
    auto result = manager->appendInt(depth);
    assert(result == TD::OP_ParAppendResult::Success);
    menu(manager, "Scanpattern", "Scan Pattern", "Dither",
         {"Raster", "Serpentine"}, {"Raster", "Serpentine"});
    TD::OP_NumericParameter strength("Diffusion");
    strength.label = "Diffusion Strength";
    strength.page = "Dither";
    strength.defaultValues[0] = 1;
    strength.clampMins[0] = strength.clampMaxes[0] = true;
    result = manager->appendFloat(strength);
    assert(result == TD::OP_ParAppendResult::Success);
    (void)result;
    menu(manager, "Colormode", "Color Mode", "Color",
         {"RGB", "Monochrome"}, {"RGB", "Monochrome"});
    menu(manager, "Monosource", "Monochrome Source", "Color",
         {"Luminance", "Red", "Green", "Blue", "Alpha"},
         {"Luminance", "Red", "Green", "Blue", "Alpha"});
    menu(manager, "Alpha", "Alpha", "Color",
         {"Preserve", "Dither", "Opaque"}, {"Preserve", "Dither", "Opaque"});
}

Settings evaluateParameters(const TD::OP_Inputs* inputs)
{
    Settings s;
    const int algorithm = std::clamp(inputs->getParInt("Algorithm"), 0, 5);
    s.kernel = static_cast<Kernel>(algorithm == 5 ? 5 + std::clamp(inputs->getParInt("Alternate"), 0, 2) : algorithm);
    s.bits = inputs->getParInt("Bitdepth");
    s.strength = static_cast<float>(inputs->getParDouble("Diffusion"));
    s.scan = static_cast<Scan>(std::clamp(inputs->getParInt("Scanpattern"), 0, 1));
    s.color = static_cast<Color>(std::clamp(inputs->getParInt("Colormode"), 0, 1));
    s.source = static_cast<Source>(std::clamp(inputs->getParInt("Monosource"), 0, 4));
    s.alpha = static_cast<Alpha>(std::clamp(inputs->getParInt("Alpha"), 0, 2));
    inputs->enablePar("Alternate", algorithm == 5);
    inputs->enablePar("Monosource", s.color == Color::Monochrome);
    return s;
}
}
