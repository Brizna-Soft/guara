#include "tema.hpp"

#include <wx/settings.h>

namespace tema
{
    bool Oscuro()
    {
        return wxSystemSettings::GetAppearance().IsDark();
    }

    wxColour Fondo() { return Oscuro() ? wxColour(0x1C, 0x1C, 0x1C) : wxColour(0xF3, 0xF3, 0xF3); }
    wxColour Tarjeta() { return Oscuro() ? wxColour(0x2B, 0x2B, 0x2B) : wxColour(0xFF, 0xFF, 0xFF); }
    wxColour Borde() { return Oscuro() ? wxColour(0x3D, 0x3D, 0x3D) : wxColour(0xE0, 0xE0, 0xE0); }
    wxColour TextoSuave() { return Oscuro() ? wxColour(0xB8, 0xB8, 0xB8) : wxColour(0x5C, 0x5C, 0x5C); }
    wxColour Enlace() { return Oscuro() ? wxColour(0x99, 0xC3, 0xFF) : wxColour(0x00, 0x5F, 0xB8); }

    wxColour Relleno(Estado e)
    {
        switch (e)
        {
        case Estado::Bien: return wxColour(0x2E, 0x9E, 0x4F);
        case Estado::Revisa: return wxColour(0xF2, 0xC1, 0x2E);
        case Estado::Problema: return wxColour(0xD8, 0x3B, 0x3B);
        case Estado::NoDisponible: break;
        }
        return wxColour(0x8A, 0x8A, 0x8A);
    }

    wxColour Simbolo(Estado e)
    {
        // Sobre el amarillo, un dibujo oscuro se lee mejor que uno blanco
        return e == Estado::Revisa ? wxColour(0x3A, 0x2E, 0x00) : wxColour(0xFF, 0xFF, 0xFF);
    }

    wxColour Letra(Estado e)
    {
        bool o = Oscuro();
        switch (e)
        {
        case Estado::Bien: return o ? wxColour(0x6C, 0xCB, 0x5F) : wxColour(0x0F, 0x7B, 0x0F);
        case Estado::Revisa: return o ? wxColour(0xFC, 0xE1, 0x00) : wxColour(0x9D, 0x5D, 0x00);
        case Estado::Problema: return o ? wxColour(0xFF, 0x99, 0xA4) : wxColour(0xC4, 0x2B, 0x1C);
        case Estado::NoDisponible: break;
        }
        return o ? wxColour(0xA8, 0xA8, 0xA8) : wxColour(0x6B, 0x6B, 0x6B);
    }
}
