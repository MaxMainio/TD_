#include "SegmentUV.h"
#include "SegmentEngine.h"
#include "OutputPacking.h"
#include "Docking.h"
#include <exception>
#include <climits>
#include <stdexcept>

extern "C"
{
DLLEXPORT void FillTOPPluginInfo(TD::TOP_PluginInfo* info)
{
    if (!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion)) return;
    info->executeMode = TD::TOP_ExecuteMode::CPUMem;
    auto& custom = info->customOPInfo;
    custom.opType->setString("Segmentuv");
    custom.opLabel->setString("Segment UV");
    custom.authorName->setString("Max Mainio Beidler");
    custom.authorEmail->setString("beidler.max@email.com");
    custom.minInputs = custom.maxInputs = 1;
    custom.minorVersion = 2;
    Segment::configureDocking(custom);
}
DLLEXPORT TD::TOP_CPlusPlusBase* CreateTOPInstance(const TD::OP_NodeInfo* node, TD::TOP_Context* context)
{
    return new SegmentUV(node, context);
}
DLLEXPORT void DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context*)
{
    delete instance;
}
}

SegmentUV::SegmentUV(const TD::OP_NodeInfo* node, TD::TOP_Context* context)
    : context_(context), nodeId_(node->opId)
{
    requestDocking();
}

void SegmentUV::requestDocking(bool resetLayout)
{
    try { dockWarning_ = Segment::scheduleDocking(nodeId_, resetLayout); }
    catch (const std::exception& e) { dockWarning_ = e.what(); }
    catch (...) { dockWarning_ = "Could not schedule Segment Info DAT setup"; }
}

void SegmentUV::pulsePressed(const char* name, void*)
{
    if (name && std::string(name) == "Repairsegmentdat") requestDocking(true);
}

void SegmentUV::getGeneralInfo(TD::TOP_GeneralInfo* info, const TD::OP_Inputs* inputs, void*)
{
    info->cookEveryFrame = false;
    // Every method is deterministic for its inputs/parameters, including Seed.
    // A dependent Info DAT must not make an unchanged TOP cook every frame.
    info->cookEveryFrameIfAsked = false;
    info->inputSizeIndex = 0;
    table_.setDetail(Parameters::evalDATDetail(inputs));
    if (!inputs || !inputs->getInputTOP(0)) table_.clear();
}

void SegmentUV::setupParameters(TD::OP_ParameterManager* manager, void*)
{
    Parameters::setup(manager);
}

void SegmentUV::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
    error_.clear();
    table_.clear();
    try
    {
        namespace Pack = TDPlugin::TOPOutput;
        const auto detail = Parameters::evalDATDetail(inputs);
        table_.setDetail(detail);
        const auto* top = inputs ? inputs->getInputTOP(0) : nullptr;
        if (!top) throw std::runtime_error("Connect one 2D TOP image");
        if (top->textureDesc.texDim != TD::OP_TexDim::e2D ||
            !top->textureDesc.width || !top->textureDesc.height)
            throw std::runtime_error("Input must be a non-empty 2D TOP image");
        TD::OP_TOPInputDownloadOptions options;
        options.verticalFlip = true;
        options.pixelFormat = TD::OP_PixelFormat::RGBA32Float;
        auto download = top->downloadTexture(options, nullptr);
        if (!download) throw std::runtime_error("Input texture download failed");
        const size_t w = download->textureDesc.width, h = download->textureDesc.height;
        auto* pixels = static_cast<float*>(download->getData());
        if (!pixels || !w || !h || w > INT_MAX || h > INT_MAX ||
            w * h > INT_MAX || w > SIZE_MAX / h / (4 * sizeof(float)) ||
            download->textureDesc.pixelFormat != TD::OP_PixelFormat::RGBA32Float ||
            download->size < w * h * 4 * sizeof(float))
            throw std::runtime_error("Input download did not return a supported complete RGBA32Float image");
        // Preserve the original owned clone and top-down processing orientation.
        const cv::Mat input = cv::Mat(int(h), int(w), CV_32FC4, pixels).clone();
        Segment::InfoTable nextTable;
        nextTable.setDetail(detail);
        const cv::Mat image = Segment::process(input, Parameters::evalAlphathreshold(inputs),
            Parameters::evalMethod(inputs), Parameters::evalSeed(inputs), &nextTable);
        const auto desc = Pack::resolvedDesc(output, uint32_t(w), uint32_t(h),
            TD::OP_PixelFormat::RG32Float, true, inputs, top->textureDesc.pixelFormat);
        auto buffer = Segment::packOutput(context_, image, desc);
        TD::TOP_UploadInfo upload;
        upload.textureDesc = desc;
        upload.firstPixel = TD::TOP_FirstPixel::BottomLeft;
        upload.colorBufferIndex = 0;
        output->uploadBuffer(&buffer, upload, nullptr);
        // Readers only see a complete snapshot paired with a successful upload.
        table_ = std::move(nextTable);
    }
    catch (const std::exception& e) { table_.clear(); error_ = e.what(); }
    catch (...) { table_.clear(); error_ = "Unexpected error while processing segments"; }
}

void SegmentUV::getErrorString(TD::OP_String* value, void*) { value->setString(error_.c_str()); }
void SegmentUV::getWarningString(TD::OP_String* value, void*) { value->setString(dockWarning_.c_str()); }

bool SegmentUV::getInfoDATSize(TD::OP_InfoDATSize* size, void*)
{
    size->rows = table_.rowCount();
    size->cols = table_.columnCount();
    size->byColumn = false;
    return true;
}

void SegmentUV::getInfoDATEntries(int32_t row, int32_t count, TD::OP_InfoDATEntries* entries, void*)
{
    for (int32_t column = 0; column < count; ++column)
        entries->values[column]->setString(table_.cell(row, column));
}
