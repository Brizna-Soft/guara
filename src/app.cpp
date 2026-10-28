// Guara, de Brizna Soft: revisa la PC sin cambiar nada y explica en español sencillo qué tiene y qué anda mal.

#include "ventana.hpp"

#include <wx/app.h>

class App : public wxApp
{
public:
    bool OnInit() override
    {
        // No se llama a wxApp::OnInit: así wxWidgets no interpreta la línea de comandos (y no muestra avisos en inglés)
        SetAppName(L"Guara");
        SetAppDisplayName(L"Guara");
        SetVendorName(L"Brizna Soft");
        MSWEnableDarkMode();   // sigue el tema de Windows (claro u oscuro)

        auto* ventana = new Ventana();
        ventana->Show();
        return true;
    }

    int OnExit() override
    {
        AppTermina();
        return wxApp::OnExit();
    }
};

wxIMPLEMENT_APP(App);
