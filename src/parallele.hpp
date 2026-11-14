// ArtPrint Slicer - licence AGPL v3
// Répartit un travail sur tous les cœurs du processeur.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace artprint {

// Appelle « travail(i) » pour chaque i de 0 à nombre - 1, sur plusieurs fils
// d'exécution. Chaque appel doit être indépendant des autres (il n'écrit que
// dans sa propre case). Si un appel lève une exception, elle est relancée ici
// une fois les autres terminés.
template <typename Travail>
void en_parallele(std::size_t nombre, const Travail& travail) {
    const std::size_t fils = std::min<std::size_t>(nombre, std::max(1u, std::thread::hardware_concurrency()));
    if (fils <= 1) {
        for (std::size_t i = 0; i < nombre; ++i) {
            travail(i);
        }
        return;
    }
    std::atomic<std::size_t> suivant{0};
    std::exception_ptr erreur;
    std::mutex verrou;
    const auto boucle = [&] {
        for (;;) {
            const std::size_t i = suivant.fetch_add(1);
            if (i >= nombre) {
                return;
            }
            try {
                travail(i);
            } catch (...) {
                const std::lock_guard<std::mutex> garde(verrou);
                if (!erreur) {
                    erreur = std::current_exception();
                }
                suivant.store(nombre);  // les autres s'arrêtent dès que possible
            }
        }
    };
    std::vector<std::thread> equipe;
    equipe.reserve(fils - 1);
    for (std::size_t f = 1; f < fils; ++f) {
        equipe.emplace_back(boucle);
    }
    boucle();
    for (std::thread& fil : equipe) {
        fil.join();
    }
    if (erreur) {
        std::rethrow_exception(erreur);
    }
}

}  // namespace artprint
