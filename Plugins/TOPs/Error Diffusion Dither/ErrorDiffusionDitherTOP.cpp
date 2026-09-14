#include "ErrorDiffusionDitherTOP.h"
#include "Parameters.h"
#include "OutputPacking.h"

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
    custom.opType->setString("Errordiffusiondither");
    custom.opLabel->setString("Error Diffusion Dither");
    custom.authorName->setString("Max Mainio Beidler");
    custom.authorEmail->setString("beidler.max@gmail.com");
    custom.minInputs = custom.maxInputs = 1;
}

DLLEXPORT TD::TOP_CPlusPlusBase* CreateTOPInstance(const TD::OP_NodeInfo*, TD::TOP_Context* context)
{
    return new ErrorDiffusionDitherTOP(context);
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

void ErrorDiffusionDitherTOP::getGeneralInfo(TD::TOP_GeneralInfo* info, const TD::OP_Inputs* inputs, void*)
{
    info->cookEveryFrame = false;
    info->cookEveryFrameIfAsked = false;
    info->inputSizeIndex = 0;
    if (inputs) Dither::evaluateParameters(inputs);
}

void ErrorDiffusionDitherTOP::setupParameters(TD::OP_ParameterManager* manager, void*)
{
    Dither::setupParameters(manager);
}

void ErrorDiffusionDitherTOP::execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void*)
{
    const auto start = Clock::now();
    error_.clear();
    warning_.clear();
    stats_.fill(0.f);
    try
    {
        namespace Pack = TDPlugin::TOPOutput;
        const auto settings = Dither::evaluateParameters(inputs);
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
        const auto image = engine_.process({pixels, w, h, w * 4}, desc.width, desc.height,
                                          settings, Dither::outputChannels(desc.pixelFormat));
        stats_[1] = milliseconds(processStart);
        const auto packStart = Clock::now();
        const size_t bytes = static_cast<size_t>(desc.width) * desc.height * bpp;
        auto buffer = context_->createOutputBuffer(bytes, TD::TOP_BufferFlags::None, nullptr);
        if (!buffer || !buffer->data || buffer->size < bytes)
            throw std::runtime_error("Could not allocate the output texture buffer");
        Dither::packImage(image, desc.pixelFormat, buffer->data, buffer->size);
        stats_[2] = milliseconds(packStart);

        TD::TOP_UploadInfo upload;
        upload.textureDesc = desc;
        upload.firstPixel = TD::TOP_FirstPixel::BottomLeft;
        upload.colorSpace = TD::OP_ColorSpace::Passthrough;
        output->uploadBuffer(&buffer, upload, nullptr);
        stats_[4] = static_cast<float>(engine_.workerCount());
        stats_[5] = static_cast<float>(engine_.scratchBytes());
        stats_[6] = static_cast<float>(engine_.retainedBytes());
    }
    catch (const std::exception& e) { error_ = e.what(); }
    catch (...) { error_ = "Unexpected error while dithering the image"; }
    stats_[3] = milliseconds(start);
}

void ErrorDiffusionDitherTOP::getErrorString(TD::OP_String* string, void*) { string->setString(error_.c_str()); }
void ErrorDiffusionDitherTOP::getWarningString(TD::OP_String* string, void*) { string->setString(warning_.c_str()); }

void ErrorDiffusionDitherTOP::getInfoCHOPChan(int32_t index, TD::OP_InfoCHOPChan* channel, void*)
{
    static constexpr const char* names[] = {"download_ms", "process_ms", "pack_ms", "execute_ms",
                                           "workers", "scratch_bytes", "retained_bytes"};
    if (index >= 0 && index < static_cast<int32_t>(stats_.size()))
    {
        channel->name->setString(names[index]);
        channel->value = stats_[index];
    }
}
