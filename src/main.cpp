#include <httplib.h>

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  -p, --port <n>     port d'ecoute (defaut: un port libre au hasard)\n"
              << "  -H, --host <addr>  adresse d'ecoute (defaut 0.0.0.0 = tout le reseau local)\n"
              << "  -d, --dir <path>   dossier a partager (defaut .)\n"
              << "  -v, --version      affiche la version\n"
              << "  -h, --help         affiche cette aide\n";
}

// Adresses IPv4 des interfaces actives (hors loopback)
static std::vector<std::string> local_ips() {
    std::vector<std::string> ips;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<char> buf(size);
    auto flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, list, &size);
    }
    if (rc != NO_ERROR) return ips;
    for (auto* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            char s[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &sin->sin_addr, s, sizeof s)) ips.emplace_back(s);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return ips;
    for (auto* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        char s[INET_ADDRSTRLEN];
        auto* sin = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, s, sizeof s)) ips.emplace_back(s);
    }
    freeifaddrs(list);
#endif
    return ips;
}

static std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

static std::string human_size(uintmax_t n) {
    const char* units[] = {"o", "Ko", "Mo", "Go", "To"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 1024 && u < 4) { v /= 1024; ++u; }
    char buf[32];
    snprintf(buf, sizeof buf, u ? "%.1f %s" : "%.0f %s", v, units[u]);
    return buf;
}

static std::string mime_type(const fs::path& p) {
    std::string ext = p.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    static const std::pair<const char*, const char*> types[] = {
        {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"},
        {".txt", "text/plain; charset=utf-8"}, {".md", "text/plain; charset=utf-8"},
        {".csv", "text/plain; charset=utf-8"}, {".log", "text/plain; charset=utf-8"},
        {".css", "text/css"}, {".js", "text/javascript"}, {".json", "application/json"},
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif", "image/gif"}, {".webp", "image/webp"}, {".svg", "image/svg+xml"},
        {".heic", "image/heic"}, {".ico", "image/x-icon"}, {".pdf", "application/pdf"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mov", "video/quicktime"},
        {".mp3", "audio/mpeg"}, {".wav", "audio/wav"}, {".ogg", "audio/ogg"},
        {".m4a", "audio/mp4"}, {".zip", "application/zip"},
    };
    for (auto& [e, t] : types)
        if (ext == e) return t;
    return "application/octet-stream";
}

// Vrai si p est root ou se trouve a l'interieur (chemins deja canoniques)
static bool is_inside(const fs::path& root, const fs::path& p) {
    auto q = p.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++q) {
        if (r->empty()) continue;
        if (q == p.end() || *q != *r) return false;
    }
    return true;
}

// Traduit un chemin d'URL en chemin disque, en refusant tout ce qui sort de root
// (../, chemins absolus, liens symboliques qui pointent ailleurs...)
static std::optional<fs::path> resolve(const fs::path& root, const std::string& url_path) {
    std::error_code ec;
    fs::path full = fs::weakly_canonical(root / fs::u8path(url_path).relative_path(), ec);
    if (ec || !is_inside(root, full)) return std::nullopt;
    return full;
}

static fs::path unique_path(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return p;
    auto stem = p.stem().u8string(), ext = p.extension().u8string();
    for (int i = 1;; ++i) {
        auto candidate = p.parent_path() / fs::u8path(stem + " (" + std::to_string(i) + ")" + ext);
        if (!fs::exists(candidate, ec)) return candidate;
    }
}

static std::string render_dir(const fs::path& dir, const std::string& url_path) {
    struct Entry { std::string name; bool is_dir; uintmax_t size; };
    std::vector<Entry> entries;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        bool d = it->is_directory(e2);
        uintmax_t size = d ? 0 : it->file_size(e2);
        entries.push_back({it->path().filename().u8string(), d, e2 ? 0 : size});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.is_dir != b.is_dir ? a.is_dir : a.name < b.name;
    });

    std::string base = url_path;
    if (base.empty() || base.back() != '/') base += '/';

    std::string h =
        "<!doctype html><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>" + html_escape(base) + "</title>"
        "<style>body{font:16px/1.7 system-ui,sans-serif;max-width:40em;margin:1em auto;padding:0 1em;"
        "color-scheme:light dark}ul{padding:0}li{list-style:none;display:flex;gap:1em}"
        "a{flex:1;text-decoration:none;overflow-wrap:anywhere}small{opacity:.6}</style>"
        "<h3>" + html_escape(base) + "</h3>"
        "<form method=post enctype=multipart/form-data>"
        "<input type=file name=f multiple onchange=this.form.submit()></form><ul>";
    if (base != "/") h += "<li><a href='../'>..</a>";
    for (auto& e : entries) {
        std::string href = url_encode(base + e.name) + (e.is_dir ? "/" : "");
        h += "<li><a href=\"" + html_escape(href) + "\">" + html_escape(e.name) + (e.is_dir ? "/" : "") + "</a>";
        if (!e.is_dir) h += "<small>" + human_size(e.size) + "</small>";
    }
    h += "</ul><script>ondragover=e=>e.preventDefault();ondrop=e=>{e.preventDefault();"
         "let i=document.querySelector('input');i.files=e.dataTransfer.files;i.form.submit()}</script>";
    return h;
}

int main(int argc, char** argv) {
    int port = 0;
    std::string host = "0.0.0.0";
    std::string dir = ".";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Valeur manquante pour " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-p" || a == "--port") port = std::atoi(next().c_str());
        else if (a == "-H" || a == "--host") host = next();
        else if (a == "-d" || a == "--dir") dir = next();
        else if (a == "-v" || a == "--version") { std::cout << APP_VERSION << "\n"; return 0; }
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { std::cerr << "Option inconnue: " << a << "\n"; usage(argv[0]); return 2; }
    }

    std::error_code ec;
    const fs::path root = fs::canonical(fs::u8path(dir), ec);
    if (ec || !fs::is_directory(root)) {
        std::cerr << "Dossier introuvable: " << dir << "\n";
        return 1;
    }

    httplib::Server svr;

    svr.Get(".*", [&](const httplib::Request& req, httplib::Response& res) {
        auto path = resolve(root, req.path);
        std::error_code ec;
        if (!path || !fs::exists(*path, ec)) {
            res.status = 404;
            res.set_content("Introuvable", "text/plain; charset=utf-8");
        } else if (fs::is_directory(*path, ec)) {
            res.set_content(render_dir(*path, req.path), "text/html; charset=utf-8");
        } else {
            auto size = fs::file_size(*path, ec);
            auto file = std::make_shared<std::ifstream>(*path, std::ios::binary);
            if (ec || !*file) {
                res.status = 403;
                return;
            }
            res.set_content_provider(size, mime_type(*path),
                [file](size_t offset, size_t length, httplib::DataSink& sink) {
                    char buf[64 * 1024];
                    file->seekg(static_cast<std::streamoff>(offset));
                    file->read(buf, static_cast<std::streamsize>(std::min(length, sizeof buf)));
                    auto n = file->gcount();
                    if (n <= 0) return false;
                    sink.write(buf, static_cast<size_t>(n));
                    return true;
                });
        }
    });

    // Depot de fichiers : chaque fichier est ecrit en streaming dans un .lanman-part,
    // puis renomme une fois complet (sans ecraser un fichier existant)
    svr.Post(".*", [&](const httplib::Request& req, httplib::Response& res,
                       const httplib::ContentReader& reader) {
        auto target = resolve(root, req.path);
        std::error_code ec;
        if (!target || !fs::is_directory(*target, ec) || !req.is_multipart_form_data()) {
            res.status = 400;
            return;
        }

        std::ofstream out;
        fs::path dest, tmp;
        bool ok = false;
        auto finish = [&] {
            if (!out.is_open()) return;
            out.close();
            std::error_code e;
            if (ok && out) fs::rename(tmp, unique_path(dest), e);
            else fs::remove(tmp, e);
        };

        bool complete = reader(
            [&](const httplib::MultipartFormData& part) {
                finish();
                fs::path name = fs::u8path(part.filename).filename();
                if (name.empty() || name == "." || name == "..") return true;
                dest = *target / name;
                tmp = dest;
                tmp += ".lanman-part";
                out.open(tmp, std::ios::binary);
                ok = out.good();
                return true;
            },
            [&](const char* data, size_t len) {
                if (out.is_open() && ok) {
                    out.write(data, static_cast<std::streamsize>(len));
                    ok = out.good();
                }
                return true;
            });
        if (!complete) ok = false;
        finish();

        res.status = 303;
        res.set_header("Location", url_encode(req.path));
    });

    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        std::cout << req.remote_addr << "  " << req.method << " " << req.path << " -> " << res.status << std::endl;
    });

    if (port == 0) {
        port = svr.bind_to_any_port(host);
    } else if (!svr.bind_to_port(host, port)) {
        port = -1;
    }
    if (port <= 0) {
        std::cerr << "Impossible d'ecouter sur " << host << "\n";
        return 1;
    }

    std::cout << "lanman partage " << root.u8string() << "\n";
    if (host == "0.0.0.0") {
        for (auto& ip : local_ips()) std::cout << "  -> http://" << ip << ":" << port << "\n";
        std::cout << "  -> http://localhost:" << port << "\n";
    } else {
        std::cout << "  -> http://" << host << ":" << port << "\n";
    }
    std::cout << "Reseau prive uniquement (pas de chiffrement ni de mot de passe). Ctrl+C pour arreter.\n"
              << std::flush;

    svr.listen_after_bind();
}
