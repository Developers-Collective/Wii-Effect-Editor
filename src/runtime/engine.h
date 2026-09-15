#pragma once
#include "archive.h"
#include <optional>

namespace breff {
class Engine {
    struct State;
    std::unique_ptr<State> state;
public:
    Engine();
    ~Engine();
    void reset();
    void start(Archive& archive,const std::string& name,std::optional<uint16_t> seed);
    void step();
    void draw(const nw4r::ef::DrawInfo& info);
    unsigned particles() const;
    bool finished() const;
};
}
