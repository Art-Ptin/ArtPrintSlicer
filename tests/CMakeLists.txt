# Premier test : vérifie la version et le texte de présentation.
add_executable(test_version
    test_version.cpp)
target_link_libraries(test_version PRIVATE artprint_core)
add_test(NAME version COMMAND test_version)

# Lecture STL : cube de 20 mm fabriqué par le test, en binaire et en texte.
add_executable(test_stl
    test_stl.cpp)
target_link_libraries(test_stl PRIVATE artprint_core)
add_test(NAME stl COMMAND test_stl)
