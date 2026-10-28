#include "ventana.hpp"

#include "ajustes.hpp"
#include "ayuda.hpp"
#include "informe.hpp"
#include "privacidad.hpp"
#include "revision.hpp"
#include "soluciones.hpp"
#include "tema.hpp"
#include "texto.hpp"
#include "version.h"

#include <wx/activityindicator.h>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/generic/hyperlink.h>
#include <wx/graphics.h>
#include <wx/iconbndl.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// ==================================================================== piezas de dibujo

namespace
{
    // Círculo del semáforo con un dibujo, para que se entienda también sin ver colores:
    // visto (bien), signo de exclamación (revisa esto), equis (problema), raya (no disponible)
    class Punto : public wxWindow
    {
    public:
        Punto(wxWindow* padre, Estado estado, int diametroDip)
            : wxWindow(padre, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), estado_(estado)
        {
            SetBackgroundStyle(wxBG_STYLE_PAINT);
            wxSize t = FromDIP(wxSize(diametroDip, diametroDip));
            SetMinSize(t);
            SetSize(t);
            Bind(wxEVT_PAINT, &Punto::AlPintar, this);
        }

        bool AcceptsFocus() const override { return false; }

    private:
        void AlPintar(wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
            if (!g) return;
            wxSize sz = GetClientSize();
            double d = std::min(sz.x, sz.y) - 1.0;
            double x = (sz.x - d) / 2.0, y = (sz.y - d) / 2.0;
            g->SetPen(*wxTRANSPARENT_PEN);
            g->SetBrush(wxBrush(tema::Relleno(estado_)));
            g->DrawEllipse(x, y, d, d);

            double grosor = std::max(1.6, d / 7.5);
            g->SetPen(g->CreatePen(wxGraphicsPenInfo(tema::Simbolo(estado_)).Width(grosor).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));
            auto P = [&](double fx, double fy) { return wxPoint2DDouble(x + d * fx, y + d * fy); };
            wxGraphicsPath p = g->CreatePath();
            switch (estado_)
            {
            case Estado::Bien:
                p.MoveToPoint(P(0.28, 0.53));
                p.AddLineToPoint(P(0.44, 0.69));
                p.AddLineToPoint(P(0.73, 0.36));
                break;
            case Estado::Revisa:
                p.MoveToPoint(P(0.5, 0.26));
                p.AddLineToPoint(P(0.5, 0.56));
                p.MoveToPoint(P(0.5, 0.74));
                p.AddLineToPoint(P(0.5, 0.75));
                break;
            case Estado::Problema:
                p.MoveToPoint(P(0.34, 0.34));
                p.AddLineToPoint(P(0.66, 0.66));
                p.MoveToPoint(P(0.66, 0.34));
                p.AddLineToPoint(P(0.34, 0.66));
                break;
            case Estado::NoDisponible:
                p.MoveToPoint(P(0.3, 0.5));
                p.AddLineToPoint(P(0.7, 0.5));
                break;
            }
            g->StrokePath(p);
        }

        Estado estado_;
    };

    // Tarjeta con esquinas redondeadas
    class Tarjeta : public wxPanel
    {
    public:
        explicit Tarjeta(wxWindow* padre)
            : wxPanel(padre, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE)
        {
            SetBackgroundStyle(wxBG_STYLE_PAINT);
            SetBackgroundColour(tema::Tarjeta());
            Bind(wxEVT_PAINT, &Tarjeta::AlPintar, this);
        }

    private:
        void AlPintar(wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
            if (!g) return;
            wxSize sz = GetClientSize();
            g->SetBrush(wxBrush(tema::Tarjeta()));
            g->SetPen(wxPen(tema::Borde(), 1));
            g->DrawRoundedRectangle(0.5, 0.5, sz.x - 1.0, sz.y - 1.0, FromDIP(8));
        }
    };

    // Los textos largos se cortan en líneas según el ancho de la ventana, y se vuelven a cortar al cambiarlo
    class Ajustador
    {
    public:
        explicit Ajustador(wxScrolledWindow* lienzo) : lienzo_(lienzo)
        {
            lienzo_->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
                e.Skip();
                if (pendiente_) return;
                pendiente_ = true;
                lienzo_->CallAfter([this] {
                    pendiente_ = false;
                    Aplicar(false);
                });
            });
        }
        Ajustador(const Ajustador&) = delete;
        Ajustador& operator=(const Ajustador&) = delete;

        int Ancho() const
        {
            int w = lienzo_->GetClientSize().x;
            return w > 100 ? w : lienzo_->FromDIP(900);
        }

        // «restar»: lo que ocupan los márgenes y lo que va al lado del texto en la misma fila
        wxStaticText* Texto(wxWindow* padre, const wxString& s, int restar)
        {
            auto* t = new wxStaticText(padre, wxID_ANY, wxEmptyString);
            t->SetLabelText(s);
            int w = Ancho() - restar;
            if (w > lienzo_->FromDIP(60)) t->Wrap(w);
            items_.push_back({ t, restar });
            return t;
        }

        void Limpiar()
        {
            items_.clear();
            ancho_ = Ancho();
        }

        void Aplicar(bool forzar)
        {
            int ancho = Ancho();
            if (!forzar && ancho == ancho_) return;
            ancho_ = ancho;
            if (items_.empty()) return;
            lienzo_->Freeze();
            // En wxWidgets 3.3, Wrap() guarda el texto original y vuelve a cortarlo desde ahí; con el mismo
            // ancho no hace nada (por eso no hay que volver a poner el texto antes)
            for (auto& a : items_)
            {
                int w = ancho - a.restar;
                if (w > lienzo_->FromDIP(60)) a.texto->Wrap(w);
            }
            lienzo_->Layout();
            lienzo_->FitInside();
            lienzo_->Thaw();
        }

    private:
        struct Item
        {
            wxStaticText* texto;
            int restar;
        };
        wxScrolledWindow* lienzo_;
        std::vector<Item> items_;
        int ancho_ = -1;
        bool pendiente_ = false;
    };

    // ---------------------------------------------------------------- avisos desde el hilo de la revisión

    PanelRevision* panelActual = nullptr;   // solo se usa en el hilo principal

    std::mutex& MutexApp()
    {
        static auto* m = new std::mutex();   // nunca se destruye: el hilo puede usarlo hasta el final
        return *m;
    }
    bool appViva = true;   // protegido por MutexApp()

    void AvisarAlPanel(std::function<void(PanelRevision*)> f)
    {
        std::lock_guard<std::mutex> candado(MutexApp());
        if (!appViva || !wxTheApp) return;
        wxTheApp->CallAfter([f]() {
            if (panelActual) f(panelActual);
        });
    }
}

void AppTermina()
{
    std::lock_guard<std::mutex> candado(MutexApp());
    appViva = false;
}

// ==================================================================== pestaña «Revisión de la PC»

class PanelRevision : public wxPanel
{
public:
    PanelRevision(wxWindow* padre, Ventana* ventana);
    ~PanelRevision() override
    {
        if (panelActual == this) panelActual = nullptr;
        // Por si la ventana se destruye sin pasar por Detener(): un std::thread activo no se puede destruir
        if (hilo_.joinable())
        {
            *cancelar_ = true;
            hilo_.detach();
        }
    }

    void Revisar();
    // Cancela la revisión en curso y espera un poco a que termine (se llama al cerrar la ventana)
    void Detener();
    void CopiarInforme();
    void GuardarInforme();
    void Progreso(const std::wstring& t, int generacion);
    void Mostrar(std::shared_ptr<Resultado> r, int generacion);

private:
    int Margen() const { return FromDIP(12); }
    int Relleno() const { return FromDIP(16); }
    int Hueco() const { return FromDIP(8); }

    void ActualizarBotones();
    void ConstruirEspera();
    void Construir();
    void ConstruirResumen(wxSizer* caja);
    void ConstruirSeccion(wxSizer* caja, const Seccion& s);
    void AgregarAviso(wxWindow* tarjeta, wxSizer* v, const Aviso& a, const wxString& prefijo);
    void Limpiar();

    Ventana* ventana_;
    wxButton* botonRevisar_ = nullptr;
    wxButton* botonCopiar_ = nullptr;
    wxButton* botonGuardar_ = nullptr;
    wxActivityIndicator* indicador_ = nullptr;
    wxStaticText* estado_ = nullptr;
    wxStaticText* esperaTexto_ = nullptr;
    wxScrolledWindow* lienzo_ = nullptr;
    std::unique_ptr<Ajustador> ajuste_;
    std::shared_ptr<Resultado> resultado_;
    bool revisando_ = false;
    int generacion_ = 0;
    std::thread hilo_;                              // el hilo de la revisión, administrado por el panel
    std::shared_ptr<std::atomic<bool>> cancelar_;   // se activa al cerrar; la revisión lo mira entre secciones
    std::shared_ptr<std::atomic<bool>> terminado_;  // lo activa el hilo al acabar
};

PanelRevision::PanelRevision(wxWindow* padre, Ventana* ventana) : wxPanel(padre), ventana_(ventana)
{
    panelActual = this;
    auto* v = new wxBoxSizer(wxVERTICAL);

    auto* barra = new wxBoxSizer(wxHORIZONTAL);
    botonRevisar_ = new wxButton(this, wxID_ANY, L"Volver a revisar");
    botonCopiar_ = new wxButton(this, wxID_ANY, L"Copiar informe");
    botonGuardar_ = new wxButton(this, wxID_ANY, L"Guardar informe...");
    botonCopiar_->SetToolTip(L"Copia un informe para pedir ayuda, sin tu nombre de usuario ni el de tu PC. Revísalo antes de compartirlo.");
    botonGuardar_->SetToolTip(L"Guarda el mismo informe en un archivo de texto.");
    indicador_ = new wxActivityIndicator(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(20, 20)));
    estado_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    estado_->SetMinSize(wxSize(FromDIP(80), -1));
    barra->Add(botonRevisar_, 0, wxALIGN_CENTER_VERTICAL);
    barra->Add(botonCopiar_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, Hueco());
    barra->Add(botonGuardar_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, Hueco());
    barra->Add(indicador_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
    barra->Add(estado_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, Hueco());
    v->Add(barra, 0, wxEXPAND | wxALL, FromDIP(10));

    lienzo_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
    lienzo_->SetBackgroundColour(tema::Fondo());
    lienzo_->SetScrollRate(0, FromDIP(20));
    // Sin esto, el tamaño mínimo sería el de todo el contenido y el panel no se achicaría a la ventana
    lienzo_->SetMinSize(FromDIP(wxSize(200, 100)));
    lienzo_->SetSizer(new wxBoxSizer(wxVERTICAL));
    v->Add(lienzo_, 1, wxEXPAND);
    SetSizer(v);
    ajuste_ = std::make_unique<Ajustador>(lienzo_);

    botonRevisar_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Revisar(); });
    botonCopiar_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CopiarInforme(); });
    botonGuardar_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GuardarInforme(); });
    indicador_->Hide();
    ActualizarBotones();
}

void PanelRevision::ActualizarBotones()
{
    botonRevisar_->Enable(!revisando_);
    botonCopiar_->Enable(!revisando_ && resultado_ != nullptr);
    botonGuardar_->Enable(!revisando_ && resultado_ != nullptr);
}

void PanelRevision::Revisar()
{
    if (revisando_) return;
    revisando_ = true;
    int generacion = ++generacion_;
    estado_->SetLabelText(L"Revisando...");
    indicador_->Show();
    indicador_->Start();
    Layout();
    ActualizarBotones();
    if (!resultado_) ConstruirEspera();

    // La revisión anterior ya terminó (revisando_ era false): solo falta recoger su hilo
    if (hilo_.joinable()) hilo_.join();
    cancelar_ = std::make_shared<std::atomic<bool>>(false);
    terminado_ = std::make_shared<std::atomic<bool>>(false);
    hilo_ = std::thread([generacion, cancelar = cancelar_, terminado = terminado_]() {
        std::shared_ptr<Resultado> r;
        try
        {
            auto progreso = [generacion](const std::wstring& t) {
                AvisarAlPanel([t, generacion](PanelRevision* p) { p->Progreso(t, generacion); });
            };
            r = std::make_shared<Resultado>(revision::Revisar(progreso, *cancelar));
        }
        catch (...)
        {
            r = std::make_shared<Resultado>();
        }
        if (!*cancelar) AvisarAlPanel([r, generacion](PanelRevision* p) { p->Mostrar(r, generacion); });
        *terminado = true;
    });
}

void PanelRevision::Detener()
{
    if (!hilo_.joinable()) return;
    *cancelar_ = true;
    // La revisión mira «cancelar» entre secciones y dentro de las consultas largas, así que suele terminar
    // enseguida. Si una consulta de Windows tarda más de 3 segundos, el hilo se suelta y termina con el programa.
    for (int i = 0; i < 60 && !*terminado_; i++) Sleep(50);
    if (*terminado_)
        hilo_.join();
    else
        hilo_.detach();
}

void PanelRevision::Progreso(const std::wstring& t, int generacion)
{
    if (generacion != generacion_ || !revisando_) return;
    estado_->SetLabelText(t);
    if (esperaTexto_) esperaTexto_->SetLabelText(t + L" Guara solo lee datos: no cambia nada en tu PC.");
}

void PanelRevision::Mostrar(std::shared_ptr<Resultado> r, int generacion)
{
    if (generacion != generacion_) return;
    revisando_ = false;
    indicador_->Stop();
    indicador_->Hide();
    resultado_ = std::move(r);
    if (resultado_->secciones.empty())
        estado_->SetLabelText(L"No pude terminar la revisión. Prueba con «Volver a revisar».");
    else
        estado_->SetLabelText(L"Revisado el " + resultado_->fecha + L".");
    Layout();
    ActualizarBotones();
    Construir();
}

void PanelRevision::Limpiar()
{
    lienzo_->DestroyChildren();
    esperaTexto_ = nullptr;
    ajuste_->Limpiar();
}

void PanelRevision::ConstruirEspera()
{
    lienzo_->Freeze();
    Limpiar();
    auto* caja = new wxBoxSizer(wxVERTICAL);
    auto* t = new Tarjeta(lienzo_);
    auto* v = new wxBoxSizer(wxVERTICAL);
    auto* titulo = new wxStaticText(t, wxID_ANY, L"Revisando tu PC...");
    titulo->SetFont(titulo->GetFont().Bold().Scaled(1.4f));
    v->Add(titulo, 0, wxLEFT | wxRIGHT | wxTOP, Relleno());
    esperaTexto_ = new wxStaticText(t, wxID_ANY, L"Esto toma unos segundos. Guara solo lee datos: no cambia nada en tu PC.");
    esperaTexto_->SetForegroundColour(tema::TextoSuave());
    v->Add(esperaTexto_, 0, wxALL, Relleno());
    t->SetSizer(v);
    caja->Add(t, 0, wxEXPAND | wxALL, Margen());
    lienzo_->SetSizer(caja);
    lienzo_->Layout();
    lienzo_->FitInside();
    lienzo_->Thaw();
}

void PanelRevision::Construir()
{
    if (!resultado_) return;
    lienzo_->Freeze();
    Limpiar();
    auto* caja = new wxBoxSizer(wxVERTICAL);
    ConstruirResumen(caja);
    for (const auto& s : resultado_->secciones) ConstruirSeccion(caja, s);
    caja->AddSpacer(Margen());
    lienzo_->SetSizer(caja);
    lienzo_->Layout();
    lienzo_->FitInside();
    lienzo_->Scroll(0, 0);
    lienzo_->Thaw();
    // Si al llenar apareció la barra de desplazamiento, el ancho cambió: se vuelven a cortar los textos
    CallAfter([this] { ajuste_->Aplicar(false); });
}

void PanelRevision::AgregarAviso(wxWindow* tarjeta, wxSizer* v, const Aviso& a, const wxString& prefijo)
{
    const int punto = 16;
    auto* h = new wxBoxSizer(wxHORIZONTAL);
    h->Add(new Punto(tarjeta, a.estado, punto), 0, wxTOP, FromDIP(1));
    int restar = 2 * Margen() + 2 * Relleno() + FromDIP(punto) + Hueco();
    wxButton* boton = nullptr;
    if (!a.solucion.empty() && soluciones::Buscar(a.solucion))
    {
        boton = new wxButton(tarjeta, wxID_ANY, L"Ver solución", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        restar += boton->GetBestSize().x + Hueco();
        std::string id = a.solucion;
        boton->Bind(wxEVT_BUTTON, [this, id](wxCommandEvent&) { ventana_->MostrarSolucion(id); });
    }
    h->Add(ajuste_->Texto(tarjeta, prefijo + wxString(a.texto), restar), 1, wxLEFT, Hueco());
    if (boton) h->Add(boton, 0, wxLEFT | wxALIGN_TOP, Hueco());
    v->Add(h, 0, wxEXPAND | wxLEFT | wxRIGHT, Relleno());
    v->AddSpacer(FromDIP(8));
}

void PanelRevision::ConstruirResumen(wxSizer* caja)
{
    const Resultado& r = *resultado_;
    auto* t = new Tarjeta(lienzo_);
    auto* v = new wxBoxSizer(wxVERTICAL);

    const int punto = 34;
    auto* h = new wxBoxSizer(wxHORIZONTAL);
    h->Add(new Punto(t, r.secciones.empty() ? Estado::NoDisponible : EstadoGeneral(r), punto), 0, wxALIGN_CENTER_VERTICAL);
    auto* columna = new wxBoxSizer(wxVERTICAL);
    auto* titulo = new wxStaticText(t, wxID_ANY, wxEmptyString);
    titulo->SetLabelText(r.secciones.empty() ? std::wstring(L"No pude revisar la PC") : TituloResumen(r));
    titulo->SetFont(titulo->GetFont().Bold().Scaled(1.45f));
    columna->Add(titulo);
    int restar = 2 * Margen() + 2 * Relleno() + FromDIP(punto) + FromDIP(14);
    auto* sub = ajuste_->Texto(t, L"Revisado el " + r.fecha + L". Guara solo leyó datos: no cambió nada en tu PC.", restar);
    sub->SetForegroundColour(tema::TextoSuave());
    columna->Add(sub, 0, wxTOP, FromDIP(2));
    h->Add(columna, 1, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(14));
    v->Add(h, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, Relleno());
    v->AddSpacer(FromDIP(14));

    int importantes = 0;
    for (Estado e : { Estado::Problema, Estado::Revisa })
        for (const auto& s : r.secciones)
            for (const auto& a : s.avisos)
                if (a.estado == e)
                {
                    AgregarAviso(t, v, a, s.titulo + L": ");
                    importantes++;
                }
    if (importantes == 0 && !r.secciones.empty())
    {
        Aviso a;
        a.estado = Estado::Bien;
        a.texto = L"No encontré nada que arreglar. Si alguien te ayuda con tu PC, puedes pasarle el informe.";
        AgregarAviso(t, v, a, wxEmptyString);
    }
    if (!r.resumenEquipo.empty())
    {
        auto* equipo = ajuste_->Texto(t, r.resumenEquipo, 2 * Margen() + 2 * Relleno());
        equipo->SetForegroundColour(tema::TextoSuave());
        v->Add(equipo, 0, wxLEFT | wxRIGHT, Relleno());
        v->AddSpacer(FromDIP(8));
    }
    v->AddSpacer(FromDIP(8));
    t->SetSizer(v);
    caja->Add(t, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, Margen());
}

void PanelRevision::ConstruirSeccion(wxSizer* caja, const Seccion& s)
{
    auto* t = new Tarjeta(lienzo_);
    auto* v = new wxBoxSizer(wxVERTICAL);

    auto* h = new wxBoxSizer(wxHORIZONTAL);
    h->Add(new Punto(t, s.estado, 22), 0, wxALIGN_CENTER_VERTICAL);
    auto* titulo = new wxStaticText(t, wxID_ANY, wxEmptyString);
    titulo->SetLabelText(s.titulo);
    titulo->SetFont(titulo->GetFont().Bold().Scaled(1.15f));
    h->Add(titulo, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    auto* estado = new wxStaticText(t, wxID_ANY, wxEmptyString);
    estado->SetLabelText(NombreEstado(s.estado));
    estado->SetForegroundColour(tema::Letra(s.estado));
    estado->SetFont(estado->GetFont().Bold());
    h->Add(estado, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    v->Add(h, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, Relleno());
    v->AddSpacer(FromDIP(12));

    if (!s.datos.empty())
    {
        const int anchoEtiqueta = FromDIP(190), separacion = FromDIP(12);
        auto* g = new wxFlexGridSizer(2, FromDIP(5), separacion);
        g->AddGrowableCol(1, 1);
        for (const auto& d : s.datos)
        {
            auto* e = new wxStaticText(t, wxID_ANY, wxEmptyString);
            e->SetLabelText(d.etiqueta);
            e->Wrap(anchoEtiqueta);
            e->SetForegroundColour(tema::TextoSuave());
            e->SetMinSize(wxSize(anchoEtiqueta, -1));
            g->Add(e, 0, wxALIGN_TOP);
            g->Add(ajuste_->Texto(t, d.valor, 2 * Margen() + 2 * Relleno() + anchoEtiqueta + separacion), 1, wxEXPAND);
        }
        v->Add(g, 0, wxEXPAND | wxLEFT | wxRIGHT, Relleno());
        v->AddSpacer(FromDIP(12));
    }

    for (const auto& a : s.avisos) AgregarAviso(t, v, a, wxEmptyString);

    if (!s.lista.empty())
    {
        v->AddSpacer(FromDIP(2));
        auto* encabezado = new wxStaticText(t, wxID_ANY, wxEmptyString);
        encabezado->SetLabelText(s.tituloLista + L":");
        encabezado->SetFont(encabezado->GetFont().Bold());
        v->Add(encabezado, 0, wxLEFT | wxRIGHT, Relleno());
        v->AddSpacer(FromDIP(4));
        const int sangria = FromDIP(14);
        for (const auto& e : s.lista)
        {
            auto* fila = new wxBoxSizer(wxHORIZONTAL);
            fila->AddSpacer(sangria);
            fila->Add(ajuste_->Texto(t, L"•  " + e.texto, 2 * Margen() + 2 * Relleno() + sangria), 1);
            v->Add(fila, 0, wxEXPAND | wxLEFT | wxRIGHT, Relleno());
            v->AddSpacer(FromDIP(3));
        }
        v->AddSpacer(FromDIP(6));
    }

    if (!s.acciones.empty())
    {
        auto* fila = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);   // sin estirar el último botón
        for (const auto& accion : s.acciones)
        {
            auto* b = new wxButton(t, wxID_ANY, accion.texto);
            std::wstring abrir = accion.abrir;
            b->Bind(wxEVT_BUTTON, [abrir](wxCommandEvent&) { soluciones::AbrirHerramienta(abrir); });
            fila->Add(b, 0, wxRIGHT | wxBOTTOM, Hueco());
        }
        v->Add(fila, 0, wxEXPAND | wxLEFT | wxRIGHT, Relleno());
    }
    v->AddSpacer(FromDIP(8));
    t->SetSizer(v);
    caja->Add(t, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, Margen());
}

void PanelRevision::CopiarInforme()
{
    if (!resultado_ || revisando_) return;
    std::wstring t = informe::Generar(*resultado_);
    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(wxString(t)));
        wxTheClipboard->Close();
        wxTheClipboard->Flush();   // el informe sigue en el portapapeles aunque se cierre Guara
        estado_->SetLabelText(L"Informe copiado, sin tu nombre de usuario ni el de tu PC. Revísalo antes de pegarlo donde pidas ayuda.");
    }
    else
        estado_->SetLabelText(L"No pude usar el portapapeles. Inténtalo otra vez.");
}

void PanelRevision::GuardarInforme()
{
    if (!resultado_ || revisando_) return;
    SYSTEMTIME hoy;
    GetLocalTime(&hoy);
    wxString nombre = wxString::Format(L"informe-guara-%04u-%02u-%02u.txt", hoy.wYear, hoy.wMonth, hoy.wDay);
    wxFileDialog d(this, L"Guardar informe", wxEmptyString, nombre, L"Texto (*.txt)|*.txt", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (d.ShowModal() != wxID_OK) return;

    std::wstring t = informe::Generar(*resultado_);
    std::wstring crlf;
    crlf.reserve(t.size() + t.size() / 20);
    for (wchar_t c : t)
    {
        if (c == L'\n') crlf += L'\r';
        crlf += c;
    }
    std::string datos = "\xEF\xBB\xBF" + texto::AUtf8(crlf);   // UTF-8 con BOM, para que el Bloc de notas lo abra bien
    wxFile f;
    if (f.Create(d.GetPath(), true) && f.Write(datos.data(), datos.size()) == datos.size())
        estado_->SetLabelText(L"Informe guardado en " + wxFileName(d.GetPath()).GetFullName() + L".");
    else
        wxMessageBox(L"No pude guardar el informe en esa carpeta. Prueba con otra.", L"Guardar informe", wxOK | wxICON_WARNING, this);
}

// ==================================================================== pestaña «Buscar soluciones»

class PanelSoluciones : public wxPanel
{
public:
    explicit PanelSoluciones(wxWindow* padre);
    void Seleccionar(const std::string& id);

private:
    void Filtrar();
    void MostrarDetalle(const soluciones::Solucion* s);

    wxSearchCtrl* buscador_ = nullptr;
    wxListBox* lista_ = nullptr;
    wxScrolledWindow* detalle_ = nullptr;
    std::unique_ptr<Ajustador> ajuste_;
    std::vector<const soluciones::Solucion*> visibles_;
};

PanelSoluciones::PanelSoluciones(wxWindow* padre) : wxPanel(padre)
{
    auto* v = new wxBoxSizer(wxVERTICAL);
    auto* intro = new wxStaticText(this, wxID_ANY, L"Escribe qué le pasa a tu PC y elige el problema en la lista.");
    v->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    buscador_ = new wxSearchCtrl(this, wxID_ANY);
    buscador_->ShowSearchButton(true);
    buscador_->ShowCancelButton(true);
    buscador_->SetDescriptiveText(L"Por ejemplo: lenta, sonido, wifi, pantalla azul");
    v->Add(buscador_, 0, wxEXPAND | wxALL, FromDIP(12));

    auto* h = new wxBoxSizer(wxHORIZONTAL);
    lista_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(300, -1)), 0, nullptr, wxLB_SINGLE);
    h->Add(lista_, 0, wxEXPAND);
    detalle_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
    detalle_->SetScrollRate(0, FromDIP(20));
    detalle_->SetMinSize(FromDIP(wxSize(200, 100)));   // ver el comentario igual en PanelRevision
    h->Add(detalle_, 1, wxEXPAND | wxLEFT, FromDIP(16));
    v->Add(h, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    SetSizer(v);
    ajuste_ = std::make_unique<Ajustador>(detalle_);

    buscador_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Filtrar(); });
    buscador_->Bind(wxEVT_SEARCH_CANCEL, [this](wxCommandEvent&) { buscador_->Clear(); });
    lista_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& e) {
        int i = e.GetSelection();
        if (i >= 0 && i < static_cast<int>(visibles_.size())) MostrarDetalle(visibles_[static_cast<size_t>(i)]);
    });
    Filtrar();
}

void PanelSoluciones::Filtrar()
{
    visibles_ = soluciones::Filtrar(buscador_->GetValue().ToStdWstring());
    lista_->Freeze();
    lista_->Clear();
    for (const auto* s : visibles_) lista_->Append(wxString(s->titulo));
    lista_->Thaw();
    if (!visibles_.empty())
    {
        lista_->SetSelection(0);
        MostrarDetalle(visibles_[0]);
    }
    else
        MostrarDetalle(nullptr);
}

void PanelSoluciones::Seleccionar(const std::string& id)
{
    buscador_->ChangeValue(wxEmptyString);
    Filtrar();
    for (size_t i = 0; i < visibles_.size(); i++)
        if (visibles_[i]->id == id)
        {
            lista_->SetSelection(static_cast<int>(i));
            lista_->EnsureVisible(static_cast<int>(i));
            MostrarDetalle(visibles_[i]);
            return;
        }
}

void PanelSoluciones::MostrarDetalle(const soluciones::Solucion* s)
{
    detalle_->Freeze();
    detalle_->DestroyChildren();
    ajuste_->Limpiar();
    auto* v = new wxBoxSizer(wxVERTICAL);
    const int restar = FromDIP(12);
    if (!s)
    {
        v->Add(ajuste_->Texto(detalle_, L"No encontré nada con esas palabras. Prueba con otras, por ejemplo «lenta», «sonido» o «internet».", restar));
    }
    else
    {
        auto* titulo = new wxStaticText(detalle_, wxID_ANY, wxEmptyString);
        titulo->SetLabelText(s->titulo);
        titulo->SetFont(titulo->GetFont().Bold().Scaled(1.35f));
        v->Add(titulo, 0, wxBOTTOM, FromDIP(6));
        auto* nota = ajuste_->Texto(detalle_, L"Prueba estos pasos en orden. Son seguros: Guara no cambia nada por ti.", restar);
        nota->SetForegroundColour(tema::TextoSuave());
        v->Add(nota, 0, wxBOTTOM, FromDIP(14));

        const int anchoNumero = FromDIP(24);
        int n = 0;
        for (const auto& paso : s->pasos)
        {
            n++;
            auto* fila = new wxBoxSizer(wxHORIZONTAL);
            auto* numero = new wxStaticText(detalle_, wxID_ANY, wxString::Format(L"%d.", n));
            numero->SetFont(numero->GetFont().Bold());
            numero->SetMinSize(wxSize(anchoNumero, -1));
            fila->Add(numero, 0);
            fila->Add(ajuste_->Texto(detalle_, paso, restar + anchoNumero), 1);
            v->Add(fila, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
        }

        if (!s->herramientas.empty())
        {
            auto* botones = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);   // sin estirar el último botón
            for (const auto& herramienta : s->herramientas)
            {
                auto* b = new wxButton(detalle_, wxID_ANY, herramienta.texto);
                std::wstring abrir = herramienta.abrir;
                b->Bind(wxEVT_BUTTON, [abrir](wxCommandEvent&) { soluciones::AbrirHerramienta(abrir); });
                botones->Add(b, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
            }
            v->Add(botones, 0, wxEXPAND | wxTOP, FromDIP(6));
        }

        if (!s->enlace.empty())
        {
            auto* enlace = new wxGenericHyperlinkCtrl(detalle_, wxID_ANY, L"Más información en la página de ayuda de Microsoft", s->enlace);
            enlace->SetNormalColour(tema::Enlace());
            enlace->SetVisitedColour(tema::Enlace());
            enlace->SetHoverColour(tema::Enlace());
            enlace->SetToolTip(s->enlace);
            enlace->Bind(wxEVT_HYPERLINK, [](wxHyperlinkEvent& e) { soluciones::AbrirEnlace(e.GetURL().ToStdWstring()); });
            v->Add(enlace, 0, wxTOP, FromDIP(8));
        }
    }
    auto* margen = new wxBoxSizer(wxVERTICAL);
    margen->Add(v, 1, wxEXPAND | wxALL, FromDIP(4));
    detalle_->SetSizer(margen);
    detalle_->Layout();
    detalle_->FitInside();
    detalle_->Scroll(0, 0);
    detalle_->Thaw();
    CallAfter([this] { ajuste_->Aplicar(true); });
}

// ==================================================================== la ventana

Ventana::Ventana() : wxFrame(nullptr, wxID_ANY, L"Guara: revisa tu PC y te dice qué hacer")
{
    SetIcons(wxIconBundle(L"AAAA_APP", reinterpret_cast<WXHINSTANCE>(GetModuleHandleW(nullptr))));

    auto* barra = new wxMenuBar();
    auto* archivo = new wxMenu();
    int idRevisar = NewControlId(), idCopiar = NewControlId(), idGuardar = NewControlId();
    archivo->Append(idRevisar, L"&Volver a revisar\tF5");
    archivo->Append(idCopiar, L"&Copiar informe");
    archivo->Append(idGuardar, L"&Guardar informe...\tCtrl+S");
    archivo->AppendSeparator();
    archivo->Append(wxID_EXIT, L"&Salir");
    barra->Append(archivo, L"&Archivo");
    barra->Append(ayuda::CrearMenu(this, L"Guara revisa tu PC sin cambiar nada y te explica en español sencillo qué tiene, "
                                         L"qué anda mal y qué hacer."),
                  L"A&yuda");
    SetMenuBar(barra);

    auto* fondo = new wxPanel(this);
    libro_ = new wxNotebook(fondo, wxID_ANY);
    revision_ = new PanelRevision(libro_, this);
    soluciones_ = new PanelSoluciones(libro_);
    libro_->AddPage(revision_, L"Revisión de la PC", true);
    libro_->AddPage(soluciones_, L"Buscar soluciones");
    auto* s = new wxBoxSizer(wxVERTICAL);
    s->Add(libro_, 1, wxEXPAND | wxALL, FromDIP(6));
    fondo->SetSizer(s);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { revision_->Revisar(); }, idRevisar);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { revision_->CopiarInforme(); }, idCopiar);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { revision_->GuardarInforme(); }, idGuardar);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_CLOSE_WINDOW, &Ventana::AlCerrar, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        RecordarTamano();
    });
    Bind(wxEVT_MOVE, [this](wxMoveEvent& e) {
        e.Skip();
        RecordarTamano();
    });

    SetMinSize(FromDIP(wxSize(720, 480)));
    ajustes::Ventana a = ajustes::Cargar();
    bool recordado = false;
    if (a.valida)
    {
        // Solo si se ve en alguna pantalla (por si se desconectó un monitor)
        wxRect r(a.x, a.y, a.ancho, a.alto);
        if (wxDisplay::GetFromPoint(wxPoint(r.x + r.width / 2, r.y + FromDIP(12))) != wxNOT_FOUND)
        {
            SetSize(r);
            recordado = true;
        }
    }
    if (!recordado)
    {
        SetSize(FromDIP(wxSize(1000, 620)));
        Centre();
    }
    normal_ = GetRect();
    if (a.valida && a.maximizada) Maximize();

    revision_->Revisar();
}

void Ventana::RecordarTamano()
{
    if (!IsMaximized() && !IsIconized() && !IsFullScreen()) normal_ = GetRect();
}

void Ventana::AlCerrar(wxCloseEvent& e)
{
    revision_->Detener();
    ajustes::Ventana a;
    a.x = normal_.x;
    a.y = normal_.y;
    a.ancho = normal_.width;
    a.alto = normal_.height;
    a.maximizada = IsMaximized();
    a.valida = true;
    ajustes::Guardar(a);
    e.Skip();
}

void Ventana::MostrarSolucion(const std::string& id)
{
    libro_->SetSelection(1);
    soluciones_->Seleccionar(id);
}
