#include "soluciones.hpp"

#include "texto.hpp"

#include <windows.h>
#include <shellapi.h>

#include <nlohmann/json.hpp>

namespace
{
    std::vector<soluciones::Solucion> Cargar()
    {
        std::vector<soluciones::Solucion> lista;
        HRSRC recurso = FindResourceW(nullptr, L"SOLUCIONES", MAKEINTRESOURCEW(10));   // 10 = RT_RCDATA
        HGLOBAL cargado = recurso ? LoadResource(nullptr, recurso) : nullptr;
        const char* datos = cargado ? static_cast<const char*>(LockResource(cargado)) : nullptr;
        DWORD tamano = recurso ? SizeofResource(nullptr, recurso) : 0;
        if (!datos || tamano == 0) return lista;

        try
        {
            auto j = nlohmann::json::parse(datos, datos + tamano);
            for (const auto& s : j.at("soluciones"))
            {
                soluciones::Solucion x;
                x.id = s.value("id", "");
                x.titulo = texto::DesdeUtf8(s.value("titulo", ""));
                x.enlace = texto::DesdeUtf8(s.value("enlace", ""));
                if (s.contains("palabras"))
                    for (const auto& p : s["palabras"]) x.palabras.push_back(texto::DesdeUtf8(p.get<std::string>()));
                if (s.contains("pasos"))
                    for (const auto& p : s["pasos"]) x.pasos.push_back(texto::DesdeUtf8(p.get<std::string>()));
                if (s.contains("herramientas"))
                    for (const auto& h : s["herramientas"])
                        x.herramientas.push_back({ texto::DesdeUtf8(h.value("texto", "")), texto::DesdeUtf8(h.value("abrir", "")) });

                std::wstring todo = x.titulo;
                for (const auto& p : x.palabras) todo += L" " + p;
                for (const auto& p : x.pasos) todo += L" " + p;
                x.busqueda = texto::Normalizar(todo);
                if (!x.id.empty() && !x.titulo.empty()) lista.push_back(std::move(x));
            }
        }
        catch (...)
        {
            // Si el archivo de soluciones está mal escrito, el buscador queda vacío (no se cierra el programa)
        }
        return lista;
    }

    // Palabras que no ayudan a buscar
    bool Vacia(const std::wstring& p)
    {
        static const wchar_t* comunes[] = { L"el", L"la", L"los", L"las", L"un", L"una", L"de", L"del", L"en", L"y", L"o", L"a",
                                            L"mi", L"me", L"se", L"que", L"no", L"con", L"por", L"para", L"es", L"esta", L"muy",
                                            L"lo", L"le", L"al", L"su", L"tu", L"pc", L"computadora", L"compu", L"laptop", L"windows" };
        for (auto c : comunes)
            if (p == c) return true;
        return false;
    }

    // true si «p» aparece al principio de alguna palabra de «texto» («lent» está en «lenta», no en «calienta»)
    bool AlInicioDePalabra(const std::wstring& texto, const std::wstring& p)
    {
        for (size_t i = texto.find(p); i != std::wstring::npos; i = texto.find(p, i + 1))
            if (i == 0 || !IsCharAlphaNumericW(texto[i - 1])) return true;
        return false;
    }
}

namespace soluciones
{
    const std::vector<Solucion>& Todas()
    {
        static const std::vector<Solucion> todas = Cargar();
        return todas;
    }

    const Solucion* Buscar(const std::string& id)
    {
        for (const auto& s : Todas())
            if (s.id == id) return &s;
        return nullptr;
    }

    std::vector<const Solucion*> Filtrar(const std::wstring& consulta)
    {
        std::vector<std::wstring> palabras;
        std::wstring actual;
        for (wchar_t c : texto::Normalizar(consulta) + L" ")
        {
            if (c == L' ' || c == L',' || c == L'.' || c == L';' || c == L'?' || c == L'¿' || c == L'!' || c == L'¡')
            {
                if (!actual.empty() && !Vacia(actual)) palabras.push_back(actual);
                actual.clear();
            }
            else
                actual += c;
        }

        std::vector<const Solucion*> r;
        for (const auto& s : Todas())
        {
            bool todas = true;
            for (const auto& p : palabras)
            {
                bool esta = AlInicioDePalabra(s.busqueda, p);
                // «lento» también encuentra «lenta», «imprime» encuentra «imprimir»
                if (!esta && p.size() >= 5) esta = AlInicioDePalabra(s.busqueda, p.substr(0, p.size() - 1));
                if (!esta) todas = false;
            }
            if (todas) r.push_back(&s);
        }
        return r;
    }

    bool AbrirHerramienta(const std::wstring& abrir)
    {
        std::wstring archivo;
        const wchar_t* parametros = nullptr;
        if (abrir == L"taskmgr")
            archivo = L"taskmgr.exe";
        else if (abrir == L"taskmgr-inicio")
        {
            archivo = L"taskmgr.exe";
            parametros = L"/0 /startup";   // abre en «Aplicaciones de arranque»
        }
        else if (abrir == L"devmgmt.msc")
            archivo = L"devmgmt.msc";
        else if (abrir == L"cleanmgr")
            archivo = L"cleanmgr.exe";
        else if (abrir == L"mdsched")
            archivo = L"mdsched.exe";
        else if (abrir == L"windowsdefender:")
            archivo = L"windowsdefender:";
        else if (abrir.rfind(L"ms-settings:", 0) == 0 && abrir.size() < 80)
        {
            for (wchar_t c : abrir)
                if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L':')) return false;
            archivo = abrir;
        }
        else
            return false;
        auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", archivo.c_str(), parametros, nullptr, SW_SHOWNORMAL));
        return r > 32;
    }
}
