# Bibliothèque commune : le code du slicer, partagé par le programme et les tests.
add_library(artprint_core
    version.cpp
    maillage.cpp
    stl.cpp
    reglages.cpp
    cinematique.cpp
    gcode.cpp
    matiere.cpp
    verificateur.cpp
    tranchage.cpp
    trajectoires.cpp
    slicer.cpp
    api.cpp
    machine.cpp
    champ_distance.cpp
    collisions.cpp
    decoupe.cpp
    connecteurs.cpp
    squelette.cpp
    champ_couches.cpp
    couches_courbes.cpp
    peau_courbe.cpp
    strategie.cpp
    multiaxe.cpp)
target_include_directories(artprint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(artprint_core PUBLIC Clipper2 nlohmann_json::nlohmann_json)

# La version vient de project() dans le CMakeLists.txt principal.
target_compile_definitions(artprint_core PRIVATE
    ARTPRINT_VERSION="${PROJECT_VERSION}")

# Programme en ligne de commande.
add_executable(artprint-slicer
    main.cpp)
target_link_libraries(artprint-slicer PRIVATE artprint_core)

# Avec MinGW, le point d'entrée Unicode « wmain » doit être demandé à l'éditeur de liens.
if(MINGW)
    target_link_options(artprint-slicer PRIVATE -municode)
endif()

# Programme avec fenêtre (Windows seulement) : la page de l'interface est
# affichée par le composant WebView2 de Microsoft Edge.
if(WIN32)
    set(WEBVIEW2_DIR ${PROJECT_SOURCE_DIR}/third_party/webview2)
    add_executable(ArtPrintSlicer WIN32
        gui/fenetre.cpp
        gui/ressources.rc)
    # (L'icône du programme est dans le dossier de l'installateur.)
    target_include_directories(ArtPrintSlicer PRIVATE ${WEBVIEW2_DIR}/include ${PROJECT_SOURCE_DIR}/installateur)
    target_link_libraries(ArtPrintSlicer PRIVATE artprint_core ole32 shell32 comdlg32 uuid)
    if(MINGW)
        target_include_directories(ArtPrintSlicer PRIVATE ${WEBVIEW2_DIR}/mingw)
        target_link_options(ArtPrintSlicer PRIVATE -municode)
    endif()
    # À côté du programme : la DLL de WebView2 et le dossier de l'interface.
    add_custom_command(TARGET ArtPrintSlicer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${WEBVIEW2_DIR}/x64/WebView2Loader.dll $<TARGET_FILE_DIR:ArtPrintSlicer>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/ui $<TARGET_FILE_DIR:ArtPrintSlicer>/ui)
endif()
