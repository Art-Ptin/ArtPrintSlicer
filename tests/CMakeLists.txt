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

# Cinématique 5 axes : calcul direct, calcul inverse et chemin le plus court de C.
add_executable(test_cinematique
    test_cinematique.cpp)
target_link_libraries(test_cinematique PRIVATE artprint_core)
add_test(NAME cinematique COMMAND test_cinematique)

# Vérificateur de G-code : un G-code correct passe, chaque faute est attrapée.
add_executable(test_verificateur
    test_verificateur.cpp)
target_link_libraries(test_verificateur PRIVATE artprint_core)
add_test(NAME verificateur COMMAND test_verificateur)
