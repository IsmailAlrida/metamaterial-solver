#pragma once
#include <string>
#include <iostream> 
#include <vector>
#include "app.hpp"

/*
So interesting thing while on the topic, the compiler will apparently
compile each cpp file on its own into obj files, then the linker will later 
link them to make the full app

so why this matters for includes is that relevant cpp files like for example main here 
and app.cpp will only really need to pull app.hpp's header files which copy-pastes it into that
file during compile time so the compiler knows what the declarations look like.

hence, we only put the declarations in the header files and implement in cpp.
*/
int main() {

    MetamaterialDesigner::App app;
    std::string glvisHost = "localhost";
    int glvisPort = 19916;

    // Stuff imgui stuff into here
    app.setupGui();
    app.setupGlvis(glvisHost, glvisPort);
    
    while (!app.shouldClose){
        app.frame();
    };


}