#pragma once
#include "archive.h"
#include <optional>
#include <array>

namespace breff {
    class Engine {
        struct State;
        std::unique_ptr<State> state;

      public:
        Engine();
        ~Engine();
        void reset();
        void start(Archive& archive, const std::string& name, std::optional<uint16_t> seed);
        void step();
        void setView(const nw4r::ef::DrawInfo& info);
        void draw(const nw4r::ef::DrawInfo& info);
        uint64_t visualSignature() const;
        unsigned particles() const;
        bool finished() const;

        struct Bounds {
            std::array<float, 3> minimum{}, maximum{};
            bool empty = true;
            void include(const Bounds& other);
        };

        Bounds bounds() const;
    };
}
