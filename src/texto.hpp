#pragma once

// Ayudas para escribir números, fechas y tiempos en español sencillo.

#include <windows.h>

#include <string>

namespace texto
{
    std::wstring Formato(const wchar_t* formato, ...);

    // Con el separador decimal que usa la persona en Windows: «31.1» o «31,1»
    std::wstring Decimal(double valor, int decimales);

    // Tamaños como los muestra el Explorador de Windows: «931 GB», «1.82 TB», «31.1 GB»
    std::wstring Bytes(unsigned long long bytes);

    // «28/10/2026» y «28/10/2026 00:41»
    std::wstring Fecha(const SYSTEMTIME& st);
    std::wstring FechaHora(const SYSTEMTIME& st);

    // «2 días y 3 horas», «5 horas y 12 minutos», «40 minutos»
    std::wstring Duracion(unsigned long long segundos);

    // «1 día» / «3 días»
    std::wstring Plural(long long n, const wchar_t* uno, const wchar_t* varios);

    // «hoy», «ayer», «hace 12 días»
    std::wstring Hace(long long dias);

    // Quita espacios al principio y al final, y deja uno solo entre palabras
    std::wstring Recortar(const std::wstring& s);

    // Minúsculas y sin tildes, para buscar sin importar cómo se escriba
    std::wstring Normalizar(const std::wstring& s);

    // Días enteros desde una fecha (hora local) hasta hoy; negativo si la fecha es futura
    long long DiasDesde(const SYSTEMTIME& fechaLocal);

    // UTF-8 <-> UTF-16
    std::wstring DesdeUtf8(const std::string& s);
    std::string AUtf8(const std::wstring& s);
}
