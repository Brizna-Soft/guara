#pragma once

// Colores de la ventana. Siguen el tema de Windows (claro u oscuro).

#include "modelo.hpp"

#include <wx/colour.h>

namespace tema
{
    bool Oscuro();

    wxColour Fondo();        // detrás de las tarjetas
    wxColour Tarjeta();      // fondo de cada tarjeta
    wxColour Borde();
    wxColour TextoSuave();   // etiquetas y notas
    wxColour Enlace();

    wxColour Relleno(Estado e);   // el círculo del semáforo
    wxColour Simbolo(Estado e);   // el dibujo dentro del círculo
    wxColour Letra(Estado e);     // el texto «Bien», «Revisa esto»...
}
