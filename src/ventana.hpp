#pragma once

// La ventana principal: la pestaña «Revisión de la PC» (tarjetas con semáforo) y la de «Buscar soluciones».

#include <wx/frame.h>

#include <string>

class wxNotebook;
class PanelRevision;
class PanelSoluciones;

class Ventana : public wxFrame
{
public:
    Ventana();

    // Pasa a la pestaña de soluciones y muestra la que tiene ese id
    void MostrarSolucion(const std::string& id);

private:
    void AlCerrar(wxCloseEvent& e);
    void RecordarTamano();

    wxNotebook* libro_ = nullptr;
    PanelRevision* revision_ = nullptr;
    PanelSoluciones* soluciones_ = nullptr;
    wxRect normal_;   // el último tamaño y posición sin maximizar
};

// La llama la aplicación al salir: desde ese momento, el hilo de la revisión ya no avisa a la ventana
void AppTermina();
