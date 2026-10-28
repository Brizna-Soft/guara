#include "modelo.hpp"

Estado Peor(Estado a, Estado b)
{
    return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

namespace
{
    int Contar(const Resultado& r, Estado e)
    {
        int n = 0;
        for (const auto& s : r.secciones)
            for (const auto& a : s.avisos)
                if (a.estado == e) n++;
        return n;
    }
}

Estado EstadoGeneral(const Resultado& r)
{
    if (Contar(r, Estado::Problema) > 0) return Estado::Problema;
    if (Contar(r, Estado::Revisa) > 0) return Estado::Revisa;
    return Estado::Bien;
}

std::wstring TituloResumen(const Resultado& r)
{
    int problemas = Contar(r, Estado::Problema);
    int revisar = Contar(r, Estado::Revisa);
    auto problemasTexto = [&] {
        return problemas == 1 ? std::wstring(L"1 problema") : std::to_wstring(problemas) + L" problemas";
    };
    auto revisarTexto = [&] {
        return revisar == 1 ? std::wstring(L"1 cosa para revisar") : std::to_wstring(revisar) + L" cosas para revisar";
    };
    if (problemas == 0 && revisar == 0) return L"Tu PC está bien";
    if (problemas == 0) return L"Hay " + revisarTexto();
    if (revisar == 0) return L"Hay " + problemasTexto();
    return L"Hay " + problemasTexto() + L" y " + revisarTexto();
}

std::wstring NombreEstado(Estado e)
{
    switch (e)
    {
    case Estado::Bien: return L"Bien";
    case Estado::Revisa: return L"Revisa esto";
    case Estado::Problema: return L"Problema";
    case Estado::NoDisponible: break;
    }
    return L"No disponible";
}
