#include <Python.h>
#include "Host.h"
#include "HostScript.h"
#include "AppleImageFileIn.h"
#include <stdexcept>
namespace ImageFile {
namespace {
struct GIL { PyGILState_STATE state=PyGILState_Ensure(); ~GIL(){PyGILState_Release(state);} };
struct Ref { PyObject* p; explicit Ref(PyObject* v):p(v){} ~Ref(){Py_XDECREF(p);} Ref(const Ref&)=delete; };
std::string error() {
    PyObject *type=nullptr,*value=nullptr,*trace=nullptr;
    PyErr_Fetch(&type,&value,&trace); PyErr_NormalizeException(&type,&value,&trace);
    Ref t(type),v(value),tb(trace),message(value?PyObject_Str(value):nullptr);
    const char* s=message.p?PyUnicode_AsUTF8(message.p):nullptr;
    std::string result=s?s:"TouchDesigner Python operation failed";PyErr_Clear();return result;
}
AppleImageFileIn* instance(PyObject* self) {
    auto* object=reinterpret_cast<TD::PY_Struct*>(self);TD::PY_GetInfo info;info.autoCook=false;
    return static_cast<AppleImageFileIn*>(object->context->getNodeInstance(info));
}
PyObject* dockResult(PyObject* self,PyObject* argument) {
    try {
        const char* s=PyUnicode_AsUTF8(argument);if(!s)return nullptr;
        auto* p=instance(self);if(!p)return nullptr;p->setDockWarning(s);
        reinterpret_cast<TD::PY_Struct*>(self)->context->makeNodeDirty();Py_RETURN_NONE;
    } catch(const std::exception& e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject* dockState(PyObject* self,PyObject*) {
    try { auto* p=instance(self);if(!p)return nullptr;
        auto s=p->snapshot();return Py_BuildValue("(ssIi)",s->path.c_str(),s->status.c_str(),p->availableMask(),int(s->selectionFallback));
    } catch(const std::exception& e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject* pollState(PyObject* self,PyObject*) {
    try {auto* p=instance(self);if(!p)return nullptr;return PyBool_FromLong(p->needsPublish());}
    catch(const std::exception& e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject* setDemand(PyObject* self,PyObject* argument) {
    try {
        auto mask=PyLong_AsUnsignedLong(argument);if(PyErr_Occurred())return nullptr;
        auto* p=instance(self);if(!p)return nullptr;
        bool changed=p->setDemand(uint32_t(mask));
        if(changed)reinterpret_cast<TD::PY_Struct*>(self)->context->makeNodeDirty();
        return PyBool_FromLong(changed);
    } catch(const std::exception& e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyMethodDef methods[]={
    {"_imageDockResult",dockResult,METH_O,"Internal deferred docking status."},
    {"_imageDockState",dockState,METH_NOARGS,"Internal published image inventory."},
    {"_imageNeedsPublish",pollState,METH_NOARGS,"Internal worker completion check; host thread only."},
    {"_imageSetDemand",setDemand,METH_O,"Internal helper demand; host thread only."},
    {nullptr,nullptr,0,nullptr}};
}
void configurePython(TD::OP_CustomOPInfo& info) {info.pythonVersion->setString(PY_VERSION);info.pythonMethods=methods;info.cookOnStart=true;}
Host::~Host(){if(globals_&&Py_IsInitialized()){GIL gil;Ref result(PyObject_CallObject(PyDict_GetItemString(globals_,"shutdown"),nullptr));if(!result.p)PyErr_Clear();Py_DECREF(globals_);}}
void Host::initialize(){
    if(globals_)return;
    Ref globals(PyDict_New());if(!globals.p)throw std::runtime_error(error());
    Ref result(PyRun_String(ImageHostScript,Py_file_input,globals.p,globals.p));
    if(!result.p)throw std::runtime_error(error());
    globals_=globals.p;Py_INCREF(globals_);
}
std::string Host::resolve(){
    if(!Py_IsInitialized())throw std::runtime_error("File resolution requires TouchDesigner's Python runtime");
    GIL gil;initialize();Ref args(Py_BuildValue("(I)",id_));
    Ref result(PyObject_CallObject(PyDict_GetItemString(globals_,"resolve"),args.p));
    if(!result.p)throw std::runtime_error(error());
    const char* s=PyUnicode_AsUTF8(result.p);if(!s)throw std::runtime_error(error());return s;
}
std::string Host::schedule(bool repair,bool initial){
    if(!Py_IsInitialized())return "Docking requires TouchDesigner's Python runtime";
    GIL gil;
    try {initialize();Ref args(Py_BuildValue("(Iii)",id_,int(repair),int(initial)));
        Ref result(PyObject_CallObject(PyDict_GetItemString(globals_,"schedule"),args.p));
        return result.p?std::string{}:error();
    }catch(const std::exception& e){return e.what();}
}
}
