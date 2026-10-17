#include "AppleFaceDetection.h"
#include "Docking.h"
#include "TOPOutputHelper.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

extern "C"
{
DLLEXPORT void FillTOPPluginInfo(TD::TOP_PluginInfo* info)
{
    if(!info->setAPIVersion(TD::TOPCPlusPlusAPIVersion)) return;
    info->executeMode=TD::TOP_ExecuteMode::CPUMem;
    auto& custom=info->customOPInfo;
    custom.opType->setString("Applefacedetection");
    custom.opLabel->setString("Apple Face Detection");
    custom.opIcon->setString("AFD");
    custom.authorName->setString("Max Mainio Beidler");
    custom.authorEmail->setString("beidler.max@gmail.com");
    custom.minInputs=custom.maxInputs=1;
    custom.minorVersion=1;
    FaceDetection::configureDocking(custom);
}
DLLEXPORT TD::TOP_CPlusPlusBase* CreateTOPInstance(const TD::OP_NodeInfo* node,TD::TOP_Context* context)
{ return new AppleFaceDetection(node,context); }
DLLEXPORT void DestroyTOPInstance(TD::TOP_CPlusPlusBase* instance,TD::TOP_Context*) { delete instance; }
}

AppleFaceDetection::AppleFaceDetection(const TD::OP_NodeInfo* node,TD::TOP_Context* context)
    :context_(context),nodeId_(node->opId) { requestDocking(); }

void AppleFaceDetection::requestDocking(bool resetLayout)
{
    try { dockWarning_=FaceDetection::scheduleDocking(nodeId_,resetLayout); }
    catch(const std::exception& e) { dockWarning_=e.what(); }
    catch(...) { dockWarning_="Could not schedule Landmarks Info DAT setup"; }
}
void AppleFaceDetection::pulsePressed(const char* name,void*)
{ if(name && !std::strcmp(name,"Repairlandmarksdat")) requestDocking(true); }

void AppleFaceDetection::getGeneralInfo(TD::TOP_GeneralInfo* info,const TD::OP_Inputs* inputs,void*)
{
    info->cookEveryFrame=false;
    info->cookEveryFrameIfAsked=false;
    info->inputSizeIndex=0;
    if(!inputs->getInputTOP(0)) { table_.clear(); faceCount_=detectionMs_=0; }
}

void AppleFaceDetection::setupParameters(TD::OP_ParameterManager* manager,void*)
{
    TD::OP_StringParameter shape;
    shape.name="Maskshape"; shape.label="Mask Shape"; shape.page="Face Detection"; shape.defaultValue="Oval";
    const char* shapes[]={"Oval","Landmarkhull","Boundingbox"};
    const char* shapeLabels[]={"Face Oval","Landmark Hull","Bounding Box"};
    manager->appendMenu(shape,3,shapes,shapeLabels);
    TD::OP_StringParameter selection;
    selection.name="Faces"; selection.label="Faces"; selection.page="Face Detection"; selection.defaultValue="All";
    const char* selections[]={"All","Largest"};
    const char* selectionLabels[]={"All Faces","Largest Face"};
    manager->appendMenu(selection,2,selections,selectionLabels);
    TD::OP_NumericParameter confidence("Minconfidence");
    confidence.label="Minimum Confidence"; confidence.page="Face Detection";
    confidence.defaultValues[0]=.5;
    confidence.minValues[0]=confidence.minSliders[0]=0;
    confidence.maxValues[0]=confidence.maxSliders[0]=1;
    confidence.clampMins[0]=confidence.clampMaxes[0]=true;
    manager->appendFloat(confidence);
    TD::OP_NumericParameter repair("Repairlandmarksdat");
    repair.label="Create/Repair Landmarks DAT"; repair.page="Face Detection";
    manager->appendPulse(repair);
}

void AppleFaceDetection::upload(TD::TOP_Output* output,const TD::OP_TextureDesc& desc,const std::vector<float>& mask)
{
    namespace Packing=TDPlugin::TOPOutput;
    const size_t bytes=FaceDetection::imageBytes(desc.width,desc.height,Packing::bytesPerPixel(desc.pixelFormat));
    if(mask.size()!=FaceDetection::imageBytes(desc.width,desc.height,sizeof(float))/sizeof(float))
        throw std::runtime_error("Mask dimensions do not match the output");
    auto buffer=context_->createOutputBuffer(bytes,TD::TOP_BufferFlags::None,nullptr);
    if(!buffer || !buffer->data || buffer->size<bytes) throw std::runtime_error("Could not allocate the face mask output buffer");
    auto* target=static_cast<uint8_t*>(buffer->data);
    // The mask occupies every available component, including alpha.
    for(float value:mask) Packing::packPixel(target,desc.pixelFormat,value,value,value,value);
    TD::TOP_UploadInfo uploadInfo;
    uploadInfo.textureDesc=desc;
    uploadInfo.firstPixel=TD::TOP_FirstPixel::BottomLeft;
    uploadInfo.colorSpace=TD::OP_ColorSpace::Passthrough;
    output->uploadBuffer(&buffer,uploadInfo,nullptr);
}

void AppleFaceDetection::execute(TD::TOP_Output* output,const TD::OP_Inputs* inputs,void*)
{
    table_.clear(); faceCount_=detectionMs_=0; error_.clear();
    TD::OP_TextureDesc desc;
    desc.width=desc.height=1; desc.depth=1;
    desc.texDim=TD::OP_TexDim::e2D; desc.pixelFormat=TD::OP_PixelFormat::Mono32Float;
    try
    {
        const auto* input=inputs->getInputTOP(0);
        desc=TDPlugin::TOPOutput::resolvedDesc(output,input?input->textureDesc.width:1,
            input?input->textureDesc.height:1,TD::OP_PixelFormat::Mono32Float,true,inputs,
            input?input->textureDesc.pixelFormat:TD::OP_PixelFormat::Invalid);
        if(!input) throw std::runtime_error("Connect a 2D TOP image to detect faces");
        if(input->textureDesc.texDim!=TD::OP_TexDim::e2D) throw std::runtime_error("Face Detection requires a 2D TOP input");
        TD::OP_TOPInputDownloadOptions options;
        options.pixelFormat=TD::OP_PixelFormat::BGRA8Fixed;
        options.colorSpace=TD::OP_ColorSpace::SRGB;
        options.verticalFlip=true; // Vision receives an upright, top-left image.
        auto download=input->downloadTexture(options,nullptr);
        if(!download || !download->getData()) throw std::runtime_error("Could not download the TOP input");
        const auto& downloaded=download->textureDesc;
        if(downloaded.texDim!=TD::OP_TexDim::e2D || downloaded.pixelFormat!=TD::OP_PixelFormat::BGRA8Fixed
            || download->size<FaceDetection::imageBytes(downloaded.width,downloaded.height,4))
            throw std::runtime_error("Invalid BGRA image downloaded from the TOP");
        const auto start=std::chrono::steady_clock::now();
        auto faces=FaceDetection::selectFaces(detector_.detect(static_cast<const uint8_t*>(download->getData()),
            download->size,downloaded.width,downloaded.height),inputs->getParDouble("Minconfidence"),inputs->getParInt("Faces")==1);
        const float elapsed=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();
        FaceDetection::InfoTable nextTable;
        nextTable.assign(faces);
        const auto shape=FaceDetection::MaskShape(std::clamp(inputs->getParInt("Maskshape"),0,2));
        const auto mask=FaceDetection::renderMask(faces,desc.width,desc.height,shape);
        upload(output,desc,mask);
        // Commit the DAT only after the matching mask has been submitted.
        table_=std::move(nextTable);
        faceCount_=static_cast<float>(faces.size()); detectionMs_=elapsed;
        return;
    }
    catch(const std::exception& e) { error_=e.what(); }
    catch(...) { error_="Unexpected face detection failure"; }
    // Never retain old landmark rows. Clear the image too whenever the host can
    // accept an upload; allocation/upload failures remain a visible node error.
    try { upload(output,desc,FaceDetection::renderMask({},desc.width,desc.height,FaceDetection::MaskShape::Oval)); }
    catch(...) {}
}

void AppleFaceDetection::getErrorString(TD::OP_String* s,void*) { s->setString(error_.c_str()); }
void AppleFaceDetection::getWarningString(TD::OP_String* s,void*) { s->setString(dockWarning_.c_str()); }
bool AppleFaceDetection::getInfoDATSize(TD::OP_InfoDATSize* size,void*)
{ size->rows=table_.rowCount(); size->cols=FaceDetection::InfoTable::columns; size->byColumn=false; return true; }
void AppleFaceDetection::getInfoDATEntries(int32_t row,int32_t count,TD::OP_InfoDATEntries* entries,void*)
{ for(int32_t col=0;col<count;++col) entries->values[col]->setString(table_.cell(row,col)); }
void AppleFaceDetection::getInfoCHOPChan(int32_t index,TD::OP_InfoCHOPChan* channel,void*)
{
    static const char* names[]={"faces","landmark_rows","detection_ms"};
    const float values[]={faceCount_,float(table_.rowCount()-1),detectionMs_};
    if(index<0 || index>=3) return;
    channel->name->setString(names[index]); channel->value=values[index];
}
