#pragma once

#include "global_types.hpp"

namespace App {

class Exporter {
    
    public:
        Exporter();
        bool export(const LevelSet& geometry, std::string targetPlatform);

    private:

};

} // namespace App
