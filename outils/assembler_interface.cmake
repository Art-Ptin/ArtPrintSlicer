# ArtPrint Slicer - licence AGPL v3
# Assemble l'interface du logiciel installé à partir de la maquette :
#   - les pages, styles et scripts de « maquette/ » ;
#   - le vrai pont (maquette/pont-reel.js) à la place du pont de simulation ;
#   - la vue 3D et three.js de « ui/ » ;
#   - une page complète autour du contenu de maquette/index.html.
# Utilisation : cmake -DSOURCE=<racine du projet> -DDEST=<dossier ui à créer> -P assembler_interface.cmake

if(NOT SOURCE OR NOT DEST)
    message(FATAL_ERROR "Il faut donner SOURCE (la racine du projet) et DEST (le dossier à créer).")
endif()

file(REMOVE_RECURSE "${DEST}")
file(MAKE_DIRECTORY "${DEST}")

# Les feuilles de style et les scripts de l'interface. Ceux qui ne servent
# qu'à la simulation ou à la planche de design ne sont pas embarqués.
file(GLOB fichiers "${SOURCE}/maquette/*.css" "${SOURCE}/maquette/*.js")
foreach(fichier ${fichiers})
    get_filename_component(nom "${fichier}" NAME)
    if(NOT nom MATCHES "^(pont\\.js|pont-reel\\.js|planche\\.js|planche\\.css)$")
        file(COPY "${fichier}" DESTINATION "${DEST}")
    endif()
endforeach()
file(COPY "${SOURCE}/maquette/polices" DESTINATION "${DEST}")
file(COPY "${SOURCE}/ui/lib" DESTINATION "${DEST}")
file(COPY "${SOURCE}/ui/vue3d.js" "${SOURCE}/ui/manipulateurs.js" DESTINATION "${DEST}")
file(COPY_FILE "${SOURCE}/maquette/pont-reel.js" "${DEST}/pont.js")
# L'image de la machine (dossier privé, absent du dépôt public) : embarquée
# seulement si elle est là. Sans elle, l'interface montre une icône.
if(EXISTS "${SOURCE}/maquette/prive/imprimante.png")
    file(COPY "${SOURCE}/maquette/prive/imprimante.png" DESTINATION "${DEST}/prive")
endif()

# La page : le contenu de la maquette dans un document complet. Le logiciel
# fonctionne sans Internet : le lien vers les polices en ligne est retiré
# (les polices sont embarquées dans « polices/ »), et le titre est le sien.
file(READ "${SOURCE}/maquette/index.html" corps)
string(REGEX REPLACE "<link rel=\"stylesheet\" href=\"https://fonts[^>]*>\n?" "" corps "${corps}")
string(REPLACE "<title>Simulation ArtPrint Slicer</title>" "<title>ArtPrint Slicer</title>" corps "${corps}")
file(WRITE "${DEST}/index.html"
    "<!doctype html>\n<html lang=\"fr\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n</head>\n<body>\n"
    "${corps}\n</body>\n</html>\n")
