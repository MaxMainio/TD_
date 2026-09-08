#include <Python.h>
#include "Docking.h"
#include "DockingScript.h"
#include "SegmentUV.h"
#include <exception>

namespace Segment
{
namespace
{
class GIL
{
public:
    GIL() : state_(PyGILState_Ensure()) {}
    ~GIL() { PyGILState_Release(state_); }
private:
    PyGILState_STATE state_;
};
class Ref
{
public:
    explicit Ref(PyObject* object) : object_(object) {}
    ~Ref() { Py_XDECREF(object_); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    PyObject* get() const { return object_; }
private:
    PyObject* object_;
};

std::string pythonError()
{
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    Ref t(type), v(value), tb(traceback);
    Ref message(value ? PyObject_Str(value) : nullptr);
    const char* text = message.get() ? PyUnicode_AsUTF8(message.get()) : nullptr;
    std::string error = "Could not schedule Segment Info DAT: ";
    error += text ? text : "TouchDesigner Python setup failed";
    PyErr_Clear();
    return error;
}

PyObject* dockResult(PyObject* self, PyObject* argument)
{
    try
    {
        const char* message = PyUnicode_AsUTF8(argument);
        if (!message) return nullptr;
        auto* object = reinterpret_cast<TD::PY_Struct*>(self);
        TD::PY_GetInfo info;
        info.autoCook = false;
        auto* instance = static_cast<SegmentUV*>(object->context->getNodeInstance(info));
        if (!instance) return nullptr;
        instance->setDockWarning(message);
        object->context->makeNodeDirty();
        Py_RETURN_NONE;
    }
    catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, e.what()); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError, "Segment docking status update failed"); }
    return nullptr;
}
PyMethodDef methods[] = {
    {"_segmentDockResult", dockResult, METH_O, "Internal deferred docking status callback."},
    {nullptr, nullptr, 0, nullptr}
};
}

void configureDocking(TD::OP_CustomOPInfo& info)
{
    info.pythonVersion->setString(PY_VERSION);
    info.pythonMethods = methods;
    info.cookOnStart = true;
}

std::string scheduleDocking(uint32_t nodeId, bool resetLayout)
{
    if (!Py_IsInitialized()) return "Segment Info DAT setup requires TouchDesigner's Python runtime";
    GIL gil;
    Ref module(PyImport_ImportModule("td"));
    if (!module.get()) return pythonError();
    Ref run(PyObject_GetAttrString(module.get(), "run"));
    if (!run.get()) return pythonError();
    Ref arguments(Py_BuildValue("(sIO)", SegmentDockingScript, nodeId, resetLayout ? Py_True : Py_False));
    Ref keywords(Py_BuildValue("{s:O}", "endFrame", Py_True));
    if (!arguments.get() || !keywords.get()) return pythonError();
    Ref result(PyObject_Call(run.get(), arguments.get(), keywords.get()));
    return result.get() ? std::string{} : pythonError();
}
}
