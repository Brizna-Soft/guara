#include "wmi.hpp"

#include "texto.hpp"

#include <oleauto.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // wxWidgets ya inició COM en el hilo principal, y con eso la seguridad del proceso quedó puesta. En vez de
    // CoInitializeSecurity (que fallaría), se ajusta la seguridad de cada proxy para que WMI pueda actuar en
    // nombre de la persona (solo lectura, sin pedir ser administrador).
    HRESULT AjustarProxy(IUnknown* proxy)
    {
        return CoSetProxyBlanket(proxy, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    }

    class Bstr
    {
    public:
        explicit Bstr(const wchar_t* s) : b_(SysAllocString(s)) {}
        ~Bstr() { SysFreeString(b_); }
        Bstr(const Bstr&) = delete;
        Bstr& operator=(const Bstr&) = delete;
        operator BSTR() const { return b_; }

    private:
        BSTR b_;
    };

    bool Leer(IWbemClassObject* o, const wchar_t* propiedad, VARIANT& v)
    {
        VariantInit(&v);
        if (!o || FAILED(o->Get(propiedad, 0, &v, nullptr, nullptr))) return false;
        if (v.vt == VT_NULL || v.vt == VT_EMPTY)
        {
            VariantClear(&v);
            return false;
        }
        return true;
    }
}

namespace wmi
{
    Conexion::Conexion(const wchar_t* espacio, const std::atomic<bool>* cancelar) : cancelar_(cancelar)
    {
        ComPtr<IWbemLocator> localizador;
        error_ = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(localizador.GetAddressOf()));
        if (FAILED(error_)) return;

        Bstr ruta(espacio);
        error_ = localizador->ConnectServer(ruta, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr,
                                            nullptr, servicios_.ReleaseAndGetAddressOf());
        if (FAILED(error_))
        {
            servicios_.Reset();
            return;
        }
        error_ = AjustarProxy(servicios_.Get());
        if (FAILED(error_)) servicios_.Reset();
    }

    HRESULT Conexion::Consultar(const wchar_t* wql, const std::function<void(IWbemClassObject*)>& cada, long esperaMs,
                                unsigned long long totalMs)
    {
        if (!servicios_) return FAILED(error_) ? error_ : E_FAIL;
        if (cancelar_ && *cancelar_) return E_ABORT;
        const unsigned long long inicio = GetTickCount64();

        Bstr lenguaje(L"WQL");
        Bstr consulta(wql);
        ComPtr<IEnumWbemClassObject> lista;
        HRESULT hr = servicios_->ExecQuery(lenguaje, consulta, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                           nullptr, lista.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return hr;
        AjustarProxy(lista.Get());   // también es un proxy; si no lo es, el error no importa

        for (int vueltas = 0; vueltas < 2000; vueltas++)
        {
            if (cancelar_ && *cancelar_) return E_ABORT;
            if (GetTickCount64() - inicio > totalMs) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            ComPtr<IWbemClassObject> objeto;
            ULONG devueltos = 0;
            hr = lista->Next(esperaMs, 1, objeto.ReleaseAndGetAddressOf(), &devueltos);
            if (hr == static_cast<HRESULT>(WBEM_S_TIMEDOUT)) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            if (FAILED(hr)) return hr;
            if (devueltos == 0) break;   // WBEM_S_FALSE: no hay más
            cada(objeto.Get());
        }
        return S_OK;
    }

    std::wstring Texto(IWbemClassObject* o, const wchar_t* propiedad)
    {
        VARIANT v;
        if (!Leer(o, propiedad, v)) return {};
        std::wstring r;
        if (v.vt == VT_BSTR)
        {
            if (v.bstrVal) r.assign(v.bstrVal, SysStringLen(v.bstrVal));
        }
        else
        {
            VARIANT c;
            VariantInit(&c);
            if (SUCCEEDED(VariantChangeType(&c, &v, 0, VT_BSTR)) && c.bstrVal) r.assign(c.bstrVal, SysStringLen(c.bstrVal));
            VariantClear(&c);
        }
        VariantClear(&v);
        return texto::Recortar(r);
    }

    std::optional<long long> Entero(IWbemClassObject* o, const wchar_t* propiedad)
    {
        VARIANT v;
        if (!Leer(o, propiedad, v)) return std::nullopt;
        std::optional<long long> r;
        switch (v.vt)
        {
        case VT_I1: r = v.cVal; break;
        case VT_UI1: r = v.bVal; break;
        case VT_I2: r = v.iVal; break;
        case VT_UI2: r = v.uiVal; break;
        case VT_I4: r = v.lVal; break;
        case VT_UI4: r = static_cast<long long>(v.ulVal); break;
        case VT_INT: r = v.intVal; break;
        case VT_UINT: r = static_cast<long long>(v.uintVal); break;
        case VT_I8: r = v.llVal; break;
        case VT_UI8: r = static_cast<long long>(v.ullVal); break;
        case VT_BOOL: r = v.boolVal != VARIANT_FALSE ? 1 : 0; break;
        case VT_BSTR:
            // WMI entrega los números de 64 bits como texto
            if (v.bstrVal && *v.bstrVal)
            {
                wchar_t* fin = nullptr;
                unsigned long long x = wcstoull(v.bstrVal, &fin, 10);
                if (fin != v.bstrVal) r = static_cast<long long>(x);
            }
            break;
        default: break;
        }
        VariantClear(&v);
        return r;
    }

    std::optional<bool> Booleano(IWbemClassObject* o, const wchar_t* propiedad)
    {
        VARIANT v;
        if (!Leer(o, propiedad, v)) return std::nullopt;
        std::optional<bool> r;
        if (v.vt == VT_BOOL) r = v.boolVal != VARIANT_FALSE;
        VariantClear(&v);
        return r;
    }

    std::wstring ExplicarError(HRESULT hr)
    {
        switch (hr)
        {
        case static_cast<HRESULT>(WBEM_E_ACCESS_DENIED):
        case E_ACCESSDENIED:
            return L"Windows no dio permiso para leerlo.";
        case static_cast<HRESULT>(WBEM_E_INVALID_NAMESPACE):
        case static_cast<HRESULT>(WBEM_E_INVALID_CLASS):
        case static_cast<HRESULT>(WBEM_E_NOT_SUPPORTED):
        case static_cast<HRESULT>(WBEM_E_NOT_FOUND):
        case static_cast<HRESULT>(WBEM_E_INVALID_QUERY):
            return L"Esta PC o esta versión de Windows no lo informa.";
        case static_cast<HRESULT>(0x800705B4L):   // HRESULT_FROM_WIN32(ERROR_TIMEOUT)
            return L"Windows tardó demasiado en responder (se agotó el tiempo).";
        case E_ABORT:
            return L"Se canceló la revisión.";
        default:
            break;
        }
        return texto::Formato(L"Windows respondió con el error 0x%08X.", static_cast<unsigned>(hr));
    }
}
