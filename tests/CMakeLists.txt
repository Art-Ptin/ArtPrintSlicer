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

# Slicer 3 axes : coupe en couches, parois, remplissage, G-code vérifié.
add_executable(test_slicer
    test_slicer.cpp)
target_link_libraries(test_slicer PRIVATE artprint_core)
add_test(NAME slicer COMMAND test_slicer)

# Commandes de l'interface graphique : ouvrir, régler, trancher, enregistrer.
add_executable(test_api
    test_api.cpp)
target_link_libraries(test_api PRIVATE artprint_core)
add_test(NAME api COMMAND test_api)

# Collisions avec la forme de la machine (machine d'essai inventée).
add_executable(test_collisions
    test_collisions.cpp)
target_link_libraries(test_collisions PRIVATE artprint_core)
add_test(NAME collisions COMMAND test_collisions)

# 5 axes : découpe par plan, squelette, stratégies, G-code multi-directions vérifié.
add_executable(test_strategie
    test_strategie.cpp)
target_link_libraries(test_strategie PRIVATE artprint_core)
add_test(NAME strategie COMMAND test_strategie)

# Banc d'essai des stratégies sur une série d'objets (long : lancé à la main).
add_executable(banc_strategies
    banc_strategies.cpp)
target_link_libraries(banc_strategies PRIVATE artprint_core)

# Fabrique de modèles d'essai (des milliers de pièces différentes, par famille
# et par graine), pour les essais en grand nombre : voir outils/essais_de_masse.
add_executable(generateur_modeles
    generateur_modeles.cpp)
target_link_libraries(generateur_modeles PRIVATE artprint_core)
