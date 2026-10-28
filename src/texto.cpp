#include "texto.hpp"

#include <shlwapi.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace texto
{
    std::wstring Formato(const wchar_t* formato, ...)
    {
        wchar_t buf[4096];
        va_list args;
        va_start(args, formato);
        int n = _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, formato, args);
        va_end(args);
        if (n < 0) n = static_cast<int>(wcslen(buf));
        return std::wstring(buf, static_cast<size_t>(n));
    }

    std::wstring Decimal(double valor, int decimales)
    {
        std::wstring s = Formato(L"%.*f", decimales, valor);
        wchar_t sep[8] = {};
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, sep, 8) > 0 && sep[0] && sep[0] != L'.')
            for (auto& c : s)
                if (c == L'.') c = sep[0];
        return s;
    }

    std::wstring Bytes(unsigned long long bytes)
    {
        // Igual que el Explorador: tres cifras, sin redondear hacia arriba y con el separador decimal de la persona
        wchar_t b[64] = {};
        if (SUCCEEDED(StrFormatByteSizeEx(bytes, SFBS_FLAGS_TRUNCATE_UNDISPLAYED_DECIMAL_DIGITS, b, _countof(b)))) return b;
        const double gb = 1024.0 * 1024.0 * 1024.0;
        return Decimal(static_cast<double>(bytes) / gb, 1) + L" GB";
    }

    std::wstring Fecha(const SYSTEMTIME& st)
    {
        return Formato(L"%02u/%02u/%04u", st.wDay, st.wMonth, st.wYear);
    }

    std::wstring FechaHora(const SYSTEMTIME& st)
    {
        return Fecha(st) + Formato(L" %02u:%02u", st.wHour, st.wMinute);
    }

    std::wstring Plural(long long n, const wchar_t* uno, const wchar_t* varios)
    {
        return std::to_wstring(n) + L" " + (n == 1 ? uno : varios);
    }

    std::wstring Duracion(unsigned long long segundos)
    {
        unsigned long long minutos = segundos / 60, horas = minutos / 60, dias = horas / 24;
        if (dias >= 1)
        {
            unsigned long long h = horas % 24;
            return Plural(static_cast<long long>(dias), L"día", L"días")
                + (h ? L" y " + Plural(static_cast<long long>(h), L"hora", L"horas") : std::wstring());
        }
        if (horas >= 1)
        {
            unsigned long long m = minutos % 60;
            return Plural(static_cast<long long>(horas), L"hora", L"horas")
                + (m ? L" y " + Plural(static_cast<long long>(m), L"minuto", L"minutos") : std::wstring());
        }
        return Plural(static_cast<long long>(minutos < 1 ? 1 : minutos), L"minuto", L"minutos");
    }

    std::wstring Hace(long long dias)
    {
        if (dias <= 0) return L"hoy";
        if (dias == 1) return L"ayer";
        return L"hace " + Plural(dias, L"día", L"días");
    }

    std::wstring Recortar(const std::wstring& s)
    {
        std::wstring r;
        r.reserve(s.size());
        bool espacio = false;
        for (wchar_t c : s)
        {
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x00A0)
            {
                espacio = !r.empty();
                continue;
            }
            if (espacio) r += L' ';
            espacio = false;
            r += c;
        }
        return r;
    }

    std::wstring Normalizar(const std::wstring& s)
    {
        std::wstring r;
        r.reserve(s.size());
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'á': case L'à': case L'ä': case L'â': case L'Á': case L'À': case L'Ä': case L'Â': c = L'a'; break;
            case L'é': case L'è': case L'ë': case L'ê': case L'É': case L'È': case L'Ë': case L'Ê': c = L'e'; break;
            case L'í': case L'ì': case L'ï': case L'î': case L'Í': case L'Ì': case L'Ï': case L'Î': c = L'i'; break;
            case L'ó': case L'ò': case L'ö': case L'ô': case L'Ó': case L'Ò': case L'Ö': case L'Ô': c = L'o'; break;
            case L'ú': case L'ù': case L'ü': case L'û': case L'Ú': case L'Ù': case L'Ü': case L'Û': c = L'u'; break;
            case L'ñ': case L'Ñ': c = L'n'; break;
            default:
                if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
                break;
            }
            r += c;
        }
        return r;
    }

    long long DiasDesde(const SYSTEMTIME& fechaLocal)
    {
        SYSTEMTIME hoy;
        GetLocalTime(&hoy);
        SYSTEMTIME a = {}, b = {};
        a.wYear = fechaLocal.wYear; a.wMonth = fechaLocal.wMonth; a.wDay = fechaLocal.wDay;
        b.wYear = hoy.wYear; b.wMonth = hoy.wMonth; b.wDay = hoy.wDay;
        FILETIME fa, fb;
        if (!SystemTimeToFileTime(&a, &fa) || !SystemTimeToFileTime(&b, &fb)) return 0;
        ULARGE_INTEGER ua, ub;
        ua.LowPart = fa.dwLowDateTime; ua.HighPart = fa.dwHighDateTime;
        ub.LowPart = fb.dwLowDateTime; ub.HighPart = fb.dwHighDateTime;
        const long long dia = 864000000000LL;   // en unidades de 100 ns
        return (static_cast<long long>(ub.QuadPart) - static_cast<long long>(ua.QuadPart)) / dia;
    }

    std::wstring DesdeUtf8(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring r(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n);
        return r;
    }

    std::string AUtf8(const std::wstring& s)
    {
        if (s.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string r(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
        return r;
    }
}
