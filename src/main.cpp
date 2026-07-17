#include "gui/application.hpp"
#include <iostream>
#include <cstdlib>
int main() {
    try {
        gui::Application app;
        app.run();
    } 
    catch (const std::exception& e) {
        // cerr for errors
        std::cerr << "Fatal error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}