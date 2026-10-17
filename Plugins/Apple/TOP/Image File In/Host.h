#pragma once
#include "CPlusPlus_Common.h"
#include <string>
namespace ImageFile {
void configurePython(TD::OP_CustomOPInfo&);
class Host {
public:
    explicit Host(uint32_t id):id_(id) {}
    ~Host();
    std::string resolve();
    std::string schedule(bool repair=false,bool initial=false);
private:
    void initialize();
    uint32_t id_;
    PyObject* globals_=nullptr;
};
}
