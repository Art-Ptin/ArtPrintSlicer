// ArtPrint Slicer - licence AGPL v3
// La fenêtre Windows du slicer. Elle affiche la page de l'interface (dossier
// « ui ») dans le composant WebView2 de Microsoft Edge, et transmet les
// demandes de la page au cœur du slicer.
//
// Échanges avec la page :
//   la page envoie   { "id": 12, "demande": { "commande": "trancher", ... } }
//   la fenêtre répond { "id": 12, "reponse": { "ok": true, ... } }
// Les commandes « dialogue_ouvrir » et « dialogue_enregistrer » sont traitées
// ici (boîtes de dialogue Windows) ; toutes les autres vont à artprint::Session.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "WebView2.h"
#include "api.hpp"

namespace {

using Json = nlohmann::json;

constexpr UINT message_reponse = WM_APP + 1;   // une réponse du slicer est prête
constexpr UINT message_dialogue = WM_APP + 2;  // ouvrir une boîte de dialogue
const wchar_t* const titre_fenetre = L"ArtPrint Slicer";
const wchar_t* const hote_virtuel = L"artprint.app";

// Identifiants des interfaces WebView2 utilisées (copiés de WebView2.h).
const GUID iid_webview_3 = {0xA0D6DF20, 0x3B92, 0x416D, {0xAA, 0x0C, 0x43, 0x7A, 0x9C, 0x72, 0x78, 0x57}};
const GUID iid_environnement_pret = {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
const GUID iid_controleur_pret = {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
const GUID iid_message_recu = {0x57213f19, 0x00e6, 0x49fa, {0x8e, 0x07, 0x89, 0x8e, 0xa0, 0x1e, 0xcb, 0xd2}};

// État de l'application. Il vit jusqu'à la fin du programme.
struct Application {
    HWND fenetre = nullptr;
    ICoreWebView2Controller* controleur = nullptr;
    ICoreWebView2* page = nullptr;
    artprint::Session session;
    std::mutex verrou_session;  // une seule commande du slicer à la fois
};
Application* application = nullptr;

// --- Conversions de texte ------------------------------------------------------

std::wstring en_utf16(const std::string& texte) {
    if (texte.empty()) {
        return {};
    }
    const int taille = MultiByteToWideChar(CP_UTF8, 0, texte.data(), static_cast<int>(texte.size()),
                                           nullptr, 0);
    std::wstring resultat(static_cast<std::size_t>(taille), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, texte.data(), static_cast<int>(texte.size()), resultat.data(),
                        taille);
    return resultat;
}

std::string en_utf8(const std::wstring& texte) {
    if (texte.empty()) {
        return {};
    }
    const int taille = WideCharToMultiByte(CP_UTF8, 0, texte.data(), static_cast<int>(texte.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string resultat(static_cast<std::size_t>(taille), '\0');
    WideCharToMultiByte(CP_UTF8, 0, texte.data(), static_cast<int>(texte.size()), resultat.data(),
                        taille, nullptr, nullptr);
    return resultat;
}

void erreur_fatale(const std::wstring& texte) {
    MessageBoxW(application ? application->fenetre : nullptr, texte.c_str(), titre_fenetre,
                MB_OK | MB_ICONERROR);
}

std::filesystem::path dossier_du_programme() {
    std::wstring chemin(MAX_PATH, L'\0');
    for (;;) {
        const DWORD taille = GetModuleFileNameW(nullptr, chemin.data(), static_cast<DWORD>(chemin.size()));
        if (taille < chemin.size()) {
            chemin.resize(taille);
            break;
        }
        chemin.resize(chemin.size() * 2);
    }
    return std::filesystem::path(chemin).parent_path();
}

// Dossier où WebView2 range ses données (le dossier d'installation n'est pas
// modifiable) : %LOCALAPPDATA%\ArtPrintSlicer\WebView2.
std::filesystem::path dossier_des_donnees() {
    PWSTR local = nullptr;
    std::filesystem::path dossier;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        dossier = std::filesystem::path(local) / L"ArtPrintSlicer" / L"WebView2";
    } else {
        dossier = std::filesystem::temp_directory_path() / L"ArtPrintSlicer" / L"WebView2";
    }
    CoTaskMemFree(local);
    std::error_code ignore;
    std::filesystem::create_directories(dossier, ignore);
    return dossier;
}

// --- Objets de rappel COM ------------------------------------------------------
// WebView2 prévient par des « rappels » : de petits objets dont il appelle la
// fonction Invoke. Ce modèle en fabrique un à partir d'une fonction ordinaire.

template <class Interface, class... Arguments>
class Rappel final : public Interface {
public:
    Rappel(const GUID& iid, std::function<HRESULT(Arguments...)> fonction)
        : iid_(iid), fonction_(std::move(fonction)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** objet) override {
        if (!objet) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) {
            *objet = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *objet = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG reste = --references_;
        if (reste == 0) {
            delete this;
        }
        return reste;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Arguments... arguments) override {
        return fonction_(arguments...);
    }

private:
    ~Rappel() = default;
    const GUID iid_;
    std::function<HRESULT(Arguments...)> fonction_;
    std::atomic<ULONG> references_{1};
};

// --- Réponses à la page --------------------------------------------------------

// Envoie à la page la réponse à la demande numéro « id ». « reponse_json »
// est déjà un texte JSON. Peut être appelée depuis n'importe quel fil.
void repondre(const std::string& id, const std::string& reponse_json) {
    auto* paquet = new std::wstring(
        en_utf16("{\"id\":" + id + ",\"reponse\":" + reponse_json + "}"));
    if (!PostMessageW(application->fenetre, message_reponse, 0, reinterpret_cast<LPARAM>(paquet))) {
        delete paquet;
    }
}

// Filtres d'une boîte de dialogue : « Modèles STL\0*.stl\0Tous\0*.*\0\0 ».
std::wstring filtres_windows(const Json& demande) {
    std::wstring filtres;
    if (demande.contains("filtres")) {
        for (const Json& filtre : demande["filtres"]) {
            const std::wstring nom = en_utf16(filtre.at(0).get<std::string>());
            const std::wstring motif = en_utf16(filtre.at(1).get<std::string>());
            filtres += nom + L" (" + motif + L")";
            filtres.push_back(L'\0');
            filtres += motif;
            filtres.push_back(L'\0');
        }
    }
    filtres.push_back(L'\0');
    return filtres;
}

// Ouvre la boîte « Ouvrir » ou « Enregistrer » de Windows et répond à la page
// avec le chemin choisi (ou « null » si l'utilisateur annule).
void traiter_dialogue(const std::string& id, const Json& demande) {
    const bool enregistrer = demande.at("commande") == "dialogue_enregistrer";
    const std::wstring filtres = filtres_windows(demande);
    const std::wstring titre = en_utf16(demande.value("titre", std::string()));
    std::wstring fichier = en_utf16(demande.value("nom_propose", std::string()));
    fichier.resize(32768, L'\0');

    // Extension ajoutée d'office si l'utilisateur n'en tape pas.
    std::wstring extension;
    if (enregistrer && demande.contains("filtres") && !demande["filtres"].empty()) {
        const std::wstring motif = en_utf16(demande["filtres"][0].at(1).get<std::string>());
        const auto point = motif.rfind(L'.');
        if (point != std::wstring::npos && motif.find(L'*', point) == std::wstring::npos) {
            extension = motif.substr(point + 1);
        }
    }

    OPENFILENAMEW options{};
    options.lStructSize = sizeof options;
    options.hwndOwner = application->fenetre;
    options.lpstrFilter = filtres.c_str();
    options.lpstrFile = fichier.data();
    options.nMaxFile = static_cast<DWORD>(fichier.size());
    options.lpstrTitle = titre.empty() ? nullptr : titre.c_str();
    options.lpstrDefExt = extension.empty() ? nullptr : extension.c_str();
    options.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
                    (enregistrer ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL choisi = enregistrer ? GetSaveFileNameW(&options) : GetOpenFileNameW(&options);
    Json reponse{{"ok", true}, {"chemin", nullptr}};
    if (choisi) {
        reponse["chemin"] = en_utf8(std::wstring(fichier.c_str()));
    }
    repondre(id, reponse.dump());
}

// Une demande arrive de la page.
void recevoir_demande(const std::wstring& message) {
    std::string id = "0";
    try {
        const Json contenu = Json::parse(en_utf8(message));
        id = contenu.at("id").dump();
        const Json& demande = contenu.at("demande");
        const std::string commande = demande.at("commande").get<std::string>();

        if (commande == "dialogue_ouvrir" || commande == "dialogue_enregistrer") {
            // La boîte de dialogue est ouverte après le retour de ce rappel.
            auto* paquet = new std::pair<std::string, Json>(id, demande);
            if (!PostMessageW(application->fenetre, message_dialogue, 0,
                              reinterpret_cast<LPARAM>(paquet))) {
                delete paquet;
            }
            return;
        }
        // Le calcul se fait dans un autre fil : la fenêtre reste vivante
        // pendant un long tranchage.
        std::thread([id, texte = demande.dump()] {
            std::string reponse;
            {
                reponse = application->session.executer(texte);
            }
            repondre(id, reponse);
        }).detach();
    } catch (const std::exception& erreur) {
        repondre(id, Json{{"ok", false}, {"erreur", erreur.what()}}.dump());
    }
}

// --- Démarrage de WebView2 -----------------------------------------------------

void ajuster_la_page() {
    if (application->controleur) {
        RECT zone;
        GetClientRect(application->fenetre, &zone);
        application->controleur->put_Bounds(zone);
    }
}

HRESULT controleur_pret(HRESULT resultat, ICoreWebView2Controller* controleur) {
    if (FAILED(resultat) || !controleur) {
        erreur_fatale(std::format(
            L"Impossible de créer la zone d'affichage (erreur 0x{:08X}).",
            static_cast<unsigned>(resultat)));
        PostQuitMessage(1);
        return S_OK;
    }
    application->controleur = controleur;
    controleur->AddRef();
    controleur->get_CoreWebView2(&application->page);
    ajuster_la_page();
    controleur->put_IsVisible(TRUE);

    EventRegistrationToken jeton{};
    auto* rappel = new Rappel<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*,
                              ICoreWebView2WebMessageReceivedEventArgs*>(
        iid_message_recu, [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* arguments) {
            LPWSTR json = nullptr;
            if (SUCCEEDED(arguments->get_WebMessageAsJson(&json)) && json) {
                recevoir_demande(json);
            }
            CoTaskMemFree(json);
            return S_OK;
        });
    application->page->add_WebMessageReceived(rappel, &jeton);
    rappel->Release();

    // Le dossier « ui » est servi à la page sous une adresse interne : c'est
    // ce qui autorise le chargement des modules JavaScript.
    const std::filesystem::path dossier_ui = dossier_du_programme() / L"ui";
    ICoreWebView2_3* page_3 = nullptr;
    if (FAILED(application->page->QueryInterface(iid_webview_3, reinterpret_cast<void**>(&page_3))) ||
        !page_3) {
        erreur_fatale(L"Le composant Microsoft Edge WebView2 installé est trop ancien.\n"
                      L"Mets Windows à jour, puis relance ArtPrint Slicer.");
        PostQuitMessage(1);
        return S_OK;
    }
    page_3->SetVirtualHostNameToFolderMapping(hote_virtuel, dossier_ui.c_str(),
                                              COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    page_3->Release();
    application->page->Navigate((std::wstring(L"https://") + hote_virtuel + L"/index.html").c_str());
    return S_OK;
}

HRESULT environnement_pret(HRESULT resultat, ICoreWebView2Environment* environnement) {
    if (FAILED(resultat) || !environnement) {
        erreur_fatale(std::format(
            L"Le composant Microsoft Edge WebView2 n'a pas pu démarrer (erreur 0x{:08X}).\n\n"
            L"Il est fourni avec Windows 11 et avec Microsoft Edge. S'il manque, installe "
            L"« WebView2 Runtime » depuis le site de Microsoft, puis relance ArtPrint Slicer.",
            static_cast<unsigned>(resultat)));
        PostQuitMessage(1);
        return S_OK;
    }
    auto* rappel = new Rappel<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                              ICoreWebView2Controller*>(iid_controleur_pret, controleur_pret);
    environnement->CreateCoreWebView2Controller(application->fenetre, rappel);
    rappel->Release();
    return S_OK;
}

bool demarrer_webview() {
    const std::filesystem::path dossier = dossier_du_programme();
    if (!std::filesystem::exists(dossier / L"ui" / L"index.html")) {
        erreur_fatale(L"Le dossier « ui » de l'interface est introuvable à côté du programme.\n"
                      L"Réinstalle ArtPrint Slicer.");
        return false;
    }
    const HMODULE chargeur = LoadLibraryW((dossier / L"WebView2Loader.dll").c_str());
    if (!chargeur) {
        erreur_fatale(L"Le fichier WebView2Loader.dll est introuvable à côté du programme.\n"
                      L"Réinstalle ArtPrint Slicer.");
        return false;
    }
    using Creation = HRESULT(STDAPICALLTYPE*)(
        PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    const auto creer = reinterpret_cast<Creation>(reinterpret_cast<void (*)()>(
        GetProcAddress(chargeur, "CreateCoreWebView2EnvironmentWithOptions")));
    if (!creer) {
        erreur_fatale(L"Le fichier WebView2Loader.dll est abîmé. Réinstalle ArtPrint Slicer.");
        return false;
    }
    auto* rappel = new Rappel<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                              ICoreWebView2Environment*>(iid_environnement_pret, environnement_pret);
    const HRESULT resultat = creer(nullptr, dossier_des_donnees().c_str(), nullptr, rappel);
    rappel->Release();
    if (FAILED(resultat)) {
        environnement_pret(resultat, nullptr);
        return false;
    }
    return true;
}

// --- Fenêtre -------------------------------------------------------------------

LRESULT CALLBACK procedure_fenetre(HWND fenetre, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_SIZE:
            ajuster_la_page();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limites = reinterpret_cast<MINMAXINFO*>(l);
            limites->ptMinTrackSize = {980, 620};
            return 0;
        }
        case message_reponse: {
            const std::unique_ptr<std::wstring> paquet(reinterpret_cast<std::wstring*>(l));
            if (application->page) {
                application->page->PostWebMessageAsJson(paquet->c_str());
            }
            return 0;
        }
        case message_dialogue: {
            const std::unique_ptr<std::pair<std::string, Json>> paquet(
                reinterpret_cast<std::pair<std::string, Json>*>(l));
            try {
                traiter_dialogue(paquet->first, paquet->second);
            } catch (const std::exception& erreur) {
                repondre(paquet->first, Json{{"ok", false}, {"erreur", erreur.what()}}.dump());
            }
            return 0;
        }
        case WM_DESTROY:
            if (application->controleur) {
                application->controleur->Close();
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(fenetre, message, w, l);
    }
}

// Demande à Windows un affichage net sur les écrans à forte densité.
void activer_haute_resolution() {
    using Fonction = BOOL(WINAPI*)(HANDLE);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto definir = reinterpret_cast<Fonction>(reinterpret_cast<void (*)()>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    // -4 : DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
    if (!definir || !definir(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) {
        SetProcessDPIAware();
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int affichage) {
    activer_haute_resolution();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return 1;
    }
    // Volontairement jamais détruit : des calculs peuvent encore tourner
    // quand la fenêtre se ferme.
    application = new Application();

    WNDCLASSEXW classe{};
    classe.cbSize = sizeof classe;
    classe.lpfnWndProc = procedure_fenetre;
    classe.hInstance = instance;
    classe.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // L'icône du logiciel (ressource numéro 1, voir ressources.rc).
    classe.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    classe.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    classe.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    classe.lpszClassName = L"ArtPrintSlicerFenetre";
    if (!RegisterClassExW(&classe)) {
        return 1;
    }
    application->fenetre =
        CreateWindowExW(0, classe.lpszClassName, titre_fenetre, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                        CW_USEDEFAULT, 1440, 900, nullptr, nullptr, instance, nullptr);
    if (!application->fenetre) {
        return 1;
    }
    ShowWindow(application->fenetre, affichage);
    UpdateWindow(application->fenetre);

    if (!demarrer_webview()) {
        return 1;
    }

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // Sortie immédiate : les fils de calcul éventuels sont abandonnés.
    ExitProcess(static_cast<UINT>(message.wParam));
}
