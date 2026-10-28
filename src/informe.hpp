#pragma once

// El informe en texto para pedir ayuda. No lleva el nombre de usuario ni el del equipo, números de serie,
// direcciones IP ni MAC, y las rutas personales aparecen como %USERPROFILE%.

#include "modelo.hpp"

#include <string>

namespace informe
{
    // Texto con saltos de línea «\n»
    std::wstring Generar(const Resultado& r);
}
