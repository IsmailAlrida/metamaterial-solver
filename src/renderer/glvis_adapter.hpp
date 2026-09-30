#pragma once

#include <memory>

union SDL_Event;

namespace App {

class GlvisAdapter {
    public:
        static constexpr int Port = 19916;

        GlvisAdapter();
        ~GlvisAdapter();

        GlvisAdapter(const GlvisAdapter&) = delete;
        GlvisAdapter& operator=(const GlvisAdapter&) = delete;

        void processEvent(const SDL_Event& event);
        void draw();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
};

} // namespace App
