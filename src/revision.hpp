#pragma once

// La revisión de la PC. Todo es de solo lectura: WMI, el registro de eventos, el registro de Windows y la
// API de Windows. Nada se cambia y nada pide ser administrador. Se llama desde un hilo aparte.

#include "modelo.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace revision
{
    // «progreso» recibe textos como «Revisando los discos...» (se llama desde el mismo hilo).
    // «cancelar» se mira entre secciones y dentro de las consultas largas: si se activa, la revisión termina
    // pronto y devuelve lo que alcanzó a leer.
    Resultado Revisar(const std::function<void(const std::wstring&)>& progreso, const std::atomic<bool>& cancelar);
}
