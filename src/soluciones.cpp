#include "soluciones.hpp"

#include "texto.hpp"

#include <windows.h>
#include <shellapi.h>

#include <nlohmann/json.hpp>

namespace
{
    // Herramientas que se pueden abrir: una lista cerrada. «clave» es lo que va en «abrir» del JSON.
    struct Destino
    {
        const wchar_t* clave;
        const wchar_t* archivo;
        const wchar_t* parametros;
    };

    const Destino Destinos[] = {
        { L"taskmgr", L"taskmgr.exe", nullptr },
        { L"taskmgr-inicio", L"taskmgr.exe", L"/0 /startup" },   // abre en «Aplicaciones de arranque»
        { L"devmgmt.msc", L"devmgmt.msc", nullptr },
        { L"cleanmgr", L"cleanmgr.exe", nullptr },
        { L"mdsched", L"mdsched.exe", nullptr },
        { L"windowsdefender:", L"windowsdefender:", nullptr },
        // Páginas de Configuración (lista oficial de URI «ms-settings:» de Microsoft)
        { L"ms-settings:batterysaver", L"ms-settings:batterysaver", nullptr },
        { L"ms-settings:bluetooth", L"ms-settings:bluetooth", nullptr },
        { L"ms-settings:disksandvolumes", L"ms-settings:disksandvolumes", nullptr },
        { L"ms-settings:display", L"ms-settings:display", nullptr },
        { L"ms-settings:network-wifi", L"ms-settings:network-wifi", nullptr },
        { L"ms-settings:printers", L"ms-settings:printers", nullptr },
        { L"ms-settings:privacy-microphone", L"ms-settings:privacy-microphone", nullptr },
        { L"ms-settings:privacy-webcam", L"ms-settings:privacy-webcam", nullptr },
        { L"ms-settings:regionlanguage", L"ms-settings:regionlanguage", nullptr },
        { L"ms-settings:sound", L"ms-settings:sound", nullptr },
        { L"ms-settings:storagesense", L"ms-settings:storagesense", nullptr },
        { L"ms-settings:troubleshoot", L"ms-settings:troubleshoot", nullptr },
        { L"ms-settings:windowsupdate", L"ms-settings:windowsupdate", nullptr },
        { L"ms-settings:windowsupdate-optionalupdates", L"ms-settings:windowsupdate-optionalupdates", nullptr },
        { L"ms-settings:windowsupdate-options", L"ms-settings:windowsupdate-options", nullptr },
    };

    const Destino* BuscarDestino(const std::wstring& abrir)
    {
        for (const auto& d : Destinos)
            if (abrir == d.clave) return &d;
        return nullptr;
    }

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
                // Solo enlaces a la ayuda oficial de Microsoft; cualquier otro se descarta y no se muestra
                x.enlace = texto::DesdeUtf8(s.value("enlace", ""));
                if (!soluciones::EnlacePermitido(x.enlace)) x.enlace.clear();
                if (s.contains("palabras"))
                    for (const auto& p : s["palabras"]) x.palabras.push_back(texto::DesdeUtf8(p.get<std::string>()));
                if (s.contains("pasos"))
                    for (const auto& p : s["pasos"]) x.pasos.push_back(texto::DesdeUtf8(p.get<std::string>()));
                // Solo herramientas de la lista cerrada; las demás no tienen botón
                if (s.contains("herramientas"))
                    for (const auto& h : s["herramientas"])
                    {
                        soluciones::Herramienta herramienta{ texto::DesdeUtf8(h.value("texto", "")), texto::DesdeUtf8(h.value("abrir", "")) };
                        if (!herramienta.texto.empty() && soluciones::HerramientaPermitida(herramienta.abrir))
                            x.herramientas.push_back(herramienta);
                    }

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

    bool EnlacePermitido(const std::wstring& url)
    {
        if (url.empty() || url.size() > 400) return false;
        for (wchar_t c : url)
            if (c <= L' ' || c > 0x7E || c == L'"' || c == L'<' || c == L'>' || c == L'\\' || c == L'^' || c == L'`' ||
                c == L'{' || c == L'}' || c == L'|')
                return false;
        return url.rfind(L"https://support.microsoft.com/", 0) == 0 || url.rfind(L"https://learn.microsoft.com/", 0) == 0;
    }

    bool HerramientaPermitida(const std::wstring& abrir)
    {
        return BuscarDestino(abrir) != nullptr;
    }

    bool AbrirHerramienta(const std::wstring& abrir)
    {
        const Destino* d = BuscarDestino(abrir);
        if (!d) return false;
        auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", d->archivo, d->parametros, nullptr, SW_SHOWNORMAL));
        return r > 32;
    }

    bool AbrirEnlace(const std::wstring& url)
    {
        if (!EnlacePermitido(url)) return false;
        auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return r > 32;
    }
}
