// Neutral host interfaces for the maintained local SDK snapshot.
// These stubs do not simulate TouchDesigner scheduling.
#pragma once
#include "TOP_CPlusPlusBase.h"
#include <string>
namespace TestHost
{
using namespace TD;
class Inputs : public OP_Inputs
{
public:
    const OP_TOPInput* source = nullptr;
    int detail = 0, method = 3, seed = 1;
    double threshold = .5;
    bool useInput = true;
    int32_t getNumInputs() const override { return {}; }
    const OP_TOPInputOpenGL* getInputTOPOpenGL(int32_t index) const override { return {}; }
    const OP_CHOPInput* getInputCHOP(int32_t index) const override { return {}; }
    const OP_DATInput* getParDAT(const char *name) const override { return {}; }
    const OP_TOPInputOpenGL* getParTOPOpenGL(const char *name) const override { return {}; }
    const OP_CHOPInput* getParCHOP(const char *name) const override { return {}; }
    const OP_ObjectInput* getParObject(const char *name) const override { return {}; }
    double getParDouble(const char* name, int32_t index = 0) const override { return threshold; }
    bool getParDouble2(const char* name, double &v0, double &v1) const override { return {}; }
    bool getParDouble3(const char* name, double &v0, double &v1, double &v2) const override { return {}; }
    bool getParDouble4(const char* name, double &v0, double &v1, double &v2, double &v3) const override { return {}; }
    int32_t getParInt(const char* name, int32_t index = 0) const override { return std::string(name) == "Datdetail" ? detail : std::string(name) == "Method" ? method : seed; }
    bool getParInt2(const char* name, int32_t &v0, int32_t &v1) const override { return {}; }
    bool getParInt3(const char* name, int32_t &v0, int32_t &v1, int32_t &v2) const override { return {}; }
    bool getParInt4(const char* name, int32_t &v0, int32_t &v1, int32_t &v2, int32_t &v3) const override { return {}; }
    const char* getParString(const char* name) const override { return useInput ? "useinput" : "rgba32float"; }
    const char* getParFilePath(const char* name) const override { return {}; }
    bool getRelativeTransform(const char* from_name, const char* to_name, double matrix[4][4]) const override { return {}; }
    void enablePar(const char* name, bool onoff) const override {  }
    const OP_DATInput* getDAT(const char *path) const override { return {}; }
    const OP_TOPInputOpenGL* getTOPOpenGL(const char *path) const override { return {}; }
    const OP_CHOPInput* getCHOP(const char *path) const override { return {}; }
    const OP_ObjectInput* getObject(const char *path) const override { return {}; }
    void* getTOPDataInCPUMemory(const OP_TOPInputOpenGL *top, const OP_TOPInputDownloadOptionsOpenGL *options) const override { return {}; }
    const OP_SOPInput* getParSOP(const char *name) const override { return {}; }
    const OP_SOPInput* getInputSOP(int32_t index) const override { return {}; }
    const OP_SOPInput* getSOP(const char *path) const override { return {}; }
    const OP_DATInput* getInputDAT(int32_t index) const override { return {}; }
    PyObject* getParPython(const char* name) const override { return {}; }
    const OP_TimeInfo* getTimeInfo() const override { return {}; }
    const OP_TOPInput* getTOP(const char* path) const override { return {}; }
    const OP_TOPInput* getInputTOP(int32_t index) const override { return source; }
    const OP_TOPInput* getParTOP(const char *name) const override { return {}; }
    const OP_POPInput* getInputPOP(int32_t index) const override { return {}; }
    const OP_POPInput* getParPOP(const char *name) const override { return {}; }
    bool getParRGB(const char* name, double &r, double &g, double &b) const override { return {}; }
    bool getParRGBA(const char* name, double &r, double &g, double &b, double &a) const override { return {}; }
    const OP_POPInput* getPOP(const char* path) const override { return {}; }
};
class ContextBase : public TOP_Context
{
public:
    PyObject* createArgumentsTuple(int numOtherArgs, void* reserved1) override { return {}; }
    PyObject* callPythonCallback(const char* functionName, PyObject* arguments, PyObject* keywords, void* reserved1) override { return {}; }
    bool beginCUDAOperations(void* reserved1) override { return {}; }
    void endCUDAOperations(void* reserved1) override {  }
    void* reservedFunc0() override { return {}; }
    void* reservedFunc1() override { return {}; }
    void* reservedFunc2() override { return {}; }
    void* reservedFunc3() override { return {}; }
    void* reservedFunc4() override { return {}; }
    void* reservedFunc5() override { return {}; }
    void* reservedFunc6() override { return {}; }
    void* reservedFunc7() override { return {}; }
    void* reservedFunc8() override { return {}; }
    void* reservedFunc9() override { return {}; }
    void* reservedFunc10() override { return {}; }
    void* reservedFunc11() override { return {}; }
    void* reservedFunc12() override { return {}; }
    void* reservedFunc13() override { return {}; }
    void* reservedFunc14() override { return {}; }
    OP_SmartRef<TOP_Buffer> createOutputBuffer(uint64_t size, TOP_BufferFlags flags, void* reserved) override { return {}; }
    void returnBuffer(OP_SmartRef<TOP_Buffer>* buf) override {  }
    int getCUDADeviceIndex(void* reserved) override { return {}; }
    void reserved1() override {  }
    void reserved2() override {  }
    void reserved3() override {  }
    void reserved4() override {  }
    void reserved5() override {  }
    void reserved6() override {  }
    void reserved7() override {  }
    void reserved8() override {  }
    void reserved9() override {  }
};
class OutputBase : public TOP_Output
{
public:
    void uploadBuffer(OP_SmartRef<TOP_Buffer>* buf, const TOP_UploadInfo& info, void* reserved) override {  }
    const OP_CUDAArrayInfo* createCUDAArray(const TOP_CUDAOutputInfo& info, void* reserved) override { return {}; }
    void getSuggestedOutputDesc(OP_TextureDesc* desc, void* reserved) override {  }
    void reserved1() override {  }
    void reserved2() override {  }
    void reserved3() override {  }
    void reserved4() override {  }
    void reserved5() override {  }
    void reserved6() override {  }
    void reserved7() override {  }
    void reserved8() override {  }
    void reserved9() override {  }
};
class InputBase : public OP_TOPInput
{
public:
    OP_SmartRef<OP_TOPDownloadResult> downloadTexture(const OP_TOPInputDownloadOptions& opts, void* reserved1) const override { return {}; }
    const OP_CUDAArrayInfo* getCUDAArray(const OP_CUDAAcquireInfo& info, void* reserved2) const override { return {}; }
    void* reserved0() override { return {}; }
    void* reserved1() override { return {}; }
    void* reserved2() override { return {}; }
    void* reserved3() override { return {}; }
    void* reserved4() override { return {}; }
};
}
