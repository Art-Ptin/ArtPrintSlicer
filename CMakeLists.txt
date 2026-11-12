# ArtPrint Slicer : fichier principal de compilation.
cmake_minimum_required(VERSION 3.25)

project(ArtPrintSlicer
    VERSION 0.1.0
    DESCRIPTION "Slicer 5 axes continu"
    LANGUAGES CXX)

# Le projet utilise le C++20, sans extensions propres au compilateur.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Avec MSVC : programmes autonomes, sans « Visual C++ Redistributable » à installer.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Avec MSVC : fichiers source lus en UTF-8 et avertissements stricts.
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

# Clipper2 : bibliothèque de calcul sur les polygones (licence Boost).
# CMake la télécharge tout seul à la première compilation.
include(FetchContent)
set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG ef88ee97c0e759792e43a2b2d8072def6c9244e8  # version 1.5.4
    GIT_SHALLOW FALSE
    SOURCE_SUBDIR CPP)
FetchContent_MakeAvailable(clipper2)

# nlohmann/json : lecture et écriture du JSON échangé avec l'interface (licence MIT).
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json)

add_subdirectory(src)

# Les tests sont lancés avec CTest.
enable_testing()
add_subdirectory(tests)

# --- Installation et installateur Windows ------------------------------------
# « cmake --install <build> --component artprint --prefix <dossier> » rassemble
# les fichiers du logiciel ; la cible « installateur » en fait un Setup.exe.
if(WIN32)
    install(TARGETS artprint-slicer ArtPrintSlicer RUNTIME DESTINATION . COMPONENT artprint)
    install(FILES third_party/webview2/x64/WebView2Loader.dll DESTINATION . COMPONENT artprint)
    # L'interface du logiciel est celle de la maquette, branchée sur le vrai
    # slicer : elle est assemblée à chaque construction (voir le script).
    set(DOSSIER_INTERFACE ${CMAKE_BINARY_DIR}/interface/ui)
    add_custom_target(interface ALL
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${PROJECT_SOURCE_DIR} -DDEST=${DOSSIER_INTERFACE}
            -P ${PROJECT_SOURCE_DIR}/outils/assembler_interface.cmake
        COMMENT "Assemblage de l'interface"
        VERBATIM)
    install(DIRECTORY ${DOSSIER_INTERFACE} DESTINATION . COMPONENT artprint)
    install(FILES exemples/reglages.exemple.txt DESTINATION exemples COMPONENT artprint)
    install(FILES outils/convertir_machine.py DESTINATION outils COMPONENT artprint)
    install(FILES LICENSE LICENCES-TIERCES.txt DESTINATION . COMPONENT artprint)
    install(FILES ${clipper2_SOURCE_DIR}/LICENSE DESTINATION licences RENAME Clipper2.txt
        COMPONENT artprint)
    install(FILES ${json_SOURCE_DIR}/LICENSE.MIT DESTINATION licences RENAME nlohmann-json.txt
        COMPONENT artprint)
    install(FILES ui/lib/LICENCE-three.txt DESTINATION licences RENAME three.js.txt
        COMPONENT artprint)

    find_program(MAKENSIS makensis
        PATHS "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS"
              "$ENV{ProgramFiles\(x86\)}/NSIS" "$ENV{ProgramFiles}/NSIS")
    if(NOT MAKENSIS)
        message(STATUS "NSIS introuvable : la cible « installateur » ne sera pas disponible.")
    endif()
    if(MAKENSIS)
        set(DOSSIER_PAQUET ${CMAKE_BINARY_DIR}/paquet)
        set(FICHIER_SETUP ${CMAKE_BINARY_DIR}/ArtPrintSlicer-Setup-${PROJECT_VERSION}.exe)
        add_custom_target(installateur
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${DOSSIER_PAQUET}
            COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --component artprint
                --prefix ${DOSSIER_PAQUET} --config $<CONFIG>
            COMMAND ${MAKENSIS} -V2 -INPUTCHARSET UTF8 -DSOURCE=${DOSSIER_PAQUET} -DVERSION=${PROJECT_VERSION}
                -DSORTIE=${FICHIER_SETUP} ${PROJECT_SOURCE_DIR}/installateur/artprint.nsi
            DEPENDS artprint-slicer ArtPrintSlicer interface
            COMMENT "Fabrication de l'installateur ${FICHIER_SETUP}"
            VERBATIM)
    endif()
endif()
