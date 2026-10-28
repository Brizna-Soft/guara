#pragma once

// La revisión de la PC. Todo es de solo lectura: WMI, el registro de eventos, el registro de Windows y la
// API de Windows. Nada se cambia y nada pide ser administrador. Se llama desde un hilo aparte.

#include "modelo.hpp"

#include <functional>
#include <string>

namespace revision
{
    // «progreso» recibe textos como «Revisando los discos...» (se llama desde el mismo hilo)
    Resultado Revisar(const std::function<void(const std::wstring&)>& progreso);
}
