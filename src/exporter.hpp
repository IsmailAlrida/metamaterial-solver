#pragma once

#include "global_types.hpp"

class Exporter {
    
    public:
        Exporter();
        bool export(const LevelSet& geometry, std::string targetPlatform);

    private:

};