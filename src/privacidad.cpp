#include "privacidad.hpp"

#include <windows.h>
#include <shlobj.h>
#define SECURITY_WIN32
#include <security.h>

#include <regex>
#include <vector>

namespace
{
    struct Datos
    {
        std::wstring usuario;
        std::wstring perfil, perfilCorto;
        std::vector<std::wstring> equipos;   // nombre NetBIOS y nombre DNS
        std::vector<std::wstring> nombres;   // partes del nombre completo de la cuenta, si Windows lo da
    };

    void AgregarUnico(std::vector<std::wstring>& v, const std::wstring& s)
    {
        if (s.size() < 2) return;
        for (const auto& x : v)
            if (CompareStringOrdinal(x.c_str(), -1, s.c_str(), -1, TRUE) == CSTR_EQUAL) return;
        v.push_back(s);
    }

    // Se crea una sola vez y no se destruye nunca: el hilo de la revisión puede usarlo hasta el último momento
    const Datos& Leer()
    {
        static const Datos* datos = [] {
            auto* d = new Datos();
            wchar_t b[1024];
            DWORD n = 1024;
            if (GetUserNameW(b, &n)) d->usuario = b;

            const COMPUTER_NAME_FORMAT tipos[] = { ComputerNameNetBIOS, ComputerNameDnsHostname,
                                                   ComputerNamePhysicalNetBIOS, ComputerNamePhysicalDnsHostname };
            for (auto tipo : tipos)
            {
                n = 1024;
                if (GetComputerNameExW(tipo, b, &n) && n > 0) AgregarUnico(d->equipos, std::wstring(b, n));
            }

            PWSTR ruta = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &ruta)) && ruta) d->perfil = ruta;
            CoTaskMemFree(ruta);
            if (!d->perfil.empty())
            {
                n = GetShortPathNameW(d->perfil.c_str(), b, 1024);
                if (n > 0 && n < 1024) d->perfilCorto = b;
            }

            // Nombre completo (por ejemplo, el de una cuenta de Microsoft o de empresa), por si aparece en algún dato
            n = 1024;
            if (GetUserNameExW(NameDisplay, b, &n) && n > 0)
            {
                std::wstring completo(b, n), parte;
                for (wchar_t c : completo + L" ")
                {
                    if (c == L' ' || c == L',' || c == L'.')
                    {
                        if (parte.size() >= 3) AgregarUnico(d->nombres, parte);
                        parte.clear();
                    }
                    else
                        parte += c;
                }
            }
            return d;
        }();
        return *datos;
    }

    bool EsLetra(wchar_t c)
    {
        return IsCharAlphaNumericW(c) != FALSE;
    }

    // Reemplaza sin importar mayúsculas. Con «palabra», solo si no está pegado a otras letras o números.
    void Reemplazar(std::wstring& s, const std::wstring& buscar, const std::wstring& poner, bool palabra)
    {
        if (buscar.size() < 2) return;
        size_t i = 0;
        while (i + buscar.size() <= s.size())
        {
            if (CompareStringOrdinal(s.data() + i, static_cast<int>(buscar.size()), buscar.data(),
                                     static_cast<int>(buscar.size()), TRUE) == CSTR_EQUAL)
            {
                size_t fin = i + buscar.size();
                bool antes = !palabra || i == 0 || !EsLetra(s[i - 1]);
                bool despues = !palabra || fin >= s.size() || !EsLetra(s[fin]);
                if (antes && despues)
                {
                    s.replace(i, buscar.size(), poner);
                    i += poner.size();
                    continue;
                }
            }
            i++;
        }
    }

    // Correos, direcciones MAC y direcciones IPv4 sueltas en el texto. Guara no las pide a Windows para el
    // informe; esto es una red de seguridad por si alguna viene dentro de un nombre.
    std::wstring QuitarIdentificadores(const std::wstring& s)
    {
        static const std::wregex* correo = new std::wregex(
            LR"([A-Za-z0-9._%+\-]+@[A-Za-z0-9\-]+(\.[A-Za-z0-9\-]+)*\.[A-Za-z]{2,})");
        static const std::wregex* mac = new std::wregex(
            LR"(\b[0-9A-Fa-f]{2}([:\-])[0-9A-Fa-f]{2}(\1[0-9A-Fa-f]{2}){4}\b)");
        static const std::wregex* ip = new std::wregex(
            LR"((^|[^0-9.])(((25[0-5]|2[0-4][0-9]|1?[0-9]?[0-9])\.){3}(25[0-5]|2[0-4][0-9]|1?[0-9]?[0-9]))(?![0-9]|\.[0-9]))");
        std::wstring r = std::regex_replace(s, *correo, L"[correo]");
        r = std::regex_replace(r, *mac, L"[MAC]");
        r = std::regex_replace(r, *ip, L"$1[IP]");
        return r;
    }

    bool Contiene(const std::wstring& s, const std::wstring& buscar)
    {
        if (buscar.size() < 2) return false;
        for (size_t i = 0; i + buscar.size() <= s.size(); i++)
        {
            if (CompareStringOrdinal(s.data() + i, static_cast<int>(buscar.size()), buscar.data(),
                                     static_cast<int>(buscar.size()), TRUE) == CSTR_EQUAL)
            {
                size_t fin = i + buscar.size();
                if ((i == 0 || !EsLetra(s[i - 1])) && (fin >= s.size() || !EsLetra(s[fin]))) return true;
            }
        }
        return false;
    }
}

namespace privacidad
{
    void Preparar()
    {
        Leer();
    }

    std::wstring LimpiarRuta(const std::wstring& texto)
    {
        const Datos& d = Leer();
        std::wstring s = texto;
        if (!d.perfil.empty()) Reemplazar(s, d.perfil, L"%USERPROFILE%", true);
        if (!d.perfilCorto.empty()) Reemplazar(s, d.perfilCorto, L"%USERPROFILE%", true);

        // Carpetas de otras cuentas de la PC: C:\Users\<nombre> -> C:\Users\[usuario]
        static const std::wregex* otra = new std::wregex(
            LR"(([A-Za-z]:\\(?:Users|Usuarios|Documents and Settings)\\)([^\\/:*?"<>|\r\n]+))", std::regex::icase);
        std::wstring r;
        auto inicio = s.cbegin();
        for (std::wsregex_iterator it(s.cbegin(), s.cend(), *otra), fin; it != fin; ++it)
        {
            const auto& m = *it;
            std::wstring carpeta = m[2].str();
            r.append(inicio, m[0].first);
            const wchar_t* comunes[] = { L"Public", L"Default", L"Default User", L"All Users", L"Acceso público" };
            bool comun = false;
            for (auto c : comunes)
                if (CompareStringOrdinal(carpeta.c_str(), -1, c, -1, TRUE) == CSTR_EQUAL) comun = true;
            r += comun ? m[0].str() : m[1].str() + L"[usuario]";
            inicio = m[0].second;
        }
        r.append(inicio, s.cend());
        return r;
    }

    std::wstring LimpiarInforme(const std::wstring& texto)
    {
        const Datos& d = Leer();
        std::wstring s = LimpiarRuta(texto);
        // Primero el equipo: un nombre como «MARIA-PC» contiene el del usuario
        for (const auto& e : d.equipos) Reemplazar(s, e, L"[equipo]", true);
        if (!d.usuario.empty()) Reemplazar(s, d.usuario, L"[usuario]", true);
        for (const auto& n : d.nombres) Reemplazar(s, n, L"[usuario]", true);
        return QuitarIdentificadores(s);
    }

    std::wstring LimpiarFinal(const std::wstring& texto)
    {
        const Datos& d = Leer();
        std::wstring s = QuitarIdentificadores(texto);
        for (const auto& e : d.equipos) Reemplazar(s, e, L"[equipo]", true);
        if (!d.usuario.empty()) Reemplazar(s, d.usuario, L"[usuario]", true);
        return s;
    }

    bool TieneDatosPersonales(const std::wstring& texto)
    {
        const Datos& d = Leer();
        if (!d.usuario.empty() && Contiene(texto, d.usuario)) return true;
        for (const auto& e : d.equipos)
            if (Contiene(texto, e)) return true;
        return false;
    }
}
