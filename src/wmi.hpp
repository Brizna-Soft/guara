#pragma once

// Consultas de solo lectura a WMI (Instrumental de administración de Windows) por COM.
// Se usan en el hilo de la revisión, que inicia COM por su cuenta (COINIT_MULTITHREADED).

#include <windows.h>
#include <objbase.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace wmi
{
    class Conexion
    {
    public:
        // espacio: por ejemplo L"ROOT\\CIMV2" o L"ROOT\\Microsoft\\Windows\\Storage".
        // Si «cancelar» se activa, las consultas se cortan y devuelven E_ABORT.
        explicit Conexion(const wchar_t* espacio, const std::atomic<bool>* cancelar = nullptr);

        bool Ok() const { return servicios_ != nullptr; }
        HRESULT Error() const { return error_; }

        // Ejecuta la consulta WQL y llama a «cada» con cada objeto. Devuelve S_OK o el error. Cada objeto puede
        // tardar hasta «esperaMs» y toda la consulta hasta «totalMs»; si no, devuelve el error de tiempo agotado.
        HRESULT Consultar(const wchar_t* wql, const std::function<void(IWbemClassObject*)>& cada, long esperaMs = 10000,
                          unsigned long long totalMs = 30000);

    private:
        Microsoft::WRL::ComPtr<IWbemServices> servicios_;
        HRESULT error_ = S_OK;
        const std::atomic<bool>* cancelar_ = nullptr;
    };

    std::wstring Texto(IWbemClassObject* o, const wchar_t* propiedad);
    std::optional<long long> Entero(IWbemClassObject* o, const wchar_t* propiedad);
    std::optional<bool> Booleano(IWbemClassObject* o, const wchar_t* propiedad);

    // Explica el error en español sencillo, para mostrarlo junto a «No disponible»
    std::wstring ExplicarError(HRESULT hr);
}
