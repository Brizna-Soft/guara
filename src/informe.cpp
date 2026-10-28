#include "informe.hpp"

#include "privacidad.hpp"
#include "texto.hpp"
#include "version.h"

namespace informe
{
    std::wstring Generar(const Resultado& r)
    {
        using privacidad::LimpiarInforme;
        std::wstring t;
        auto linea = [&t](const std::wstring& s) {
            t += s;
            t += L"\n";
        };

        linea(L"Informe de Guara " + texto::DesdeUtf8(APP_VERSION_TEXTO) + L" (Brizna Soft)");
        linea(L"Revisión del " + r.fecha + L".");
        linea(L"No incluye el nombre de usuario ni el del equipo, números de serie, direcciones IP ni MAC.");
        linea(L"Las rutas de la carpeta personal aparecen como %USERPROFILE%.");
        linea(L"");
        linea(L"== Resumen: " + TituloResumen(r) + L" ==");
        if (!r.resumenEquipo.empty()) linea(L"Equipo: " + LimpiarInforme(r.resumenEquipo));
        for (Estado e : { Estado::Problema, Estado::Revisa })
            for (const auto& s : r.secciones)
                for (const auto& a : s.avisos)
                    if (a.estado == e) linea(L"- [" + NombreEstado(e) + L"] " + s.titulo + L": " + LimpiarInforme(a.texto));

        for (const auto& s : r.secciones)
        {
            linea(L"");
            linea(L"== " + s.titulo + L": " + NombreEstado(s.estado) + L" ==");
            for (const auto& d : s.datos)
                linea(d.etiqueta + (!d.etiqueta.empty() && d.etiqueta.back() == L':' ? L" " : L": ") + LimpiarInforme(d.valor));
            for (const auto& a : s.avisos) linea(L"- [" + NombreEstado(a.estado) + L"] " + LimpiarInforme(a.texto));
            if (!s.lista.empty())
            {
                linea(s.tituloLista + L":");
                for (const auto& e : s.lista) linea(L"  * " + LimpiarInforme(e.textoInforme.empty() ? e.texto : e.textoInforme));
            }
        }
        linea(L"");
        linea(L"Hecho con Guara, gratis y de código abierto: https://github.com/" + texto::DesdeUtf8(APP_REPO));
        return privacidad::LimpiarFinal(t);
    }
}
