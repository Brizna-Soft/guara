#pragma once

// Lo que encuentra la revisión: secciones con datos, avisos con semáforo y listas.
// Lo arma el hilo de la revisión (revision.cpp) y lo muestran la ventana y el informe.

#include <string>
#include <vector>

// Semáforo. El orden importa: un número más alto es más grave.
enum class Estado
{
    Bien = 0,          // verde: «Bien»
    NoDisponible = 1,  // gris: «No disponible» (Windows no lo informa o no dio permiso)
    Revisa = 2,        // amarillo: «Revisa esto»
    Problema = 3,      // rojo: «Problema»
};

struct Aviso
{
    Estado estado = Estado::Bien;
    std::wstring texto;     // qué significa y qué hacer, en una línea
    std::string solucion;   // id de la solución que lo explica (vacío si no hay)
};

struct Dato
{
    std::wstring etiqueta;
    std::wstring valor;
};

struct Elemento
{
    std::wstring texto;          // lo que se ve en la ventana
    std::wstring textoInforme;   // lo que va al informe (vacío: lo mismo que «texto»)
};

struct Accion
{
    std::wstring texto;   // texto del botón
    std::wstring abrir;   // herramienta de Windows (ver soluciones::AbrirHerramienta)
};

struct Seccion
{
    std::string id;
    std::wstring titulo;
    Estado estado = Estado::Bien;
    std::wstring resumen;   // una frase corta para la línea del equipo en el resumen
    std::vector<Dato> datos;
    std::vector<Aviso> avisos;
    std::wstring tituloLista;
    std::vector<Elemento> lista;
    bool listaEnInforme = true;   // false: el informe dice solo cuántos hay (por ejemplo, programas de inicio)
    std::vector<Accion> acciones;
};

struct Resultado
{
    std::vector<Seccion> secciones;
    std::wstring fecha;           // «28/10/2026 a las 00:41»
    std::wstring resumenEquipo;   // «Windows 11 Pro · AMD Ryzen 7 · 32 GB de memoria · ...»
};

Estado Peor(Estado a, Estado b);

// El estado general: rojo si hay algún problema, amarillo si hay algo para revisar, verde si no.
Estado EstadoGeneral(const Resultado& r);

// «Tu PC está bien», «Hay 2 cosas para revisar», «Hay 1 problema y 1 cosa para revisar»...
std::wstring TituloResumen(const Resultado& r);

// «Bien», «Revisa esto», «Problema», «No disponible»
std::wstring NombreEstado(Estado e);
