# ArtPrint Slicer : fichier principal de compilation.
cmake_minimum_required(VERSION 3.25)

project(ArtPrintSlicer
    VERSION 0.0.1
    DESCRIPTION "Slicer 5 axes continu"
    LANGUAGES CXX)

# Le projet utilise le C++20, sans extensions propres au compilateur.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Avec MSVC : fichiers source lus en UTF-8 et avertissements stricts.
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

add_subdirectory(src)

# Les tests sont lancés avec CTest.
enable_testing()
add_subdirectory(tests)
