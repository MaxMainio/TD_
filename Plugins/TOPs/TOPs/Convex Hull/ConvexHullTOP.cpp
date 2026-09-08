#include "ConvexHullTOP.h"
#include "Parameters.h"
#include "OutputPacking.h"
#include "Docking.h"

#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>

extern "C"
{
DLLEXPORT void FillTOPPluginInfo(TD::TOP_PluginInfo* info)
{
    if (!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion)) return;
    info->executeMode = TD::TOP_ExecuteMode::CPUMem;
    auto& custom = info->customOPInfo;
    custom.opType->setString("Convexhull");
    custom.opLabel->setString("Convex Hull");
    custom.authorName->setString("Max Mainio Beidler");
    custom.authorEmail->setString("beidler.max@gmail.com");
    custom.minInputs = custom.maxInputs = 1;
    custom.minorVersion = 3;
    Hull::configureDocking(custom);
}

DLLEXPORT TD::TOP_CPlusPlusBase* CreateTOPInstance(const TD::OP_NodeInfo* node, TD::TOP_Context* context)
{
    return new ConvexHullTOP(node, context);
}

DLLEXPORT void DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance, TD::TOP_Context*)
{
    delete instance;
}
}

namespace
{
using Clock = std::chrono::steady_clock;
float milliseconds(Clock::time_point start)
{
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}
}

ConvexHullTOP::ConvexHullTOP(const TD::OP_NodeInfo* node, TD::TOP_Context* context)
    : context_(context), nodeId_(node->opId)
{
    requestDocking();
}

void ConvexHullTOP::requestDocking(bool resetLayout)
{
    try { dockWarning_ = Hull::scheduleDocking(nodeId_, resetLayout); }
    catch (const std::exception& e) { dockWarning_ = e.what(); }
    catch (...) { dockWarning_ = "Could not schedule Hull Info DAT setup"; }
}

void ConvexHullTOP::pulsePressed(const char* name, void*)
{
    if (name && std::string(name) == "Repairhulldat") requestDocking(true);
}

void ConvexHullTOP::getGeneralInfo(TD::TOP_GeneralInfo* info, const TD::OP_Inputs* inputs, void*)
{
    info->cookEveryFrame = false;
    info->cookEveryFrameIfAsked = false;
    info->inputSizeIndex = 0;
    if (inputs) Hull::evaluateParameters(inputs);
    table_.setDetail(Hull::evaluateDATDetail(inputs));
    if (!inputs || !inputs->getInputTOP(0)) table_.clear();
}

void ConvexHullTOP::setupParameters(TD::OP_ParameterManager* manager, void*)
{
    Hull::setupParameters(manager);
}

void ConvexHullTOP::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
    const auto start = Clock::now();
    error_.clear();
    warning_.clear();
    stats_.fill(0.f);
    table_.clear();
    try
    {
        namespace Pack = TDPlugin::TOPOutput;
        const auto settings = Hull::evaluateParameters(inputs);
        const auto detail = Hull::evaluateDATDetail(inputs);
        table_.setDetail(detail);
        const TD::OP_TOPInput* top = inputs->getInputTOP(0);
        if (!top) throw std::runtime_error("Connect one 2D TOP image");
        if (top->textureDesc.texDim != TD::OP_TexDim::e2D || !top->textureDesc.width || !top->textureDesc.height)
            throw std::runtime_error("Input must be a non-empty 2D TOP image");

        TD::OP_TextureDesc suggested;
        output->getSuggestedOutputDesc(&suggested, nullptr);
        const auto requestedFormat = Pack::commonFormatUsesInput(inputs) ? top->textureDesc.pixelFormat : suggested.pixelFormat;
        if (requestedFormat != TD::OP_PixelFormat::Invalid && !Pack::isSupportedOutputFormat(requestedFormat))
            warning_ = "Selected pixel format is not supported by this SDK; using 8-bit RGBA";
        const auto desc = Pack::resolvedDesc(output, top->textureDesc.width, top->textureDesc.height,
                                            TD::OP_PixelFormat::RGBA8Fixed, true, inputs, top->textureDesc.pixelFormat);
        const size_t bpp = Pack::bytesPerPixel(desc.pixelFormat);
        if (!desc.width || !desc.height || !bpp ||
            desc.width > SIZE_MAX / static_cast<size_t>(desc.height) / bpp)
            throw std::runtime_error("Invalid output dimensions or format");

        const auto downloadStart = Clock::now();
        TD::OP_TOPInputDownloadOptions options;
        options.verticalFlip = false;
        options.pixelFormat = TD::OP_PixelFormat::RGBA32Float;
        // Preserve the working color values; do not add an implicit transfer function.
        options.colorSpace = TD::OP_ColorSpace::Passthrough;
        auto download = top->downloadTexture(options, nullptr);
        if (!download) throw std::runtime_error("Input texture download failed");
        const float* pixels = static_cast<const float*>(download->getData());
        stats_[0] = milliseconds(downloadStart);
        const size_t w = download->textureDesc.width, h = download->textureDesc.height;
        if (!pixels || !w || !h || w > SIZE_MAX / h / (4 * sizeof(float)) ||
            download->textureDesc.pixelFormat != TD::OP_PixelFormat::RGBA32Float ||
            download->size < w * h * 4 * sizeof(float))
            throw std::runtime_error("Input download did not return a complete RGBA32Float image");

        const auto processStart = Clock::now();
        const auto image = engine_.process({pixels, w, h, w * 4}, settings);
        stats_[1] = milliseconds(processStart);
        const auto packStart = Clock::now();
        const size_t bytes = static_cast<size_t>(desc.width) * desc.height * bpp;
        auto buffer = context_->createOutputBuffer(bytes, TD::TOP_BufferFlags::None, nullptr);
        if (!buffer || !buffer->data || buffer->size < bytes)
            throw std::runtime_error("Could not allocate the output texture buffer");
        Hull::packImage(image, desc.pixelFormat, buffer->data, buffer->size, desc.width, desc.height);
        stats_[2] = milliseconds(packStart);

        Hull::InfoTable nextTable;
        nextTable.assign(engine_.measurements(), w, h, detail);
        TD::TOP_UploadInfo upload;
        upload.textureDesc = desc;
        upload.firstPixel = TD::TOP_FirstPixel::BottomLeft;
        upload.colorSpace = TD::OP_ColorSpace::Passthrough;
        output->uploadBuffer(&buffer, upload, nullptr);
        table_ = std::move(nextTable);
        const auto& sources = engine_.stats();
        for (size_t c = 0; c < sources.size(); ++c)
        {
            const auto& s = sources[c];
            const size_t offset = 6+c*6;
            stats_[offset] = float(s.selectedPixels);
            stats_[offset+1] = float(s.retainedPixels);
            stats_[offset+2] = float(s.blobs);
            stats_[offset+3] = float(s.hulls);
            stats_[offset+4] = float(s.outputPixels);
            stats_[offset+5] = float(double(s.outputPixels)/(w*h));
        }
        stats_[4] = static_cast<float>(engine_.workerCount());
        stats_[5] = static_cast<float>(engine_.retainedBytes());

    }
    catch (const std::exception& e) { table_.clear(); error_ = e.what(); }
    catch (...) { table_.clear(); error_ = "Unexpected error while calculating convex hulls"; }
    stats_[3] = milliseconds(start);
}

void ConvexHullTOP::getErrorString(TD::OP_String* string, void*) { string->setString(error_.c_str()); }
void ConvexHullTOP::getWarningString(TD::OP_String* string, void*)
{
    const std::string message = warning_ + (warning_.empty() || dockWarning_.empty() ? "" : "\n") + dockWarning_;
    string->setString(message.c_str());
}

bool ConvexHullTOP::getInfoDATSize(TD::OP_InfoDATSize* size, void*)
{
    size->rows = table_.rowCount();
    size->cols = table_.columnCount();
    size->byColumn = false;
    return true;
}

void ConvexHullTOP::getInfoDATEntries(int32_t row, int32_t count, TD::OP_InfoDATEntries* entries, void*)
{
    for (int32_t column = 0; column < count; ++column)
        entries->values[column]->setString(table_.cell(row, column));
}

void ConvexHullTOP::getInfoCHOPChan(int32_t index, TD::OP_InfoCHOPChan* channel, void*)
{
    static constexpr const char* timings[] = {"download_ms", "process_ms", "pack_ms", "execute_ms", "workers", "retained_bytes"};
    static constexpr const char* sources[] = {"luma", "r", "g", "b", "a"};
    static constexpr const char* metrics[] = {"selected_pixels", "retained_pixels", "blobs", "hulls", "output_pixels", "coverage"};
    if (index < 0 || index >= int32_t(stats_.size())) return;
    const std::string name = index < 6 ? timings[index] : std::string(sources[(index-6)/6])+"_"+metrics[(index-6)%6];
    channel->name->setString(name.c_str());
    channel->value = stats_[index];
}
