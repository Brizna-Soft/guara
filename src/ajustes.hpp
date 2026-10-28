#pragma once

// Ajustes de la ventana (tamaño, posición y si estaba maximizada), en %LOCALAPPDATA%\Guara\ajustes.json

namespace ajustes
{
    struct Ventana
    {
        int x = 0, y = 0, ancho = 0, alto = 0;
        bool maximizada = false;
        bool valida = false;   // false si no había ajustes guardados
    };

    Ventana Cargar();
    void Guardar(const Ventana& v);
}
