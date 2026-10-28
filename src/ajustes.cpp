#include "ajustes.hpp"

#include <windows.h>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
    std::wstring Carpeta()
    {
        PWSTR ruta = nullptr;
        std::wstring r;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &ruta)) && ruta) r = std::wstring(ruta) + L"\\Guara";
        CoTaskMemFree(ruta);
        return r;
    }
}

namespace ajustes
{
    Ventana Cargar()
    {
        Ventana v;
        std::wstring carpeta = Carpeta();
        if (carpeta.empty()) return v;
        std::ifstream f(carpeta + L"\\ajustes.json", std::ios::binary);
        if (!f) return v;
        try
        {
            std::stringstream ss;
            ss << f.rdbuf();
            auto j = nlohmann::json::parse(ss.str());
            const auto& w = j.at("ventana");
            v.x = w.value("x", 0);
            v.y = w.value("y", 0);
            v.ancho = w.value("ancho", 0);
            v.alto = w.value("alto", 0);
            v.maximizada = w.value("maximizada", false);
            v.valida = v.ancho > 200 && v.alto > 150 && v.ancho < 20000 && v.alto < 20000;
        }
        catch (...)
        {
            v = Ventana();   // archivo dañado: se usan los valores de siempre
        }
        return v;
    }

    void Guardar(const Ventana& v)
    {
        std::wstring carpeta = Carpeta();
        if (carpeta.empty()) return;
        CreateDirectoryW(carpeta.c_str(), nullptr);
        nlohmann::json j;
        j["ventana"] = { { "x", v.x }, { "y", v.y }, { "ancho", v.ancho }, { "alto", v.alto }, { "maximizada", v.maximizada } };
        std::ofstream f(carpeta + L"\\ajustes.json", std::ios::binary | std::ios::trunc);
        if (f) f << j.dump(2) << "\n";
    }
}
