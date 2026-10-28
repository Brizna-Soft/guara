#include "revision.hpp"

#include "privacidad.hpp"
#include "texto.hpp"
#include "wmi.hpp"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winevt.h>
#include <winioctl.h>
#include <wrl/client.h>

#include <algorithm>
#include <bit>
#include <cwctype>
#include <map>
#include <optional>
#include <vector>

using Microsoft::WRL::ComPtr;
using texto::Formato;

namespace
{
    const wchar_t* ClaveVersion = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const HRESULT NoEncontrado = static_cast<HRESULT>(WBEM_E_NOT_FOUND);

    // ---------------------------------------------------------------- ayudas

    std::wstring RegistroTexto(HKEY raiz, const wchar_t* clave, const wchar_t* valor)
    {
        wchar_t b[2048] = {};
        DWORD n = sizeof(b);
        if (RegGetValueW(raiz, clave, valor, RRF_RT_REG_SZ, nullptr, b, &n) == ERROR_SUCCESS) return texto::Recortar(b);
        return {};
    }

    std::optional<DWORD> RegistroNumero(HKEY raiz, const wchar_t* clave, const wchar_t* valor)
    {
        DWORD v = 0, n = sizeof(v);
        if (RegGetValueW(raiz, clave, valor, RRF_RT_REG_DWORD, nullptr, &v, &n) == ERROR_SUCCESS) return v;
        return std::nullopt;
    }

    Aviso NuevoAviso(Estado e, std::wstring t, std::string solucion = {})
    {
        Aviso a;
        a.estado = e;
        a.texto = std::move(t);
        a.solucion = std::move(solucion);
        return a;
    }

    // El estado de la sección es el peor de sus avisos
    void Cerrar(Seccion& s)
    {
        Estado e = Estado::Bien;
        for (const auto& a : s.avisos) e = Peor(e, a.estado);
        s.estado = e;
    }

    Seccion SeccionNoDisponible(const char* id, const wchar_t* titulo, const std::wstring& motivo)
    {
        Seccion s;
        s.id = id;
        s.titulo = titulo;
        s.avisos.push_back(NuevoAviso(Estado::NoDisponible, L"No disponible: " + motivo));
        s.estado = Estado::NoDisponible;
        return s;
    }

    bool EmpiezaCon(const std::wstring& s, const wchar_t* prefijo)
    {
        size_t n = wcslen(prefijo);
        return s.size() >= n && CompareStringOrdinal(s.c_str(), static_cast<int>(n), prefijo, static_cast<int>(n), TRUE) == CSTR_EQUAL;
    }

    bool Igual(const std::wstring& a, const wchar_t* b)
    {
        return CompareStringOrdinal(a.c_str(), -1, b, -1, TRUE) == CSTR_EQUAL;
    }

    unsigned long long Numero(const FILETIME& f)
    {
        return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    }

    SYSTEMTIME Local(const FILETIME& utc)
    {
        SYSTEMTIME st = {}, local = {};
        FileTimeToSystemTime(&utc, &st);
        if (!SystemTimeToTzSpecificLocalTime(nullptr, &st, &local)) return st;
        return local;
    }

    bool TieneBateria()
    {
        SYSTEM_POWER_STATUS p = {};
        return GetSystemPowerStatus(&p) && !(p.BatteryFlag & 128) && p.BatteryFlag != 255;
    }

    // ---------------------------------------------------------------- Sistema

    bool FechaQfe(const std::wstring& s, SYSTEMTIME& st)
    {
        st = {};
        int a = 0, b = 0, c = 0;
        auto digitos = [](const std::wstring& x) {
            return !x.empty() && std::all_of(x.begin(), x.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
        };
        if (swscanf_s(s.c_str(), L"%d/%d/%d", &a, &b, &c) == 3)
        {
            // Windows la da como mes/día/año
            st.wMonth = static_cast<WORD>(a);
            st.wDay = static_cast<WORD>(b);
            st.wYear = static_cast<WORD>(c);
        }
        else if (s.size() == 8 && digitos(s))
        {
            st.wYear = static_cast<WORD>(_wtoi(s.substr(0, 4).c_str()));
            st.wMonth = static_cast<WORD>(_wtoi(s.substr(4, 2).c_str()));
            st.wDay = static_cast<WORD>(_wtoi(s.substr(6, 2).c_str()));
        }
        else if (s.size() == 16)
        {
            // En Windows viejos viene como FILETIME en hexadecimal
            wchar_t* fin = nullptr;
            unsigned long long x = wcstoull(s.c_str(), &fin, 16);
            if (!fin || *fin) return false;
            FILETIME ft;
            ft.dwLowDateTime = static_cast<DWORD>(x);
            ft.dwHighDateTime = static_cast<DWORD>(x >> 32);
            st = Local(ft);
        }
        else
            return false;
        return st.wYear >= 2000 && st.wYear <= 2100 && st.wMonth >= 1 && st.wMonth <= 12 && st.wDay >= 1 && st.wDay <= 31;
    }

    int Comparar(const SYSTEMTIME& a, const SYSTEMTIME& b)
    {
        if (a.wYear != b.wYear) return a.wYear < b.wYear ? -1 : 1;
        if (a.wMonth != b.wMonth) return a.wMonth < b.wMonth ? -1 : 1;
        if (a.wDay != b.wDay) return a.wDay < b.wDay ? -1 : 1;
        return 0;
    }

    Seccion RevisarSistema(wmi::Conexion& cimv2)
    {
        Seccion s;
        s.id = "sistema";
        s.titulo = L"Sistema";

        std::wstring caption;
        if (cimv2.Ok())
            cimv2.Consultar(L"SELECT Caption FROM Win32_OperatingSystem",
                            [&](IWbemClassObject* o) { caption = wmi::Texto(o, L"Caption"); });
        std::wstring version = RegistroTexto(HKEY_LOCAL_MACHINE, ClaveVersion, L"DisplayVersion");
        if (version.empty()) version = RegistroTexto(HKEY_LOCAL_MACHINE, ClaveVersion, L"ReleaseId");
        std::wstring compilacion = RegistroTexto(HKEY_LOCAL_MACHINE, ClaveVersion, L"CurrentBuild");
        auto ubr = RegistroNumero(HKEY_LOCAL_MACHINE, ClaveVersion, L"UBR");
        int numero = _wtoi(compilacion.c_str());

        std::wstring edicion = caption;
        if (EmpiezaCon(edicion, L"Microsoft ")) edicion = edicion.substr(10);
        if (edicion.empty())
        {
            edicion = RegistroTexto(HKEY_LOCAL_MACHINE, ClaveVersion, L"ProductName");
            // El registro dice «Windows 10» también en Windows 11
            if (numero >= 22000 && EmpiezaCon(edicion, L"Windows 10")) edicion = L"Windows 11" + edicion.substr(10);
        }
        if (edicion.empty()) edicion = L"Windows";

        std::wstring windows = edicion;
        if (!version.empty()) windows += L", versión " + version;
        if (!compilacion.empty())
            windows += L" (compilación " + compilacion + (ubr ? L"." + std::to_wstring(*ubr) : std::wstring()) + L")";
        s.datos.push_back({ L"Windows", windows });
        s.resumen = edicion + (version.empty() ? std::wstring() : L" " + version);

        s.datos.push_back({ L"Tipo de PC", TieneBateria() ? L"Laptop (tiene batería)" : L"De escritorio (sin batería)" });

        unsigned long long segundos = GetTickCount64() / 1000;
        s.datos.push_back({ L"Encendida desde hace", texto::Duracion(segundos) });

        SYSTEMTIME ultima = {};
        bool hay = false;
        HRESULT hr = cimv2.Ok() ? cimv2.Consultar(L"SELECT InstalledOn FROM Win32_QuickFixEngineering",
                                                  [&](IWbemClassObject* o) {
                                                      SYSTEMTIME st;
                                                      if (FechaQfe(wmi::Texto(o, L"InstalledOn"), st) && (!hay || Comparar(st, ultima) > 0))
                                                      {
                                                          ultima = st;
                                                          hay = true;
                                                      }
                                                  })
                                : cimv2.Error();
        long long dias = hay ? texto::DiasDesde(ultima) : -1;
        if (hay)
            s.datos.push_back({ L"Última actualización", texto::Fecha(ultima) + L" (" + texto::Hace(dias) + L")" });
        else
            s.datos.push_back({ L"Última actualización",
                                FAILED(hr) ? L"No disponible (" + wmi::ExplicarError(hr) + L")" : std::wstring(L"No disponible") });

        if (numero >= 10240 && numero < 22000)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                L"Windows 10 dejó de recibir actualizaciones de seguridad el 14 de octubre de 2025. Si tu PC lo permite, pasa a Windows 11.",
                "windows-10"));
        if (hay)
        {
            if (dias > 120)
                s.avisos.push_back(NuevoAviso(Estado::Problema,
                    Formato(L"La última actualización se instaló hace %lld días. Abre Windows Update y busca actualizaciones.", dias),
                    "windows-update"));
            else if (dias > 60)
                s.avisos.push_back(NuevoAviso(Estado::Revisa,
                    Formato(L"La última actualización se instaló hace %lld días. Busca actualizaciones en Windows Update.", dias),
                    "windows-update"));
            else
                s.avisos.push_back(NuevoAviso(Estado::Bien, L"Windows tiene actualizaciones recientes."));
        }
        long long diasEncendida = static_cast<long long>(segundos / 86400);
        if (diasEncendida >= 7)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                Formato(L"Hace %lld días que Windows no se reinicia. Usa «Reiniciar» de vez en cuando: «Apagar» no siempre limpia todo.", diasEncendida),
                "pc-lenta"));
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Procesador

    Seccion RevisarProcesador()
    {
        Seccion s;
        s.id = "procesador";
        s.titulo = L"Procesador";

        const wchar_t* clave = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
        std::wstring nombre = RegistroTexto(HKEY_LOCAL_MACHINE, clave, L"ProcessorNameString");
        auto mhz = RegistroNumero(HKEY_LOCAL_MACHINE, clave, L"~MHz");
        if (nombre.empty()) return SeccionNoDisponible("procesador", L"Procesador", L"Windows no informa el modelo.");

        int nucleos = 0, hilos = 0;
        DWORD largo = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &largo);
        if (largo > 0)
        {
            std::vector<BYTE> buf(largo);
            auto* primero = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data());
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, primero, &largo))
            {
                for (DWORD i = 0; i < largo;)
                {
                    auto* p = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + i);
                    if (p->Size == 0) break;
                    if (p->Relationship == RelationProcessorCore)
                    {
                        nucleos++;
                        for (WORD g = 0; g < p->Processor.GroupCount; g++)
                            hilos += std::popcount(static_cast<unsigned long long>(p->Processor.GroupMask[g].Mask));
                    }
                    i += p->Size;
                }
            }
        }
        if (hilos == 0) hilos = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

        s.datos.push_back({ L"Modelo", nombre });
        if (nucleos > 0) s.datos.push_back({ L"Núcleos", std::to_wstring(nucleos) });
        if (hilos > 0) s.datos.push_back({ L"Hilos", std::to_wstring(hilos) });
        if (mhz && *mhz > 0) s.datos.push_back({ L"Velocidad base", texto::Decimal(*mhz / 1000.0, 1) + L" GHz" });
        s.resumen = nombre;

        // Uso en este momento: dos lecturas con un segundo de diferencia
        FILETIME i1, k1, u1, i2, k2, u2;
        if (GetSystemTimes(&i1, &k1, &u1))
        {
            Sleep(1000);
            if (GetSystemTimes(&i2, &k2, &u2))
            {
                unsigned long long libre = Numero(i2) - Numero(i1);
                unsigned long long total = (Numero(k2) - Numero(k1)) + (Numero(u2) - Numero(u1));   // el kernel incluye el tiempo libre
                if (total > 0)
                {
                    int uso = static_cast<int>(100 - (libre * 100 / total));
                    uso = std::clamp(uso, 0, 100);
                    s.datos.push_back({ L"Uso ahora", Formato(L"%d %%", uso) });
                    if (uso >= 90)
                        s.avisos.push_back(NuevoAviso(Estado::Revisa,
                            Formato(L"El procesador está muy ocupado (%d %%). Mira en el Administrador de tareas qué programa lo usa.", uso),
                            "pc-lenta"));
                    else
                        s.avisos.push_back(NuevoAviso(Estado::Bien, L"El procesador no está sobrecargado."));
                }
            }
        }
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Memoria

    std::wstring TipoMemoria(long long t)
    {
        switch (t)
        {
        case 20: return L"DDR";
        case 21: return L"DDR2";
        case 24: return L"DDR3";
        case 26: return L"DDR4";
        case 27: return L"LPDDR";
        case 28: return L"LPDDR2";
        case 29: return L"LPDDR3";
        case 30: return L"LPDDR4";
        case 34: return L"DDR5";
        case 35: return L"LPDDR5";
        default: return {};
        }
    }

    std::wstring GigasEnteros(unsigned long long bytes)
    {
        const unsigned long long giga = 1024ULL * 1024ULL * 1024ULL;
        if (bytes >= giga && bytes % giga == 0) return Formato(L"%llu GB", bytes / giga);
        return texto::Bytes(bytes);
    }

    Seccion RevisarMemoria(wmi::Conexion& cimv2)
    {
        Seccion s;
        s.id = "memoria";
        s.titulo = L"Memoria";

        ULONGLONG instaladaKb = 0;
        GetPhysicallyInstalledSystemMemory(&instaladaKb);
        MEMORYSTATUSEX ms = {};
        ms.dwLength = sizeof(ms);
        bool hayEstado = GlobalMemoryStatusEx(&ms) != FALSE;

        struct Modulo
        {
            unsigned long long capacidad = 0;
            long long velocidad = 0;
            long long tipo = 0;
        };
        std::vector<Modulo> modulos;
        HRESULT hr = cimv2.Ok()
            ? cimv2.Consultar(L"SELECT Capacity, Speed, ConfiguredClockSpeed, SMBIOSMemoryType FROM Win32_PhysicalMemory",
                              [&](IWbemClassObject* o) {
                                  Modulo m;
                                  m.capacidad = static_cast<unsigned long long>(wmi::Entero(o, L"Capacity").value_or(0));
                                  long long configurada = wmi::Entero(o, L"ConfiguredClockSpeed").value_or(0);
                                  m.velocidad = configurada > 0 ? configurada : wmi::Entero(o, L"Speed").value_or(0);
                                  m.tipo = wmi::Entero(o, L"SMBIOSMemoryType").value_or(0);
                                  if (m.capacidad > 0) modulos.push_back(m);
                              })
            : cimv2.Error();

        unsigned long long total = instaladaKb ? instaladaKb * 1024ULL : (hayEstado ? ms.ullTotalPhys : 0);
        if (total == 0) return SeccionNoDisponible("memoria", L"Memoria", L"Windows no informa cuánta memoria hay.");

        std::wstring tipo = modulos.empty() ? std::wstring() : TipoMemoria(modulos[0].tipo);
        std::wstring totalTexto = GigasEnteros(total);
        s.datos.push_back({ L"Total", totalTexto + (tipo.empty() ? std::wstring() : L" (" + tipo + L")") });
        if (hayEstado)
        {
            unsigned long long usada = ms.ullTotalPhys - ms.ullAvailPhys;
            s.datos.push_back({ L"En uso", texto::Bytes(usada) + L" de " + texto::Bytes(ms.ullTotalPhys) +
                                               Formato(L" (%lu %%)", ms.dwMemoryLoad) });
        }
        if (!modulos.empty())
        {
            // Agrupa los módulos iguales: «2 × 16 GB a 6000 MHz»
            std::map<std::pair<unsigned long long, long long>, int> grupos;
            for (const auto& m : modulos) grupos[{ m.capacidad, m.velocidad }]++;
            std::wstring t;
            for (const auto& [clave, n] : grupos)
            {
                if (!t.empty()) t += L" + ";
                t += Formato(L"%d × ", n) + GigasEnteros(clave.first);
                if (clave.second > 0) t += Formato(L" a %lld MHz", clave.second);
            }
            s.datos.push_back({ L"Módulos", t });
        }
        else if (FAILED(hr))
            s.datos.push_back({ L"Módulos", L"No disponible (" + wmi::ExplicarError(hr) + L")" });

        if (hayEstado && ms.dwMemoryLoad >= 90)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                Formato(L"La memoria está casi llena (%lu %%). Cierra los programas que no uses o reinicia la PC.", ms.dwMemoryLoad),
                "pc-lenta"));
        if (total <= 4ULL * 1024ULL * 1024ULL * 1024ULL)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                L"Con 4 GB o menos, Windows va lento. Si tu PC lo permite, agrega más memoria.", "pc-lenta"));
        if (s.avisos.empty()) s.avisos.push_back(NuevoAviso(Estado::Bien, L"Hay memoria libre suficiente."));
        s.resumen = totalTexto + L" de memoria";
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Discos

    struct DiscoFisico
    {
        std::wstring id, nombre;
        long long tipoMedio = 0, bus = 0, salud = -1;
        unsigned long long tamano = 0;
    };

    struct Unidad
    {
        wchar_t letra = L'C';
        unsigned long long libre = 0, total = 0;
        int disco = -1;
    };

    // Número del disco físico donde está una unidad (solo consulta, sin abrir el disco para leer)
    int NumeroDeDisco(wchar_t letra)
    {
        wchar_t ruta[] = L"\\\\.\\C:";
        ruta[4] = letra;
        HANDLE h = CreateFileW(ruta, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return -1;
        alignas(8) BYTE buf[sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT)] = {};
        DWORD n = 0;
        int r = -1;
        if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf, sizeof(buf), &n, nullptr))
        {
            auto* v = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf);
            if (v->NumberOfDiskExtents >= 1) r = static_cast<int>(v->Extents[0].DiskNumber);
        }
        CloseHandle(h);
        return r;
    }

    std::wstring TipoDisco(const DiscoFisico& d)
    {
        if (d.bus == 17) return L"SSD NVMe";
        if (d.bus == 7) return d.tipoMedio == 4 ? L"SSD por USB" : L"Disco USB";
        if (d.tipoMedio == 4) return L"SSD";
        if (d.tipoMedio == 3) return L"HDD (disco mecánico)";
        if (d.bus == 14 || d.bus == 15) return L"Disco virtual";
        if (d.bus == 12 || d.bus == 13) return L"Tarjeta de memoria";
        return L"Disco";
    }

    std::wstring SaludDisco(long long h)
    {
        switch (h)
        {
        case 0: return L"buena";
        case 1: return L"con advertencias";
        case 2: return L"mala";
        default: return L"desconocida";
        }
    }

    Seccion RevisarDiscos()
    {
        Seccion s;
        s.id = "discos";
        s.titulo = L"Discos";

        std::vector<DiscoFisico> discos;
        wmi::Conexion almacenamiento(L"ROOT\\Microsoft\\Windows\\Storage");
        HRESULT hr = almacenamiento.Ok()
            ? almacenamiento.Consultar(L"SELECT DeviceId, FriendlyName, MediaType, BusType, HealthStatus, Size FROM MSFT_PhysicalDisk",
                                       [&](IWbemClassObject* o) {
                                           DiscoFisico d;
                                           d.id = wmi::Texto(o, L"DeviceId");
                                           d.nombre = wmi::Texto(o, L"FriendlyName");
                                           d.tipoMedio = wmi::Entero(o, L"MediaType").value_or(0);
                                           d.bus = wmi::Entero(o, L"BusType").value_or(0);
                                           d.salud = wmi::Entero(o, L"HealthStatus").value_or(-1);
                                           d.tamano = static_cast<unsigned long long>(wmi::Entero(o, L"Size").value_or(0));
                                           discos.push_back(d);
                                       })
            : almacenamiento.Error();
        std::sort(discos.begin(), discos.end(),
                  [](const DiscoFisico& a, const DiscoFisico& b) { return _wtoi(a.id.c_str()) < _wtoi(b.id.c_str()); });

        std::vector<Unidad> unidades;
        DWORD mascara = GetLogicalDrives();
        for (int i = 0; i < 26; i++)
        {
            if (!(mascara & (1u << i))) continue;
            wchar_t raiz[] = L"A:\\";
            raiz[0] = static_cast<wchar_t>(L'A' + i);
            if (GetDriveTypeW(raiz) != DRIVE_FIXED) continue;
            ULARGE_INTEGER libre = {}, total = {};
            if (!GetDiskFreeSpaceExW(raiz, &libre, &total, nullptr) || total.QuadPart == 0) continue;
            Unidad u;
            u.letra = raiz[0];
            u.libre = libre.QuadPart;
            u.total = total.QuadPart;
            u.disco = NumeroDeDisco(raiz[0]);
            unidades.push_back(u);
        }
        if (discos.empty() && unidades.empty())
            return SeccionNoDisponible("discos", L"Discos", wmi::ExplicarError(FAILED(hr) ? hr : NoEncontrado));

        wchar_t carpetaWindows[MAX_PATH] = L"C:\\";
        GetSystemWindowsDirectoryW(carpetaWindows, MAX_PATH);
        wchar_t letraWindows = static_cast<wchar_t>(towupper(carpetaWindows[0]));

        for (const auto& d : discos)
        {
            std::wstring letras;
            int cuantas = 0;
            for (const auto& u : unidades)
                if (u.disco >= 0 && std::to_wstring(u.disco) == d.id)
                {
                    letras += (letras.empty() ? L"" : L", ") + std::wstring(1, u.letra) + L":";
                    cuantas++;
                }
            std::wstring valor = TipoDisco(d) + L", " + texto::Bytes(d.tamano) + L", salud " + SaludDisco(d.salud);
            if (cuantas == 1) valor += L" (unidad " + letras + L")";
            if (cuantas > 1) valor += L" (unidades " + letras + L")";
            std::wstring nombre = d.nombre.empty() ? L"Disco " + d.id : d.nombre;
            s.datos.push_back({ nombre, valor });

            if (d.salud == 1)
                s.avisos.push_back(NuevoAviso(Estado::Revisa,
                    L"El disco «" + nombre + L"» avisa de un posible problema. Haz una copia de tus archivos importantes.", "disco-falla"));
            else if (d.salud == 2)
                s.avisos.push_back(NuevoAviso(Estado::Problema,
                    L"El disco «" + nombre + L"» está fallando. Copia tus archivos importantes cuanto antes y cámbialo.", "disco-falla"));

            for (const auto& u : unidades)
                if (u.letra == letraWindows && std::to_wstring(u.disco) == d.id)
                {
                    s.resumen = L"Windows en " + TipoDisco(d);
                    if (d.tipoMedio == 3)
                        s.avisos.push_back(NuevoAviso(Estado::Revisa,
                            L"Windows está en un disco mecánico (HDD), que es lento. Cambiarlo por un SSD es la mejora que más se nota.",
                            "pc-lenta"));
                }
        }
        if (discos.empty())
            s.datos.push_back({ L"Salud de los discos", L"No disponible (" + wmi::ExplicarError(FAILED(hr) ? hr : NoEncontrado) + L")" });

        for (const auto& u : unidades)
        {
            double porcentaje = 100.0 * static_cast<double>(u.libre) / static_cast<double>(u.total);
            s.datos.push_back({ L"Unidad " + std::wstring(1, u.letra) + L":",
                                texto::Bytes(u.libre) + L" libres de " + texto::Bytes(u.total) + Formato(L" (%.0f %%)", porcentaje) });
            if (porcentaje < 5.0)
                s.avisos.push_back(NuevoAviso(Estado::Problema,
                    Formato(L"La unidad %c: casi no tiene espacio (quedan %s). Windows puede fallar al actualizarse. Libera espacio.",
                            u.letra, texto::Bytes(u.libre).c_str()),
                    "disco-lleno"));
            else if (porcentaje < 10.0)
                s.avisos.push_back(NuevoAviso(Estado::Revisa,
                    Formato(L"La unidad %c: tiene menos del 10 %% libre (quedan %s). Libera espacio para que Windows funcione bien.",
                            u.letra, texto::Bytes(u.libre).c_str()),
                    "disco-lleno"));
        }
        if (s.avisos.empty()) s.avisos.push_back(NuevoAviso(Estado::Bien, L"Los discos están sanos y tienen espacio libre."));
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Dispositivos

    std::wstring SignificadoCodigo(long long c)
    {
        switch (c)
        {
        case 1: return L"No está bien configurado.";
        case 3: return L"El controlador puede estar dañado, o falta memoria.";
        case 9: return L"Windows no reconoce este aparato.";
        case 10: return L"No puede arrancar.";
        case 12: return L"No encuentra recursos libres para funcionar.";
        case 14: return L"Hay que reiniciar la PC para que funcione.";
        case 16: return L"Windows no reconoce todos los recursos que usa.";
        case 18: return L"Hay que volver a instalar su controlador.";
        case 19: return L"Su configuración está incompleta o dañada.";
        case 21: return L"Windows lo está quitando.";
        case 22: return L"Está desactivado.";
        case 24: return L"No está presente, no funciona bien o le faltan controladores.";
        case 28: return L"No tiene su controlador instalado.";
        case 29: return L"Está desactivado porque el firmware de la PC no le dio recursos.";
        case 31: return L"Windows no puede cargar su controlador.";
        case 32: return L"Su controlador está desactivado.";
        case 33: return L"Windows no sabe qué recursos necesita.";
        case 34: return L"Windows no puede saber cómo configurarlo.";
        case 35: return L"Al firmware de la PC le falta información para usarlo.";
        case 36: return L"Tiene un conflicto de interrupciones (IRQ).";
        case 37: return L"Windows no puede iniciar su controlador.";
        case 38: return L"No carga porque otra copia de su controlador sigue en memoria.";
        case 39: return L"Falta su controlador o está dañado.";
        case 40: return L"Falta información de su servicio en Windows.";
        case 41: return L"Windows cargó el controlador, pero no encuentra el aparato.";
        case 42: return L"Hay un aparato duplicado.";
        case 43: return L"Windows lo detuvo porque informó problemas.";
        case 44: return L"Un programa o un servicio lo apagó.";
        case 46: return L"Windows no puede usarlo porque se está apagando.";
        case 47: return L"Está preparado para quitarlo con seguridad.";
        case 48: return L"Su controlador se bloqueó porque tiene problemas conocidos con Windows.";
        case 49: return L"Windows no puede iniciar aparatos nuevos (su registro está demasiado grande).";
        case 50: return L"Windows no puede aplicar todas sus propiedades.";
        case 51: return L"Está esperando a otro aparato.";
        case 52: return L"Windows no puede comprobar la firma digital de su controlador.";
        case 53: return L"Está reservado por el depurador de Windows.";
        case 54: return L"Falló y se está reiniciando.";
        case 56: return L"Windows todavía lo está configurando.";
        default: return Formato(L"Tiene el error número %lld.", c);
        }
    }

    // Nombre para el informe: los aparatos Bluetooth, los teléfonos y las impresoras de red pueden llevar el
    // nombre de su dueño o del equipo, así que en el informe solo va el tipo de aparato.
    std::wstring NombreParaInforme(const std::wstring& id, const std::wstring& nombre)
    {
        if (EmpiezaCon(id, L"BTH")) return L"Aparato Bluetooth";
        if (EmpiezaCon(id, L"SWD\\PRINTENUM") || EmpiezaCon(id, L"WSDPRINT")) return L"Impresora";
        if (EmpiezaCon(id, L"SWD\\WPDBUSENUM")) return L"Teléfono o aparato portátil";
        if (EmpiezaCon(id, L"SWD\\")) return L"Aparato de software";
        return nombre;
    }

    Seccion RevisarDispositivos(wmi::Conexion& cimv2)
    {
        Seccion s;
        s.id = "dispositivos";
        s.titulo = L"Dispositivos con problemas";
        s.tituloLista = L"Dispositivos";

        int conProblema = 0, desactivados = 0;
        HRESULT hr = cimv2.Ok()
            ? cimv2.Consultar(L"SELECT Name, ConfigManagerErrorCode, DeviceID FROM Win32_PnPEntity WHERE ConfigManagerErrorCode <> 0",
                              [&](IWbemClassObject* o) {
                                  long long codigo = wmi::Entero(o, L"ConfigManagerErrorCode").value_or(0);
                                  if (codigo == 0 || codigo == 45) return;   // 45: no está conectado ahora, es normal
                                  std::wstring nombre = wmi::Texto(o, L"Name");
                                  std::wstring id = wmi::Texto(o, L"DeviceID");
                                  if (nombre.empty()) nombre = L"Aparato desconocido";
                                  std::wstring detalle = L": " + SignificadoCodigo(codigo) + Formato(L" (código %lld)", codigo);
                                  Elemento e;
                                  e.texto = nombre + detalle;
                                  e.textoInforme = NombreParaInforme(id, nombre) + detalle;
                                  s.lista.push_back(e);
                                  if (codigo == 22) desactivados++;
                                  else conProblema++;
                              })
            : cimv2.Error();
        if (FAILED(hr)) return SeccionNoDisponible("dispositivos", L"Dispositivos con problemas", wmi::ExplicarError(hr));

        s.datos.push_back({ L"Con problemas", conProblema == 0 ? std::wstring(L"Ninguno") : std::to_wstring(conProblema) });
        if (desactivados > 0) s.datos.push_back({ L"Desactivados", std::to_wstring(desactivados) });
        if (conProblema > 0)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                (conProblema == 1 ? std::wstring(L"1 dispositivo tiene un problema.") : Formato(L"%d dispositivos tienen problemas.", conProblema)) +
                    L" Abre el Administrador de dispositivos y actualiza su controlador.",
                "dispositivo-error"));
        else
            s.avisos.push_back(NuevoAviso(Estado::Bien, L"Todos los dispositivos funcionan sin errores."));
        s.acciones.push_back({ L"Abrir el Administrador de dispositivos", L"devmgmt.msc" });
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Tarjeta gráfica

    Seccion RevisarGrafica(wmi::Conexion& cimv2)
    {
        Seccion s;
        s.id = "grafica";
        s.titulo = L"Tarjeta gráfica";

        struct Tarjeta
        {
            std::wstring nombre, version, inf, pnp;
            SYSTEMTIME fecha = {};
            bool hayFecha = false;
            long long ancho = 0, alto = 0, hz = 0, error = 0;
        };
        std::vector<Tarjeta> tarjetas;
        HRESULT hr = cimv2.Ok()
            ? cimv2.Consultar(L"SELECT Name, DriverVersion, DriverDate, PNPDeviceID, InfFilename, CurrentHorizontalResolution, "
                              L"CurrentVerticalResolution, CurrentRefreshRate, ConfigManagerErrorCode FROM Win32_VideoController",
                              [&](IWbemClassObject* o) {
                                  Tarjeta t;
                                  t.nombre = wmi::Texto(o, L"Name");
                                  t.version = wmi::Texto(o, L"DriverVersion");
                                  t.inf = wmi::Texto(o, L"InfFilename");
                                  t.pnp = wmi::Texto(o, L"PNPDeviceID");
                                  std::wstring f = wmi::Texto(o, L"DriverDate");   // «20261017000000.000000-000»
                                  if (f.size() >= 8)
                                  {
                                      t.fecha.wYear = static_cast<WORD>(_wtoi(f.substr(0, 4).c_str()));
                                      t.fecha.wMonth = static_cast<WORD>(_wtoi(f.substr(4, 2).c_str()));
                                      t.fecha.wDay = static_cast<WORD>(_wtoi(f.substr(6, 2).c_str()));
                                      t.hayFecha = t.fecha.wYear >= 1990 && t.fecha.wMonth >= 1 && t.fecha.wMonth <= 12 && t.fecha.wDay >= 1;
                                  }
                                  t.ancho = wmi::Entero(o, L"CurrentHorizontalResolution").value_or(0);
                                  t.alto = wmi::Entero(o, L"CurrentVerticalResolution").value_or(0);
                                  t.hz = wmi::Entero(o, L"CurrentRefreshRate").value_or(0);
                                  t.error = wmi::Entero(o, L"ConfigManagerErrorCode").value_or(0);
                                  if (t.nombre.empty()) t.nombre = L"Tarjeta gráfica";
                                  tarjetas.push_back(t);
                              })
            : cimv2.Error();
        if (tarjetas.empty())
            return SeccionNoDisponible("grafica", L"Tarjeta gráfica", wmi::ExplicarError(FAILED(hr) ? hr : NoEncontrado));

        // Se muestran las tarjetas de verdad (PCI); las pantallas virtuales (escritorio remoto, etc.) no cuentan
        bool hayPci = std::any_of(tarjetas.begin(), tarjetas.end(), [](const Tarjeta& t) { return EmpiezaCon(t.pnp, L"PCI\\"); });
        std::vector<const Tarjeta*> elegidas;
        for (const auto& t : tarjetas)
            if (!hayPci || EmpiezaCon(t.pnp, L"PCI\\")) elegidas.push_back(&t);

        int n = 0;
        for (const Tarjeta* t : elegidas)
        {
            n++;
            std::wstring sufijo = elegidas.size() > 1 ? L" " + std::to_wstring(n) : std::wstring();
            s.datos.push_back({ L"Tarjeta" + sufijo, t->nombre });
            std::wstring controlador = t->version.empty() ? std::wstring() : L"versión " + t->version;
            if (t->hayFecha) controlador += (controlador.empty() ? L"" : L", ") + std::wstring(L"del ") + texto::Fecha(t->fecha);
            if (!controlador.empty()) s.datos.push_back({ L"Controlador" + sufijo, controlador });
            if (t->ancho > 0 && t->alto > 0)
                s.datos.push_back({ L"Pantalla" + sufijo,
                                    Formato(L"%lld × %lld", t->ancho, t->alto) + (t->hz > 1 ? Formato(L" a %lld Hz", t->hz) : std::wstring()) });

            if (Igual(t->inf, L"display.inf"))
                s.avisos.push_back(NuevoAviso(Estado::Problema,
                    L"Windows usa un controlador básico para «" + t->nombre + L"». Instala el de AMD, NVIDIA o Intel para que funcione bien.",
                    "controlador-video"));
            else if (t->error != 0)
                s.avisos.push_back(NuevoAviso(Estado::Problema, L"«" + t->nombre + L"» tiene un problema: " + SignificadoCodigo(t->error),
                                              "controlador-video"));
            else if (t->hayFecha && texto::DiasDesde(t->fecha) > 730)
                s.avisos.push_back(NuevoAviso(Estado::Revisa,
                    L"El controlador de «" + t->nombre + L"» tiene más de 2 años. Busca uno más nuevo en la página del fabricante.",
                    "controlador-video"));
            if (s.resumen.empty()) s.resumen = t->nombre;
        }
        if (s.avisos.empty()) s.avisos.push_back(NuevoAviso(Estado::Bien, L"La tarjeta gráfica tiene su controlador instalado."));
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Batería

    Seccion RevisarBateria()
    {
        Seccion s;
        s.id = "bateria";
        s.titulo = L"Batería";

        SYSTEM_POWER_STATUS p = {};
        if (GetSystemPowerStatus(&p) && p.BatteryLifePercent <= 100)
            s.datos.push_back({ L"Carga", Formato(L"%u %%", static_cast<unsigned>(p.BatteryLifePercent)) +
                                              (p.ACLineStatus == 1 ? L", conectada a la corriente" : L", usando la batería") });

        std::map<std::wstring, long long> diseno, completa;
        wmi::Conexion w(L"ROOT\\WMI");
        HRESULT hr1 = w.Ok() ? w.Consultar(L"SELECT InstanceName, DesignedCapacity FROM BatteryStaticData",
                                           [&](IWbemClassObject* o) { diseno[wmi::Texto(o, L"InstanceName")] = wmi::Entero(o, L"DesignedCapacity").value_or(0); })
                             : w.Error();
        HRESULT hr2 = w.Ok() ? w.Consultar(L"SELECT InstanceName, FullChargedCapacity FROM BatteryFullChargedCapacity",
                                           [&](IWbemClassObject* o) { completa[wmi::Texto(o, L"InstanceName")] = wmi::Entero(o, L"FullChargedCapacity").value_or(0); })
                             : w.Error();

        int n = 0;
        for (const auto& [id, disenada] : diseno)
        {
            auto it = completa.find(id);
            if (it == completa.end() || disenada <= 0 || it->second <= 0) continue;
            n++;
            double salud = std::min(100.0, 100.0 * static_cast<double>(it->second) / static_cast<double>(disenada));
            std::wstring sufijo = diseno.size() > 1 ? L" " + std::to_wstring(n) : std::wstring();
            s.datos.push_back({ L"Salud" + sufijo, Formato(L"%.0f %% de su capacidad original", salud) });
            s.datos.push_back({ L"Capacidad" + sufijo, Formato(L"%lld de %lld mWh", it->second, disenada) });
            if (salud < 50.0)
                s.avisos.push_back(NuevoAviso(Estado::Problema,
                    Formato(L"La batería ya perdió más de la mitad de su capacidad (le queda el %.0f %%). Conviene cambiarla.", salud), "bateria"));
            else if (salud < 80.0)
                s.avisos.push_back(NuevoAviso(Estado::Revisa,
                    Formato(L"La batería está gastada (le queda el %.0f %% de su capacidad). Dura menos que cuando era nueva.", salud), "bateria"));
            else
                s.avisos.push_back(NuevoAviso(Estado::Bien, L"La batería está en buen estado."));
        }
        if (n == 0)
        {
            HRESULT hr = FAILED(hr1) ? hr1 : (FAILED(hr2) ? hr2 : NoEncontrado);
            s.datos.push_back({ L"Salud", L"No disponible (" + wmi::ExplicarError(hr) + L")" });
        }
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Seguridad

    Seccion RevisarSeguridad()
    {
        Seccion s;
        s.id = "seguridad";
        s.titulo = L"Seguridad";

        struct Antivirus
        {
            std::wstring nombre;
            bool activo = false, alDia = true;
        };
        std::vector<Antivirus> antivirus;
        wmi::Conexion centro(L"ROOT\\SecurityCenter2");
        HRESULT hrCentro = centro.Ok()
            ? centro.Consultar(L"SELECT displayName, productState FROM AntiVirusProduct",
                               [&](IWbemClassObject* o) {
                                   long long estado = wmi::Entero(o, L"productState").value_or(0);
                                   Antivirus a;
                                   a.nombre = wmi::Texto(o, L"displayName");
                                   a.activo = (estado & 0x1000) != 0;   // examen activado
                                   a.alDia = (estado & 0x10) == 0;      // firmas al día
                                   if (a.nombre.empty()) a.nombre = L"Antivirus";
                                   antivirus.push_back(a);
                               })
            : centro.Error();

        bool hayDefender = false, defenderPrincipal = false;
        std::optional<bool> tiempoReal;
        long long edadFirmas = -1;
        wmi::Conexion defender(L"ROOT\\Microsoft\\Windows\\Defender");
        HRESULT hrDefender = defender.Ok()
            ? defender.Consultar(L"SELECT * FROM MSFT_MpComputerStatus",
                                 [&](IWbemClassObject* o) {
                                     hayDefender = true;
                                     std::wstring modo = wmi::Texto(o, L"AMRunningMode");
                                     auto servicio = wmi::Booleano(o, L"AMServiceEnabled");
                                     auto activado = wmi::Booleano(o, L"AntivirusEnabled");
                                     tiempoReal = wmi::Booleano(o, L"RealTimeProtectionEnabled");
                                     edadFirmas = wmi::Entero(o, L"AntivirusSignatureAge").value_or(-1);
                                     defenderPrincipal = modo.empty() ? (activado.value_or(false) && servicio.value_or(false))
                                                                      : Igual(modo, L"Normal");
                                 })
            : defender.Error();

        if (antivirus.empty() && !hayDefender)
            return SeccionNoDisponible("seguridad", L"Seguridad", wmi::ExplicarError(FAILED(hrCentro) ? hrCentro : hrDefender));

        std::wstring lista;
        for (const auto& a : antivirus)
        {
            if (!lista.empty()) lista += L"; ";
            lista += a.nombre + (a.activo ? L" (activo" : L" (apagado") + (a.alDia ? L", al día)" : L", desactualizado)");
        }
        if (!antivirus.empty()) s.datos.push_back({ L"Antivirus", lista });
        else if (FAILED(hrCentro)) s.datos.push_back({ L"Antivirus", L"No disponible (" + wmi::ExplicarError(hrCentro) + L")" });
        else s.datos.push_back({ L"Antivirus", L"Ninguno registrado" });

        bool firmasValidas = edadFirmas >= 0 && edadFirmas < 65535;
        if (hayDefender)
        {
            std::wstring d;
            if (!defenderPrincipal)
                d = L"En espera: otro antivirus está a cargo";
            else
            {
                d = tiempoReal.value_or(false) ? L"Protección en tiempo real activada" : L"Protección en tiempo real apagada";
                if (firmasValidas) d += L", firmas de " + texto::Hace(edadFirmas);
            }
            s.datos.push_back({ L"Microsoft Defender", d });
        }

        bool algunoActivo = std::any_of(antivirus.begin(), antivirus.end(), [](const Antivirus& a) { return a.activo; }) ||
                            (defenderPrincipal && tiempoReal.value_or(false));
        if (!algunoActivo)
            s.avisos.push_back(NuevoAviso(Estado::Problema,
                L"No hay ningún antivirus activo. Abre Seguridad de Windows y activa la protección.", "proteccion"));
        else
        {
            for (const auto& a : antivirus)
                if (a.activo && !a.alDia)
                    s.avisos.push_back(NuevoAviso(Estado::Revisa,
                        L"Las firmas de «" + a.nombre + L"» están viejas. Actualízalas desde el antivirus.", "proteccion"));
            if (defenderPrincipal && tiempoReal.has_value() && !*tiempoReal)
                s.avisos.push_back(NuevoAviso(Estado::Problema,
                    L"La protección en tiempo real de Microsoft Defender está apagada. Actívala en Seguridad de Windows.", "proteccion"));
            if (defenderPrincipal && firmasValidas && edadFirmas > 7)
                s.avisos.push_back(NuevoAviso(Estado::Revisa,
                    Formato(L"Las firmas de Microsoft Defender tienen %lld días. Búscalas en Seguridad de Windows.", edadFirmas), "proteccion"));
            if (s.avisos.empty()) s.avisos.push_back(NuevoAviso(Estado::Bien, L"El antivirus está activo y al día."));
        }
        s.acciones.push_back({ L"Abrir Seguridad de Windows", L"windowsdefender:" });
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Apagados y pantallas azules

    struct Evento
    {
        FILETIME cuando = {};
        unsigned long codigo = 0;
        bool pantallaAzul = false;
    };

    bool Dibujar(EVT_HANDLE contexto, EVT_HANDLE evento, std::vector<BYTE>& buf, DWORD& cuantos)
    {
        DWORD usado = 0;
        if (EvtRender(contexto, evento, EvtRenderEventValues, static_cast<DWORD>(buf.size()), buf.data(), &usado, &cuantos)) return true;
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
        buf.resize(usado);
        return EvtRender(contexto, evento, EvtRenderEventValues, static_cast<DWORD>(buf.size()), buf.data(), &usado, &cuantos) != FALSE;
    }

    // Lee eventos del registro System con una consulta XPath. «dato» es el valor que trae el código de la
    // pantalla azul. Devuelve 0 si pudo leer, o el código de error de Windows.
    DWORD LeerEventos(const wchar_t* xpath, const wchar_t* dato, bool esBugCheck, std::vector<Evento>& salida)
    {
        EVT_HANDLE consulta = EvtQuery(nullptr, L"System", xpath, EvtQueryChannelPath | EvtQueryReverseDirection);
        if (!consulta) return GetLastError();
        const wchar_t* rutas[] = { dato };
        EVT_HANDLE contextoSistema = EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem);
        EVT_HANDLE contextoDato = EvtCreateRenderContext(1, rutas, EvtRenderContextValues);
        DWORD error = ERROR_SUCCESS;
        std::vector<BYTE> buf(16384);
        for (int vueltas = 0; vueltas < 100; vueltas++)
        {
            EVT_HANDLE eventos[32];
            DWORD n = 0;
            if (!EvtNext(consulta, 32, eventos, 10000, 0, &n))
            {
                DWORD e = GetLastError();
                if (e != ERROR_NO_MORE_ITEMS) error = e;
                break;
            }
            for (DWORD i = 0; i < n; i++)
            {
                Evento ev;
                DWORD cuantos = 0;
                if (contextoSistema && Dibujar(contextoSistema, eventos[i], buf, cuantos) && cuantos > static_cast<DWORD>(EvtSystemTimeCreated))
                {
                    auto* v = reinterpret_cast<PEVT_VARIANT>(buf.data());
                    if (v[EvtSystemTimeCreated].Type == EvtVarTypeFileTime)
                    {
                        ULARGE_INTEGER u;
                        u.QuadPart = v[EvtSystemTimeCreated].FileTimeVal;
                        ev.cuando.dwLowDateTime = u.LowPart;
                        ev.cuando.dwHighDateTime = u.HighPart;
                    }
                }
                if (contextoDato && Dibujar(contextoDato, eventos[i], buf, cuantos) && cuantos >= 1)
                {
                    auto* v = reinterpret_cast<PEVT_VARIANT>(buf.data());
                    if (v[0].Type == EvtVarTypeUInt32)
                        ev.codigo = v[0].UInt32Val;
                    else if (v[0].Type == EvtVarTypeString && v[0].StringVal)
                        ev.codigo = wcstoul(v[0].StringVal, nullptr, 16);   // «0x00000133 (...)»
                }
                ev.pantallaAzul = esBugCheck || ev.codigo != 0;
                salida.push_back(ev);
                EvtClose(eventos[i]);
            }
        }
        if (contextoDato) EvtClose(contextoDato);
        if (contextoSistema) EvtClose(contextoSistema);
        EvtClose(consulta);
        return error;
    }

    struct CodigoParada
    {
        unsigned long codigo;
        const wchar_t* nombre;
        const wchar_t* pista;
    };

    const CodigoParada CodigosParada[] = {
        { 0x0A, L"IRQL_NOT_LESS_OR_EQUAL", L"un controlador usó memoria que no debía" },
        { 0x19, L"BAD_POOL_HEADER", L"un controlador dañó la memoria" },
        { 0x1A, L"MEMORY_MANAGEMENT", L"un problema con la memoria RAM o con un controlador" },
        { 0x1E, L"KMODE_EXCEPTION_NOT_HANDLED", L"un controlador falló" },
        { 0x24, L"NTFS_FILE_SYSTEM", L"un problema al leer o escribir el disco" },
        { 0x3B, L"SYSTEM_SERVICE_EXCEPTION", L"un controlador o un servicio de Windows falló" },
        { 0x50, L"PAGE_FAULT_IN_NONPAGED_AREA", L"un problema con la memoria RAM o con un controlador" },
        { 0x7A, L"KERNEL_DATA_INPAGE_ERROR", L"Windows no pudo leer datos del disco" },
        { 0x7B, L"INACCESSIBLE_BOOT_DEVICE", L"Windows no pudo leer el disco de arranque" },
        { 0x7E, L"SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", L"un controlador falló" },
        { 0x7F, L"UNEXPECTED_KERNEL_MODE_TRAP", L"un fallo del hardware o de un controlador" },
        { 0x9F, L"DRIVER_POWER_STATE_FAILURE", L"un controlador falló al suspender o al despertar la PC" },
        { 0xA0, L"INTERNAL_POWER_ERROR", L"un problema con la administración de energía" },
        { 0xBE, L"ATTEMPTED_WRITE_TO_READONLY_MEMORY", L"un controlador falló" },
        { 0xC2, L"BAD_POOL_CALLER", L"un controlador usó mal la memoria" },
        { 0xD1, L"DRIVER_IRQL_NOT_LESS_OR_EQUAL", L"un controlador usó memoria que no debía" },
        { 0xEF, L"CRITICAL_PROCESS_DIED", L"un proceso esencial de Windows se cerró" },
        { 0xF4, L"CRITICAL_OBJECT_TERMINATION", L"un proceso esencial se cerró; a veces es el disco" },
        { 0x101, L"CLOCK_WATCHDOG_TIMEOUT", L"el procesador dejó de responder" },
        { 0x109, L"CRITICAL_STRUCTURE_CORRUPTION", L"un controlador o la memoria dañó datos de Windows" },
        { 0x116, L"VIDEO_TDR_FAILURE", L"el controlador de la tarjeta gráfica dejó de responder" },
        { 0x117, L"VIDEO_TDR_TIMEOUT_DETECTED", L"el controlador de la tarjeta gráfica dejó de responder" },
        { 0x119, L"VIDEO_SCHEDULER_INTERNAL_ERROR", L"un problema con la tarjeta gráfica o su controlador" },
        { 0x124, L"WHEA_UNCORRECTABLE_ERROR", L"un error del hardware: a menudo calor, la fuente de poder o el procesador" },
        { 0x133, L"DPC_WATCHDOG_VIOLATION", L"un controlador tardó demasiado en responder, a menudo el del disco o el del chipset" },
        { 0x139, L"KERNEL_SECURITY_CHECK_FAILURE", L"un controlador o la memoria dañó datos" },
        { 0x154, L"UNEXPECTED_STORE_EXCEPTION", L"un problema con el disco o con la memoria" },
    };

    // «0x00000133 (DPC_WATCHDOG_VIOLATION)»
    std::wstring CodigoTexto(unsigned long c)
    {
        std::wstring t = Formato(L"0x%08lX", c);
        for (const auto& p : CodigosParada)
            if (p.codigo == c) return t + L" (" + p.nombre + L")";
        return t;
    }

    const wchar_t* PistaCodigo(unsigned long c)
    {
        for (const auto& p : CodigosParada)
            if (p.codigo == c) return p.pista;
        return nullptr;
    }

    Seccion RevisarApagados()
    {
        Seccion s;
        s.id = "apagados";
        s.titulo = L"Apagados inesperados y pantallas azules";
        s.tituloLista = L"Últimos 30 días";

        std::vector<Evento> energia, bugcheck;
        // 2592000000 ms = 30 días
        DWORD e1 = LeerEventos(
            L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and (EventID=41) and TimeCreated[timediff(@SystemTime) <= 2592000000]]]",
            L"Event/EventData/Data[@Name='BugcheckCode']", false, energia);
        DWORD e2 = LeerEventos(
            L"*[System[Provider[@Name='Microsoft-Windows-WER-SystemErrorReporting' or @Name='BugCheck'] and (EventID=1001) and "
            L"TimeCreated[timediff(@SystemTime) <= 2592000000]]]",
            L"Event/EventData/Data[@Name='param1']", true, bugcheck);
        if (e1 != ERROR_SUCCESS && e2 != ERROR_SUCCESS)
            return SeccionNoDisponible("apagados", L"Apagados inesperados y pantallas azules",
                                       Formato(L"Windows no dejó leer su registro de eventos (error %lu).", e1));

        // Cada pantalla azul deja un BugCheck 1001 y, al arrancar de nuevo, un Kernel-Power 41 con el mismo
        // código. Se cuenta una sola vez; si no quedó el 1001, cuenta el 41.
        const unsigned long long media = 30ULL * 60ULL * 10000000ULL;   // media hora, en unidades de 100 ns
        std::vector<Evento> todos = bugcheck;
        for (const auto& k : energia)
        {
            if (k.codigo != 0)
            {
                bool yaEsta = std::any_of(bugcheck.begin(), bugcheck.end(), [&](const Evento& b) {
                    unsigned long long x = Numero(b.cuando), y = Numero(k.cuando);
                    return (x > y ? x - y : y - x) <= media;
                });
                if (yaEsta) continue;
            }
            todos.push_back(k);
        }
        std::sort(todos.begin(), todos.end(), [](const Evento& a, const Evento& b) { return Numero(a.cuando) > Numero(b.cuando); });

        int azules = 0, apagados = 0;
        std::map<unsigned long, int> codigos;
        const Evento* ultimaAzul = nullptr;
        const Evento* ultimoApagado = nullptr;
        for (const auto& e : todos)
        {
            if (e.pantallaAzul)
            {
                azules++;
                if (!ultimaAzul) ultimaAzul = &e;
                if (e.codigo) codigos[e.codigo]++;
            }
            else
            {
                apagados++;
                if (!ultimoApagado) ultimoApagado = &e;
            }
        }

        s.datos.push_back({ L"Apagados inesperados",
                            apagados == 0 ? std::wstring(L"Ninguno")
                                          : Formato(L"%d (el último, %s)", apagados, texto::FechaHora(Local(ultimoApagado->cuando)).c_str()) });
        s.datos.push_back({ L"Pantallas azules",
                            azules == 0 ? std::wstring(L"Ninguna")
                                        : Formato(L"%d (la última, %s)", azules, texto::FechaHora(Local(ultimaAzul->cuando)).c_str()) });

        int mostrados = 0;
        for (const auto& e : todos)
        {
            if (++mostrados > 15) break;
            Elemento el;
            el.texto = texto::FechaHora(Local(e.cuando)) + L": ";
            if (e.pantallaAzul)
                el.texto += e.codigo ? L"pantalla azul, código " + CodigoTexto(e.codigo) : std::wstring(L"pantalla azul");
            else
                el.texto += L"se apagó sin cerrar Windows";
            s.lista.push_back(el);
        }

        // El código que más se repite, con una pista en español
        std::wstring masComun;
        if (!codigos.empty())
        {
            auto it = std::max_element(codigos.begin(), codigos.end(),
                                       [](const auto& a, const auto& b) { return a.second < b.second; });
            masComun = (azules > 1 ? L" (el código más común es " : L" (código ") + CodigoTexto(it->first);
            if (const wchar_t* pista = PistaCodigo(it->first)) masComun += std::wstring(L": ") + pista;
            masComun += L")";
        }
        if (azules >= 2)
            s.avisos.push_back(NuevoAviso(Estado::Problema,
                Formato(L"Windows tuvo %d pantallas azules en 30 días", azules) + masComun + L". Sigue los pasos de la solución.",
                "pantalla-azul"));
        else if (azules == 1)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                L"Windows tuvo 1 pantalla azul en 30 días" + masComun + L". Si se repite, sigue los pasos de la solución.", "pantalla-azul"));
        if (apagados >= 3)
            s.avisos.push_back(NuevoAviso(Estado::Problema,
                Formato(L"La PC se apagó %d veces sin cerrar Windows. Puede ser la corriente, el calor o que se colgó.", apagados),
                "se-reinicia-sola"));
        else if (apagados >= 1)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                (apagados == 1 ? std::wstring(L"La PC se apagó 1 vez sin cerrar Windows.") : Formato(L"La PC se apagó %d veces sin cerrar Windows.", apagados)) +
                    L" Puede ser un corte de luz o que la apagaste con el botón.",
                "se-reinicia-sola"));
        if (s.avisos.empty())
            s.avisos.push_back(NuevoAviso(Estado::Bien, L"No hubo apagados inesperados ni pantallas azules en los últimos 30 días."));
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- Programas que arrancan con Windows

    struct Arranque
    {
        std::wstring nombre, programa;
        bool desactivado = false;
    };

    // Task Manager guarda en StartupApproved si un programa está desactivado: el primer byte es par (02, 06) si
    // está activado e impar (03, 07) si está desactivado. Solo se lee.
    bool Desactivado(HKEY raiz, const wchar_t* subclave, const std::wstring& nombre)
    {
        BYTE datos[64] = {};
        DWORD n = sizeof(datos);
        std::wstring clave = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\") + subclave;
        if (RegGetValueW(raiz, clave.c_str(), nombre.c_str(), RRF_RT_REG_BINARY, nullptr, datos, &n) != ERROR_SUCCESS || n == 0) return false;
        return (datos[0] & 1) != 0;
    }

    std::wstring Expandir(const std::wstring& s)
    {
        wchar_t b[4096];
        DWORD n = ExpandEnvironmentStringsW(s.c_str(), b, _countof(b));
        return (n > 0 && n <= _countof(b)) ? std::wstring(b) : s;
    }

    // De la línea de comandos, solo el programa (sin argumentos, que podrían tener datos personales)
    std::wstring ProgramaDeComando(const std::wstring& comando)
    {
        std::wstring c = texto::Recortar(comando);
        std::wstring r;
        if (!c.empty() && c[0] == L'"')
        {
            size_t fin = c.find(L'"', 1);
            r = c.substr(1, fin == std::wstring::npos ? std::wstring::npos : fin - 1);
        }
        else
        {
            size_t p = texto::Normalizar(c).find(L".exe");
            r = p != std::wstring::npos ? c.substr(0, p + 4) : c.substr(0, c.find(L' '));
        }
        return privacidad::LimpiarRuta(Expandir(r));
    }

    void LeerRun(HKEY raiz, REGSAM vista, const wchar_t* aprobado, std::vector<Arranque>& lista)
    {
        HKEY k = nullptr;
        if (RegOpenKeyExW(raiz, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ | vista, &k) != ERROR_SUCCESS) return;
        std::vector<wchar_t> nombre(16384);
        for (DWORD i = 0; i < 1000; i++)
        {
            DWORD largoNombre = static_cast<DWORD>(nombre.size());
            DWORD tipo = 0, largoDatos = 0;
            LONG r = RegEnumValueW(k, i, nombre.data(), &largoNombre, nullptr, &tipo, nullptr, &largoDatos);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS || (tipo != REG_SZ && tipo != REG_EXPAND_SZ)) continue;
            std::vector<wchar_t> datos(largoDatos / sizeof(wchar_t) + 2, L'\0');
            DWORD largo = static_cast<DWORD>(datos.size() * sizeof(wchar_t));
            largoNombre = static_cast<DWORD>(nombre.size());
            if (RegEnumValueW(k, i, nombre.data(), &largoNombre, nullptr, &tipo, reinterpret_cast<BYTE*>(datos.data()), &largo) != ERROR_SUCCESS)
                continue;
            Arranque a;
            a.nombre.assign(nombre.data(), largoNombre);
            a.programa = ProgramaDeComando(datos.data());
            a.desactivado = Desactivado(raiz, aprobado, a.nombre);
            lista.push_back(a);
        }
        RegCloseKey(k);
    }

    std::wstring DestinoDeAcceso(const std::wstring& lnk)
    {
        ComPtr<IShellLinkW> enlace;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(enlace.GetAddressOf())))) return {};
        ComPtr<IPersistFile> archivo;
        if (FAILED(enlace.As(&archivo)) || FAILED(archivo->Load(lnk.c_str(), STGM_READ))) return {};
        wchar_t destino[MAX_PATH] = {};
        if (enlace->GetPath(destino, MAX_PATH, nullptr, SLGP_RAWPATH) != S_OK) return {};
        return privacidad::LimpiarRuta(Expandir(destino));
    }

    void LeerCarpeta(REFKNOWNFOLDERID carpeta, HKEY raizAprobado, std::vector<Arranque>& lista)
    {
        PWSTR ruta = nullptr;
        HRESULT hr = SHGetKnownFolderPath(carpeta, 0, nullptr, &ruta);
        std::wstring dir = SUCCEEDED(hr) && ruta ? ruta : L"";
        CoTaskMemFree(ruta);
        if (dir.empty()) return;

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring archivo = fd.cFileName;
            if (Igual(archivo, L"desktop.ini")) continue;
            Arranque a;
            a.nombre = archivo;
            size_t punto = archivo.rfind(L'.');
            std::wstring extension = punto == std::wstring::npos ? std::wstring() : texto::Normalizar(archivo.substr(punto));
            if (extension == L".lnk")
            {
                a.nombre = archivo.substr(0, punto);
                a.programa = DestinoDeAcceso(dir + L"\\" + archivo);
            }
            else
                a.programa = privacidad::LimpiarRuta(dir + L"\\" + archivo);
            a.desactivado = Desactivado(raizAprobado, L"StartupFolder", archivo);
            lista.push_back(a);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    Seccion RevisarInicio()
    {
        Seccion s;
        s.id = "inicio";
        s.titulo = L"Programas que arrancan con Windows";
        s.tituloLista = L"Programas";

        std::vector<Arranque> lista;
        LeerRun(HKEY_CURRENT_USER, 0, L"Run", lista);
        LeerRun(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"Run", lista);
        LeerRun(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"Run32", lista);
        LeerCarpeta(FOLDERID_Startup, HKEY_CURRENT_USER, lista);
        LeerCarpeta(FOLDERID_CommonStartup, HKEY_LOCAL_MACHINE, lista);

        std::stable_sort(lista.begin(), lista.end(), [](const Arranque& a, const Arranque& b) {
            if (a.desactivado != b.desactivado) return !a.desactivado;
            return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.nombre.c_str(), -1, b.nombre.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
        });

        int activos = 0;
        for (const auto& a : lista)
        {
            if (!a.desactivado) activos++;
            Elemento e;
            e.texto = a.nombre + (a.programa.empty() ? std::wstring() : L": " + a.programa) + (a.desactivado ? L" (desactivado)" : L"");
            s.lista.push_back(e);
        }
        s.datos.push_back({ L"Activos al encender", Formato(L"%d de %d", activos, static_cast<int>(lista.size())) });
        if (activos > 10)
            s.avisos.push_back(NuevoAviso(Estado::Revisa,
                Formato(L"Arrancan %d programas con Windows. Desactiva los que no uses para que encienda más rápido.", activos),
                "arranque-lento"));
        else
            s.avisos.push_back(NuevoAviso(Estado::Bien, activos == 0 ? L"Ningún programa arranca solo con Windows."
                                                                     : L"Pocos programas arrancan solos con Windows."));
        s.acciones.push_back({ L"Abrir el Administrador de tareas", L"taskmgr-inicio" });
        Cerrar(s);
        return s;
    }

    // ---------------------------------------------------------------- todo junto

    template <class F>
    Seccion Intentar(const char* id, const wchar_t* titulo, F&& f)
    {
        try
        {
            return f();
        }
        catch (...)
        {
            return SeccionNoDisponible(id, titulo, L"algo falló al leerlo.");
        }
    }

    Resultado RevisarTodo(const std::function<void(const std::wstring&)>& progreso)
    {
        auto paso = [&](const wchar_t* t) {
            if (progreso) progreso(t);
        };
        Resultado r;

        // El procesador primero: así su uso no incluye el trabajo de las consultas que vienen después
        paso(L"Revisando el procesador...");
        Seccion procesador = Intentar("procesador", L"Procesador", [] { return RevisarProcesador(); });

        paso(L"Revisando Windows...");
        wmi::Conexion cimv2(L"ROOT\\CIMV2");
        Seccion sistema = Intentar("sistema", L"Sistema", [&] { return RevisarSistema(cimv2); });
        paso(L"Revisando la memoria...");
        Seccion memoria = Intentar("memoria", L"Memoria", [&] { return RevisarMemoria(cimv2); });
        paso(L"Revisando los discos...");
        Seccion discos = Intentar("discos", L"Discos", [] { return RevisarDiscos(); });
        paso(L"Revisando la tarjeta gráfica...");
        Seccion grafica = Intentar("grafica", L"Tarjeta gráfica", [&] { return RevisarGrafica(cimv2); });
        bool bateria = TieneBateria();
        Seccion seccionBateria;
        if (bateria)
        {
            paso(L"Revisando la batería...");
            seccionBateria = Intentar("bateria", L"Batería", [] { return RevisarBateria(); });
        }
        paso(L"Buscando dispositivos con problemas...");
        Seccion dispositivos = Intentar("dispositivos", L"Dispositivos con problemas", [&] { return RevisarDispositivos(cimv2); });
        paso(L"Revisando el antivirus...");
        Seccion seguridad = Intentar("seguridad", L"Seguridad", [] { return RevisarSeguridad(); });
        paso(L"Leyendo el registro de eventos...");
        Seccion apagados = Intentar("apagados", L"Apagados inesperados y pantallas azules", [] { return RevisarApagados(); });
        paso(L"Revisando los programas de inicio...");
        Seccion inicio = Intentar("inicio", L"Programas que arrancan con Windows", [] { return RevisarInicio(); });

        r.secciones = { sistema, procesador, memoria, discos, grafica };
        if (bateria) r.secciones.push_back(seccionBateria);
        r.secciones.push_back(dispositivos);
        r.secciones.push_back(seguridad);
        r.secciones.push_back(apagados);
        r.secciones.push_back(inicio);

        for (const char* id : { "sistema", "procesador", "memoria", "grafica", "discos" })
            for (const auto& s : r.secciones)
                if (s.id == id && !s.resumen.empty())
                    r.resumenEquipo += (r.resumenEquipo.empty() ? L"" : L" · ") + s.resumen;

        SYSTEMTIME ahora;
        GetLocalTime(&ahora);
        r.fecha = texto::Fecha(ahora) + Formato(L" a las %02u:%02u", ahora.wHour, ahora.wMinute);
        return r;
    }
}

namespace revision
{
    Resultado Revisar(const std::function<void(const std::wstring&)>& progreso)
    {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        privacidad::Preparar();
        Resultado r = RevisarTodo(progreso);   // las conexiones de WMI se cierran antes de CoUninitialize
        if (SUCCEEDED(com)) CoUninitialize();
        return r;
    }
}
