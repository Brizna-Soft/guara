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
        linea(L"");
        linea(L"Sobre este informe:");
        linea(L"- Lleva datos de la PC (piezas, versiones, espacio libre y avisos) y la fecha y el código de los apagados");
        linea(L"  inesperados y pantallas azules de los últimos 30 días.");
        linea(L"- Guara no pone en el informe números de serie, direcciones IP ni MAC, ni el contenido de tus archivos.");
        linea(L"- No lleva los nombres de los programas de inicio (solo cuántos son) ni los de impresoras, aparatos");
        linea(L"  Bluetooth, teléfonos o salidas de audio con problemas (solo el tipo de aparato).");
        linea(L"- Además, Guara quita lo mejor que puede tu nombre de usuario, el nombre de la PC y los correos, y cambia");
        linea(L"  las rutas de tu carpeta personal por %USERPROFILE%. Revísalo antes de compartirlo.");
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
            if (!s.lista.empty() && s.listaEnInforme)
            {
                linea(s.tituloLista + L":");
                for (const auto& e : s.lista) linea(L"  * " + LimpiarInforme(e.textoInforme.empty() ? e.texto : e.textoInforme));
            }
            else if (!s.lista.empty())
                linea(s.tituloLista + texto::Formato(L": %d (los nombres no van en el informe).", static_cast<int>(s.lista.size())));
        }
        linea(L"");
        linea(L"Hecho con Guara, gratis y de código abierto: https://github.com/" + texto::DesdeUtf8(APP_REPO));
        return privacidad::LimpiarFinal(t);
    }
}
