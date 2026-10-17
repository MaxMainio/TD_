#include <Python.h>
#include "ModelHost.h"
#include "ModelHostScript.h"
// Avoid Objective-C types in the Python bridge.
#include <stdexcept>

namespace DepthModels {
namespace {
struct GIL { PyGILState_STATE state=PyGILState_Ensure(); ~GIL(){PyGILState_Release(state);} };
struct Ref { PyObject* p; explicit Ref(PyObject* v):p(v){} ~Ref(){Py_XDECREF(p);} Ref(const Ref&)=delete; };
std::string error() {
    PyObject *type=nullptr,*value=nullptr,*trace=nullptr;
    PyErr_Fetch(&type,&value,&trace); PyErr_NormalizeException(&type,&value,&trace);
    Ref t(type),v(value),tb(trace),message(value?PyObject_Str(value):nullptr);
    const char* s=message.p?PyUnicode_AsUTF8(message.p):nullptr;
    std::string result=s?s:"TouchDesigner model helper failed"; PyErr_Clear(); return result;
}
}
// Result reporting is implemented beside AppleDepth to keep its Objective-C
// declarations out of this translation unit.
extern PyMethodDef modelMethods[];
void configurePython(TD::OP_CustomOPInfo& info) {
    info.pythonVersion->setString(PY_VERSION);
    info.pythonMethods=modelMethods;
    info.cookOnStart=true;
}
Host::~Host() {
    if(globals_ && Py_IsInitialized()) {
        GIL gil;
        Ref result(PyObject_CallObject(PyDict_GetItemString(globals_,"shutdown"),nullptr));
        if(!result.p) PyErr_Clear();
        Py_DECREF(globals_);
    }
}
void Host::initialize() {
    if(globals_) return;
    Ref globals(PyDict_New());
    if(!globals.p) throw std::runtime_error(error());
    Ref result(PyRun_String(DepthModelHostScript,Py_file_input,globals.p,globals.p));
    if(!result.p) throw std::runtime_error(error());
    globals_=globals.p; Py_INCREF(globals_);
}
std::string Host::schedule(const std::string& resources,bool copy) {
    if(!Py_IsInitialized()) return "Model project actions require TouchDesigner's Python runtime";
    GIL gil;
    try {
        initialize();
        Ref args(Py_BuildValue("(Isi)",id_,resources.c_str(),int(copy)));
        Ref result(PyObject_CallObject(PyDict_GetItemString(globals_,"schedule"),args.p));
        return result.p?std::string{}:error();
    } catch(const std::exception& e) { return e.what(); }
}
}
