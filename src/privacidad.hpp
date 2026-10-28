#pragma once

// Quita datos personales de los textos: la carpeta del usuario, su nombre y el nombre del equipo.

#include <string>

namespace privacidad
{
    // Lee una vez el nombre de usuario, el del equipo y la carpeta personal. Conviene llamarla desde el hilo
    // de la revisión, porque en una PC de empresa puede tardar.
    void Preparar();

    // Cambia la carpeta personal por %USERPROFILE% y las carpetas de otras cuentas por C:\Users\[usuario].
    // Se usa en todo lo que muestra la ventana.
    std::wstring LimpiarRuta(const std::wstring& texto);

    // Para el informe: además cambia el nombre de usuario por [usuario], el del equipo por [equipo], y los
    // correos, direcciones MAC e IPv4 que aparezcan por [correo], [MAC] e [IP].
    std::wstring LimpiarInforme(const std::wstring& texto);

    // Pasada final sobre el informe completo: correos, MAC, IPv4, y el nombre de usuario y del equipo como
    // palabras sueltas.
    std::wstring LimpiarFinal(const std::wstring& texto);

    // true si el texto todavía tiene el nombre de usuario o el del equipo (como palabra suelta)
    bool TieneDatosPersonales(const std::wstring& texto);
}
