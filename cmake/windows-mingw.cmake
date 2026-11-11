# ArtPrint Slicer : fabrication des programmes Windows 64 bits avec MinGW-w64.
# Utilisation : cmake -S . -B build/windows -DCMAKE_TOOLCHAIN_FILE=cmake/windows-mingw.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Programmes autonomes : aucune DLL du compilateur à livrer à côté.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
