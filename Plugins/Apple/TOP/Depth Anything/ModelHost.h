#pragma once
#include "CPlusPlus_Common.h"
#include <string>

namespace DepthModels {
void configurePython(TD::OP_CustomOPInfo& info);
class Host {
public:
    explicit Host(uint32_t id) : id_(id) {}
    ~Host();
    std::string schedule(const std::string& resources, bool copy);
private:
    void initialize();
    uint32_t id_;
    PyObject* globals_ = nullptr;
};
}
