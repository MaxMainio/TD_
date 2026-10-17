#include <Python.h>
#include "DAT_CPlusPlusBase.h"
#include "Metadata.h"
#include "ResolverScript.h"
#include <array>
#include <dlfcn.h>
#include <filesystem>
#include <stdexcept>

extern "C" DLLEXPORT void FillDATPluginInfo(TD::DAT_PluginInfo* info);
namespace {
struct GIL {
    PyGILState_STATE state = PyGILState_Ensure();
    ~GIL() { PyGILState_Release(state); }
};
struct Ref {
    PyObject* object;
    explicit Ref(PyObject* p) : object(p) {}
    ~Ref() { Py_XDECREF(object); }
    Ref(const Ref&) = delete;
};
std::string pythonError() {
    PyObject *type = nullptr, *value = nullptr, *traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    Ref t(type), v(value), trace(traceback), message(value ? PyObject_Str(value) : nullptr);
    const char* text = message.object ? PyUnicode_AsUTF8(message.object) : nullptr;
    std::string result = text ? text : "TouchDesigner Python path resolution failed";
    PyErr_Clear(); return result;
}
void check(TD::OP_ParAppendResult result) {
    if (result != TD::OP_ParAppendResult::Success) throw std::runtime_error("Cannot create MetaData_In parameters");
}
bool provided(const char* value) {
    return value && std::string(value).find_first_not_of(" \t\r\n") != std::string::npos;
}
class MetaDataInDAT final : public TD::DAT_CPlusPlusBase {
public:
    explicit MetaDataInDAT(const TD::OP_NodeInfo* info) : nodeId_(info->opId) {
        Dl_info library{};
        if (dladdr(reinterpret_cast<const void*>(&FillDATPluginInfo), &library) && library.dli_fname)
            resources_ = (std::filesystem::path(library.dli_fname).parent_path().parent_path() / "Resources").string();
    }
    ~MetaDataInDAT() override {
        if (globals_ && Py_IsInitialized()) { GIL gil; Py_DECREF(globals_); }
    }
    void getGeneralInfo(TD::DAT_GeneralInfo* info, const TD::OP_Inputs*, void*) override {
        info->cookEveryFrame = false; info->cookEveryFrameIfAsked = true;
    }
    void setupParameters(TD::OP_ParameterManager* manager, void*) override {
        TD::OP_StringParameter p;
        p.page = "Source"; p.name = "File"; p.label = "File"; p.defaultValue = "";
        p.help = "Select one file. Clear this field to use an Operator instead.";
        check(manager->appendFile(p));
        p.name = "Operator"; p.label = "Operator";
        p.help = "Select an operator with a File parameter. Clear this field to select a File instead.";
        check(manager->appendOP(p));
        TD::OP_NumericParameter n;
        n.page = "Source"; n.name = "Autorefresh"; n.label = "Auto Refresh"; n.defaultValues[0] = 1;
        n.section = true;
        check(manager->appendToggle(n));
        TD::OP_NumericParameter pulse;
        pulse.page = "Source"; pulse.name = "Refresh"; pulse.label = "Refresh"; check(manager->appendPulse(pulse));
        TD::OP_NumericParameter deep;
        deep.page = "Source"; deep.name = "Deepscan"; deep.label = "Deep Scan"; check(manager->appendToggle(deep));
        n.page = "Technical"; n.section = false;
        n.name = "Checkinterval"; n.label = "Check Interval (s)"; n.defaultValues[0] = 1;
        n.minValues[0] = .1; n.maxValues[0] = 86400; n.clampMins[0] = n.clampMaxes[0] = true;
        n.minSliders[0] = .1; n.maxSliders[0] = 10; check(manager->appendFloat(n));
        n.name = "Readtimeout"; n.label = "Read Timeout (s)"; n.defaultValues[0] = 30;
        n.minSliders[0] = 1; n.maxSliders[0] = 300; check(manager->appendFloat(n));
    }
    void setParameterEnableStates(const TD::OP_Inputs* inputs, TD::OP_ParEnableState* states, void*) override {
        const bool file = provided(inputs->getParString("File"));
        const bool op = provided(inputs->getParString("Operator"));
        // A conflicting saved/scripted state must stay editable, not lock both controls.
        states->setEnableState("File", file || !op);
        states->setEnableState("Operator", op || !file);
        states->setEnableState("Checkinterval", inputs->getParInt("Autorefresh") != 0);
    }
    void pulsePressed(const char* name, void*) override { if (std::string(name) == "Refresh") refresh_ = true; }
    void execute(TD::DAT_Output* output, const TD::OP_Inputs* inputs, void*) override {
        output->setOutputDataType(TD::DAT_OutDataType::Table);
        try {
            Metadata::Request r;
            r.path = resolve(inputs); r.resources = resources_;
            r.deep = inputs->getParInt("Deepscan") != 0;
            r.automatic = inputs->getParInt("Autorefresh") != 0;
            r.interval = inputs->getParDouble("Checkinterval"); r.timeout = inputs->getParDouble("Readtimeout");
            engine_.request(r, refresh_); refresh_ = false;
            result_ = engine_.result(); error_ = result_->error;
            warning_.clear();
            for (const auto& warning : result_->warnings) { if (!warning_.empty()) warning_ += '\n'; warning_ += warning; }
            int32_t rows = 0, cols = 0;
            output->getTableSize(&rows, &cols);
            if (displayedRevision_ != result_->revision || hadError_ ||
                rows != int32_t(result_->rows.size() + 1) || cols != 4) {
                write(output, result_->rows); displayedRevision_ = result_->revision; hadError_ = false;
            }
            updateInfo();
        } catch (const std::exception& e) {
            error_ = Metadata::cleanUTF8(e.what()); warning_.clear();
            Metadata::Request empty; engine_.request(empty);
            result_ = engine_.result();
            write(output, {}); hadError_ = true; updateInfo();
        } catch (...) {
            error_ = "Unexpected MetaData_In failure"; warning_.clear();
            Metadata::Request empty; engine_.request(empty); result_ = engine_.result();
            write(output, {}); hadError_ = true; updateInfo();
        }
    }
    void getErrorString(TD::OP_String* out, void*) override { out->setString(error_.c_str()); }
    void getWarningString(TD::OP_String* out, void*) override { out->setString(warning_.c_str()); }
    void getInfoPopupString(TD::OP_String* out, void*) override {
        std::string value = "MetaData_In 0.1.1\n";
        for (const auto& row : info_) value += row[0] + ": " + row[1] + '\n';
        out->setString(value.c_str());
    }
    bool getInfoDATSize(TD::OP_InfoDATSize* size, void*) override {
        size->rows = int32_t(info_.size()); size->cols = 2; size->byColumn = false; return true;
    }
    void getInfoDATEntries(int32_t index, int32_t count, TD::OP_InfoDATEntries* out, void*) override {
        if (index < 0 || size_t(index) >= info_.size()) return;
        for (int i=0; i < count && i < 2; ++i) out->values[i]->setString(info_[size_t(index)][size_t(i)].c_str());
    }
    int32_t getNumInfoCHOPChans(void*) override { return 5; }
    void getInfoCHOPChan(int32_t index, TD::OP_InfoCHOPChan* out, void*) override {
        static const char* names[] = {"reading", "rows", "reads", "read_ms", "limited"};
        if (index < 0 || index >= 5) return;
        out->name->setString(names[index]);
        const double values[] = {result_->status == "reading" ? 1. : 0., double(result_->rows.size()),
            double(result_->reads), result_->elapsedMs, result_->status == "partial" ? 1. : 0.};
        out->value = float(values[index]);
    }
private:
    std::string resolve(const TD::OP_Inputs* inputs) {
        if (!Py_IsInitialized()) throw std::runtime_error("Operator references require TouchDesigner's Python runtime; install as a Custom OP");
        GIL gil;
        if (!globals_) {
            Ref globals(PyDict_New());
            if (!globals.object) throw std::runtime_error(pythonError());
            Ref ran(PyRun_String(metadataResolverScript, Py_file_input, globals.object, globals.object));
            if (!ran.object) throw std::runtime_error(pythonError());
            globals_ = globals.object; Py_INCREF(globals_);
        }
        Ref td(PyImport_ImportModule("td"));
        if (!td.object) throw std::runtime_error(pythonError());
        Ref node(PyObject_CallMethod(td.object, "op", "I", nodeId_));
        if (!node.object) throw std::runtime_error(pythonError());
        Ref args(Py_BuildValue("(Oii)", node.object,
            int(provided(inputs->getParString("File"))), int(provided(inputs->getParString("Operator")))));
        if (!args.object) throw std::runtime_error(pythonError());
        Ref result(PyObject_CallObject(PyDict_GetItemString(globals_, "resolve"), args.object));
        if (!result.object) throw std::runtime_error(pythonError());
        const char* path = PyUnicode_AsUTF8(result.object);
        if (!path) throw std::runtime_error(pythonError());
        return path;
    }
    static void write(TD::DAT_Output* output, const std::vector<Metadata::Row>& rows) {
        output->setTableSize(int32_t(rows.size()+1), 4);
        const char* header[] = {"key", "value", "group", "source"};
        for (int c=0; c<4; ++c) output->setCellString(0,c,header[c]);
        int32_t row = 1;
        for (const auto& value : rows) {
            const std::string* cells[] = {&value.key,&value.value,&value.group,&value.source};
            for (int c=0;c<4;++c) output->setCellString(row,c,cells[c]->c_str());
            ++row;
        }
    }
    void updateInfo() {
        info_ = {{{"status", error_.empty() ? result_->status : "error"}, {"file", result_->path},
            {"rows", std::to_string(result_->rows.size())}, {"reads", std::to_string(result_->reads)},
            {"read_ms", std::to_string(result_->elapsedMs)}, {"exiftool_version",result_->exifVersion},
            {"ffprobe_version",result_->ffprobeVersion}, {"warnings",warning_}, {"error",error_}}};
    }
    uint32_t nodeId_;
    std::string resources_, error_, warning_;
    PyObject* globals_ = nullptr;
    Metadata::Engine engine_;
    std::shared_ptr<const Metadata::Result> result_ = std::make_shared<Metadata::Result>();
    uint64_t displayedRevision_ = UINT64_MAX;
    bool refresh_ = false, hadError_ = false;
    std::array<std::array<std::string,2>,9> info_{};
};
}
extern "C" {
DLLEXPORT void FillDATPluginInfo(TD::DAT_PluginInfo* info) {
    if (!info->setAPIVersion(TD::DATCPlusPlusAPIVersion)) return;
    auto& custom = info->customOPInfo;
    custom.opType->setString("Metadatain"); custom.opLabel->setString("MetaData_In");
    custom.opIcon->setString("MDI"); custom.authorName->setString("Max Beidler");
    custom.minInputs = custom.maxInputs = 0;
    custom.pythonVersion->setString(PY_VERSION); custom.cookOnStart = true;
}
DLLEXPORT TD::DAT_CPlusPlusBase* CreateDATInstance(const TD::OP_NodeInfo* info) { return new MetaDataInDAT(info); }
DLLEXPORT void DestroyDATInstance(TD::DAT_CPlusPlusBase* instance) { delete instance; }
}
