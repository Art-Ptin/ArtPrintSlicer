# Bibliothèque commune : le code du slicer, partagé par le programme et les tests.
add_library(artprint_core
    version.cpp
    maillage.cpp
    stl.cpp
    reglages.cpp
    cinematique.cpp
    gcode.cpp
    matiere.cpp
    verificateur.cpp)
target_include_directories(artprint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# La version vient de project() dans le CMakeLists.txt principal.
target_compile_definitions(artprint_core PRIVATE
    ARTPRINT_VERSION="${PROJECT_VERSION}")

# Programme en ligne de commande.
add_executable(artprint-slicer
    main.cpp)
target_link_libraries(artprint-slicer PRIVATE artprint_core)
