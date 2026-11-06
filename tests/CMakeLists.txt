# Premier test : vérifie la version et le texte de présentation.
add_executable(test_version
    test_version.cpp)
target_link_libraries(test_version PRIVATE artprint_core)

add_test(NAME version COMMAND test_version)
