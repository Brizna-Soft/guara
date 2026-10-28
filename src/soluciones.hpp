#pragma once

// Buscador de soluciones: la lista de problemas comunes va dentro del .exe (res\soluciones.json, recurso
// SOLUCIONES). Para agregar uno, basta con sumarlo a ese archivo y volver a compilar.

#include <string>
#include <vector>

namespace soluciones
{
    struct Herramienta
    {
        std::wstring texto;   // texto del botón
        std::wstring abrir;   // qué abre (ver AbrirHerramienta)
    };

    struct Solucion
    {
        std::string id;
        std::wstring titulo;
        std::vector<std::wstring> palabras;
        std::vector<std::wstring> pasos;
        std::vector<Herramienta> herramientas;
        std::wstring enlace;      // «Más información»: página oficial de Microsoft
        std::wstring busqueda;    // todo el texto, en minúsculas y sin tildes
    };

    const std::vector<Solucion>& Todas();
    const Solucion* Buscar(const std::string& id);

    // Las que tienen todas las palabras de la consulta (sin importar mayúsculas ni tildes)
    std::vector<const Solucion*> Filtrar(const std::wstring& consulta);

    // true solo para páginas de ayuda de Microsoft: https://support.microsoft.com/ o https://learn.microsoft.com/
    bool EnlacePermitido(const std::wstring& url);

    // true solo para las herramientas de la lista fija (ver soluciones.cpp)
    bool HerramientaPermitida(const std::wstring& abrir);

    // Abre una herramienta de Windows de la lista fija: páginas concretas de Configuración, Seguridad de Windows,
    // el Administrador de tareas, el de dispositivos, el Liberador de espacio y el Diagnóstico de memoria.
    bool AbrirHerramienta(const std::wstring& abrir);

    // Abre la página de ayuda en el navegador, solo si EnlacePermitido() la acepta
    bool AbrirEnlace(const std::wstring& url);
}
