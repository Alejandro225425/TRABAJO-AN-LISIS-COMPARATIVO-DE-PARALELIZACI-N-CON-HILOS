// Multiplicacion de matrices SECUENCIAL (1 hilo, 1 core) en C++
// VERSION CON INTERFAZ GRAFICA (Win32 API)
//
// Compilar con MSVC:  cl /O2 /EHsc MMS.cpp /link psapi.lib user32.lib gdi32.lib
// Compilar con g++:   g++ -O2 -std=c++17 -o MMS.exe MMS.cpp -lpsapi -lgdi32 -luser32 -mwindows

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

using Matrix = std::vector<std::vector<int>>;
static constexpr int SEED = 42;

// ===================== Infraestructura GUI =====================

#define IDC_ROWS  101
#define IDC_COLS  102
#define IDC_COLB  103
#define IDC_RUN   104
#define IDC_CLR   105
#define IDC_OUT   106
#define IDT_TMR   1
#define WM_DONE   (WM_USER + 1)

static HWND  g_hWnd  = NULL;
static HWND  g_hOut  = NULL;
static HWND  g_hRun  = NULL;
static HWND  g_hRows = NULL;
static HWND  g_hCols = NULL;
static HWND  g_hColB = NULL;
static HFONT g_fMono = NULL;
static HFONT g_fUI   = NULL;

static std::mutex  g_mx;
static std::string g_ob;

class GuiBuf : public std::streambuf {
protected:
    int overflow(int c) override {
        if (c != EOF) { std::lock_guard<std::mutex> l(g_mx); g_ob += (char)c; }
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::lock_guard<std::mutex> l(g_mx); g_ob.append(s, n); return n;
    }
    int sync() override { return 0; }
};
static GuiBuf g_gbuf;

static void FlushGui() {
    std::string t;
    { std::lock_guard<std::mutex> l(g_mx); if (g_ob.empty()) return; t.swap(g_ob); }
    if (!g_hOut) return;
    std::string r;
    r.reserve(t.size() + t.size() / 4);
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '\n' && (i == 0 || t[i-1] != '\r')) r += '\r';
        r += t[i];
    }
    int n = GetWindowTextLengthA(g_hOut);
    SendMessageA(g_hOut, EM_SETSEL, n, n);
    SendMessageA(g_hOut, EM_REPLACESEL, FALSE, (LPARAM)r.c_str());
    SendMessageA(g_hOut, EM_SCROLLCARET, 0, 0);
}

static int GetEditInt(HWND h) { char b[32]; GetWindowTextA(h, b, 32); return atoi(b); }

// ===================== Funciones comunes =====================

Matrix generate_matrix(int rows, int cols, std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 9);
    Matrix m(rows, std::vector<int>(cols));
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            m[i][j] = dist(rng);
    return m;
}

void print_matrix(const Matrix& m, const std::string& name) {
    std::cout << "\nMatriz " << name << ":\n";
    for (const auto& row : m) {
        std::cout << "  ";
        for (int v : row)
            std::cout << std::setw(4) << v << "  ";
        std::cout << "\n";
    }
}

Matrix multiply(const Matrix& A, const Matrix& B) {
    int rows_a = (int)A.size();
    int cols_a = (int)A[0].size();
    int cols_b = (int)B[0].size();
    Matrix C(rows_a, std::vector<int>(cols_b, 0));
    for (int i = 0; i < rows_a; ++i)
        for (int j = 0; j < cols_b; ++j)
            for (int k = 0; k < cols_a; ++k)
                C[i][j] += A[i][k] * B[k][j];
    return C;
}

double get_memory_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
#endif
    return 0.0;
}

double get_process_cpu_time() {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        ULARGE_INTEGER ki, ui;
        ki.LowPart = k.dwLowDateTime; ki.HighPart = k.dwHighDateTime;
        ui.LowPart = u.dwLowDateTime; ui.HighPart = u.dwHighDateTime;
        return (ki.QuadPart + ui.QuadPart) / 10000000.0;
    }
#endif
    return 0.0;
}

// ===================== FUNCIONES DE INFORMACION DEL PROCESO =====================

#ifdef _WIN32

void mostrar_info_ipc() {
    std::cout << "\n========== INFORMACION IPC (Inter-Process Communication) ==========\n";

    DWORD handleCount = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) {
        std::cout << "  Handles abiertos:       " << handleCount << "\n";
    }

    DWORD pid = GetCurrentProcessId();
    std::cout << "  PID del proceso:        " << pid << "\n";

    HWND consoleWnd = GetConsoleWindow();
    std::cout << "  Consola asociada:       " << (consoleWnd ? "Si" : "No") << "\n";

    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);

    std::cout << "  Handle STDIN:           " << hStdIn << "\n";
    std::cout << "  Handle STDOUT:          " << hStdOut << "\n";
    std::cout << "  Handle STDERR:          " << hStdErr << "\n";

    std::cout << "===================================================================\n";
}

void mostrar_info_pila() {
    std::cout << "\n========== INFORMACION DE LA PILA (STACK) ==========\n";

    MEMORY_BASIC_INFORMATION mbi;
    volatile int stackVar = 0;
    void* stackAddr = (void*)&stackVar;

    if (VirtualQuery(stackAddr, &mbi, sizeof(mbi))) {
        SIZE_T stackReserved = mbi.RegionSize;

        std::cout << "  Direccion base de pila:     0x" << std::hex << mbi.AllocationBase << std::dec << "\n";
        std::cout << "  Direccion actual (aprox):   0x" << std::hex << stackAddr << std::dec << "\n";
        std::cout << "  Tamano de region:           " << (stackReserved / 1024) << " KB\n";
        std::cout << "  Estado de memoria:          ";
        switch (mbi.State) {
            case MEM_COMMIT:  std::cout << "COMMIT (en uso)\n"; break;
            case MEM_RESERVE: std::cout << "RESERVE (reservada)\n"; break;
            case MEM_FREE:    std::cout << "FREE (libre)\n"; break;
            default:          std::cout << "Desconocido\n";
        }
        std::cout << "  Proteccion:                 ";
        if (mbi.Protect & PAGE_READWRITE) std::cout << "LECTURA/ESCRITURA\n";
        else if (mbi.Protect & PAGE_READONLY) std::cout << "SOLO LECTURA\n";
        else if (mbi.Protect & PAGE_EXECUTE_READWRITE) std::cout << "EJECUTAR/LEER/ESCRIBIR\n";
        else std::cout << "0x" << std::hex << mbi.Protect << std::dec << "\n";
    }

    DWORD threadId = GetCurrentThreadId();
    std::cout << "  ID del hilo actual:         " << threadId << "\n";

    std::cout << "====================================================\n";
}

void mostrar_info_datos() {
    std::cout << "\n========== INFORMACION DE DATOS DEL PROGRAMA ==========\n";

    PROCESS_MEMORY_COUNTERS_EX pmcEx;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmcEx, sizeof(pmcEx))) {
        std::cout << "  Working Set (RAM usada):        " << std::setw(10) << (pmcEx.WorkingSetSize / 1024) << " KB\n";
        std::cout << "  Peak Working Set:               " << std::setw(10) << (pmcEx.PeakWorkingSetSize / 1024) << " KB\n";
        std::cout << "  Private Bytes (Heap+Stack):     " << std::setw(10) << (pmcEx.PrivateUsage / 1024) << " KB\n";
        std::cout << "  Page File Usage:                " << std::setw(10) << (pmcEx.PagefileUsage / 1024) << " KB\n";
        std::cout << "  Page Faults:                    " << std::setw(10) << pmcEx.PageFaultCount << "\n";
    }

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    if (GlobalMemoryStatusEx(&memInfo)) {
        std::cout << "\n  -- Memoria del Sistema --\n";
        std::cout << "  Memoria fisica total:           " << std::setw(10) << (memInfo.ullTotalPhys / (1024*1024)) << " MB\n";
        std::cout << "  Memoria fisica disponible:      " << std::setw(10) << (memInfo.ullAvailPhys / (1024*1024)) << " MB\n";
        std::cout << "  Memoria virtual total:          " << std::setw(10) << (memInfo.ullTotalVirtual / (1024*1024)) << " MB\n";
        std::cout << "  Memoria virtual disponible:     " << std::setw(10) << (memInfo.ullAvailVirtual / (1024*1024)) << " MB\n";
        std::cout << "  Uso de memoria:                 " << std::setw(10) << memInfo.dwMemoryLoad << " %\n";
    }

    std::cout << "========================================================\n";
}

void mostrar_modulos_proceso() {
    std::cout << "\n========== MODULOS/DLLs CARGADOS EN EL PROCESO ==========\n";

    HANDLE hProcess = GetCurrentProcess();
    HMODULE hMods[1024];
    DWORD cbNeeded;

    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        int numModules = cbNeeded / sizeof(HMODULE);
        std::cout << "  Total de modulos cargados: " << numModules << "\n\n";

        std::cout << "  " << std::left << std::setw(45) << "NOMBRE DEL MODULO"
                  << std::right << std::setw(18) << "DIRECCION BASE"
                  << std::setw(12) << "TAMANO" << "\n";
        std::cout << "  " << std::string(75, '-') << "\n";

        for (int i = 0; i < numModules && i < 30; i++) {
            char modName[MAX_PATH];
            MODULEINFO modInfo;

            if (GetModuleFileNameExA(hProcess, hMods[i], modName, sizeof(modName))) {
                std::string fullPath(modName);
                size_t pos = fullPath.find_last_of("\\/");
                std::string fileName = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;

                if (GetModuleInformation(hProcess, hMods[i], &modInfo, sizeof(modInfo))) {
                    std::cout << "  " << std::left << std::setw(45) << fileName
                              << "0x" << std::hex << std::right << std::setw(16) << modInfo.lpBaseOfDll
                              << std::dec << std::setw(10) << (modInfo.SizeOfImage / 1024) << " KB\n";
                }
            }
        }

        if (numModules > 30) {
            std::cout << "\n  ... y " << (numModules - 30) << " modulos mas\n";
        }
    }

    std::cout << "==========================================================\n";
}

void mostrar_acceso_nucleo() {
    std::cout << "\n========== ACCESO AL NUCLEO (KERNEL) ==========\n";

    FILETIME creationTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER kTime, uTime;
        kTime.LowPart = kernelTime.dwLowDateTime;
        kTime.HighPart = kernelTime.dwHighDateTime;
        uTime.LowPart = userTime.dwLowDateTime;
        uTime.HighPart = userTime.dwHighDateTime;

        double kernelSec = kTime.QuadPart / 10000000.0;
        double userSec = uTime.QuadPart / 10000000.0;
        double totalSec = kernelSec + userSec;

        std::cout << "\n  -- Tiempo de CPU del Proceso --\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "  Tiempo en MODO KERNEL:      " << std::setw(12) << kernelSec << " s\n";
        std::cout << "  Tiempo en MODO USUARIO:     " << std::setw(12) << userSec << " s\n";
        std::cout << "  Tiempo TOTAL de CPU:        " << std::setw(12) << totalSec << " s\n";

        if (totalSec > 0) {
            double kernelPct = (kernelSec / totalSec) * 100.0;
            double userPct = (userSec / totalSec) * 100.0;
            std::cout << std::setprecision(1);
            std::cout << "  Porcentaje en Kernel:       " << std::setw(12) << kernelPct << " %\n";
            std::cout << "  Porcentaje en Usuario:      " << std::setw(12) << userPct << " %\n";
        }

        SYSTEMTIME stCreation;
        FILETIME localCreation;
        FileTimeToLocalFileTime(&creationTime, &localCreation);
        FileTimeToSystemTime(&localCreation, &stCreation);
        std::cout << "\n  Proceso iniciado:           "
                  << std::setfill('0') << std::setw(2) << stCreation.wHour << ":"
                  << std::setw(2) << stCreation.wMinute << ":"
                  << std::setw(2) << stCreation.wSecond << std::setfill(' ') << "\n";
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    std::cout << "\n  -- Informacion del Sistema (Nucleos) --\n";
    std::cout << "  Numero de procesadores:     " << std::setw(12) << sysInfo.dwNumberOfProcessors << "\n";
    std::cout << "  Arquitectura del procesador:";
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: std::cout << "         x64 (AMD64)\n"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: std::cout << "         x86 (Intel)\n"; break;
        case PROCESSOR_ARCHITECTURE_ARM:   std::cout << "         ARM\n"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: std::cout << "         ARM64\n"; break;
        default: std::cout << "         Desconocida (" << sysInfo.wProcessorArchitecture << ")\n";
    }
    std::cout << "  Nivel del procesador:       " << std::setw(12) << sysInfo.wProcessorLevel << "\n";
    std::cout << "  Revision del procesador:    " << std::setw(12) << sysInfo.wProcessorRevision << "\n";
    std::cout << "  Tamano de pagina:           " << std::setw(10) << (sysInfo.dwPageSize / 1024) << " KB\n";
    std::cout << "  Direccion min aplicacion:   0x" << std::hex << sysInfo.lpMinimumApplicationAddress << std::dec << "\n";
    std::cout << "  Direccion max aplicacion:   0x" << std::hex << sysInfo.lpMaximumApplicationAddress << std::dec << "\n";

    DWORD_PTR processAffinity, systemAffinity;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
        std::cout << "\n  -- Afinidad de Nucleos --\n";
        std::cout << "  Mascara del proceso:        0x" << std::hex << processAffinity << std::dec << "\n";
        std::cout << "  Mascara del sistema:        0x" << std::hex << systemAffinity << std::dec << "\n";

        std::cout << "  Nucleos disponibles:        ";
        bool first = true;
        for (int i = 0; i < 64; i++) {
            if (processAffinity & (1ULL << i)) {
                if (!first) std::cout << ", ";
                std::cout << i;
                first = false;
            }
        }
        std::cout << "\n";

        int coreCount = 0;
        DWORD_PTR temp = processAffinity;
        while (temp) {
            coreCount += temp & 1;
            temp >>= 1;
        }
        std::cout << "  Total nucleos asignados:    " << std::setw(12) << coreCount << "\n";
    }

    DWORD priorityClass = GetPriorityClass(GetCurrentProcess());
    std::cout << "\n  -- Prioridad del Proceso --\n";
    std::cout << "  Clase de prioridad:         ";
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS:         std::cout << "IDLE (Baja)\n"; break;
        case BELOW_NORMAL_PRIORITY_CLASS: std::cout << "BELOW_NORMAL\n"; break;
        case NORMAL_PRIORITY_CLASS:       std::cout << "NORMAL\n"; break;
        case ABOVE_NORMAL_PRIORITY_CLASS: std::cout << "ABOVE_NORMAL\n"; break;
        case HIGH_PRIORITY_CLASS:         std::cout << "HIGH (Alta)\n"; break;
        case REALTIME_PRIORITY_CLASS:     std::cout << "REALTIME (Tiempo real)\n"; break;
        default: std::cout << "Desconocida (0x" << std::hex << priorityClass << std::dec << ")\n";
    }

    int threadPriority = GetThreadPriority(GetCurrentThread());
    std::cout << "  Prioridad del hilo:         ";
    switch (threadPriority) {
        case THREAD_PRIORITY_IDLE:          std::cout << "IDLE\n"; break;
        case THREAD_PRIORITY_LOWEST:        std::cout << "LOWEST\n"; break;
        case THREAD_PRIORITY_BELOW_NORMAL:  std::cout << "BELOW_NORMAL\n"; break;
        case THREAD_PRIORITY_NORMAL:        std::cout << "NORMAL\n"; break;
        case THREAD_PRIORITY_ABOVE_NORMAL:  std::cout << "ABOVE_NORMAL\n"; break;
        case THREAD_PRIORITY_HIGHEST:       std::cout << "HIGHEST\n"; break;
        case THREAD_PRIORITY_TIME_CRITICAL: std::cout << "TIME_CRITICAL\n"; break;
        default: std::cout << threadPriority << "\n";
    }

    ULONG64 cycleTime = 0;
    typedef BOOL (WINAPI *QueryProcessCycleTimeFunc)(HANDLE, PULONG64);
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32) {
        QueryProcessCycleTimeFunc pQueryProcessCycleTime =
            (QueryProcessCycleTimeFunc)GetProcAddress(hKernel32, "QueryProcessCycleTime");
        if (pQueryProcessCycleTime && pQueryProcessCycleTime(GetCurrentProcess(), &cycleTime)) {
            std::cout << "\n  -- Ciclos de CPU --\n";
            std::cout << "  Ciclos totales del proceso: " << cycleTime << "\n";
            if (cycleTime > 0) {
                FILETIME c, e, k, u;
                GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
                ULARGE_INTEGER ki, ui;
                ki.LowPart = k.dwLowDateTime; ki.HighPart = k.dwHighDateTime;
                ui.LowPart = u.dwLowDateTime; ui.HighPart = u.dwHighDateTime;
                double totalTime = (ki.QuadPart + ui.QuadPart) / 10000000.0;
                if (totalTime > 0.001) {
                    double ghz = (cycleTime / totalTime) / 1e9;
                    std::cout << std::setprecision(2);
                    std::cout << "  Frecuencia estimada:        " << std::setw(10) << ghz << " GHz\n";
                }
            }
        }
    }

    IO_COUNTERS ioCounters;
    if (GetProcessIoCounters(GetCurrentProcess(), &ioCounters)) {
        std::cout << "\n  -- Operaciones de I/O (Llamadas al Kernel) --\n";
        std::cout << "  Operaciones de lectura:     " << std::setw(12) << ioCounters.ReadOperationCount << "\n";
        std::cout << "  Operaciones de escritura:   " << std::setw(12) << ioCounters.WriteOperationCount << "\n";
        std::cout << "  Otras operaciones:          " << std::setw(12) << ioCounters.OtherOperationCount << "\n";
        std::cout << "  Bytes leidos:               " << std::setw(12) << (ioCounters.ReadTransferCount / 1024) << " KB\n";
        std::cout << "  Bytes escritos:             " << std::setw(12) << (ioCounters.WriteTransferCount / 1024) << " KB\n";
        std::cout << "  Otros bytes transferidos:   " << std::setw(12) << (ioCounters.OtherTransferCount / 1024) << " KB\n";
    }

    std::cout << "\n  -- Contexto de Ejecucion --\n";
    std::cout << "  PID del proceso:            " << std::setw(12) << GetCurrentProcessId() << "\n";
    std::cout << "  TID del hilo principal:     " << std::setw(12) << GetCurrentThreadId() << "\n";

    DWORD processorNumber = GetCurrentProcessorNumber();
    std::cout << "  Nucleo actual de ejecucion: " << std::setw(12) << processorNumber << "\n";

    BOOL isWow64 = FALSE;
    typedef BOOL (WINAPI *IsWow64ProcessFunc)(HANDLE, PBOOL);
    IsWow64ProcessFunc pIsWow64Process =
        (IsWow64ProcessFunc)GetProcAddress(hKernel32, "IsWow64Process");
    if (pIsWow64Process) {
        pIsWow64Process(GetCurrentProcess(), &isWow64);
    }
    std::cout << "  Proceso WoW64 (32 en 64):   " << (isWow64 ? "Si" : "No") << "\n";

    std::cout << "===============================================\n";
}

void mostrar_info_programa() {
    std::cout << "\n========== SEGMENTO DE PROGRAMA (CODIGO) ==========\n";

    HMODULE hModule = GetModuleHandle(NULL);
    MODULEINFO modInfo;

    if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        std::cout << "\n  -- Ejecutable Principal --\n";
        std::cout << "  Direccion base del codigo:  0x" << std::hex << modInfo.lpBaseOfDll << std::dec << "\n";
        std::cout << "  Punto de entrada:           0x" << std::hex << modInfo.EntryPoint << std::dec << "\n";
        std::cout << "  Tamano de la imagen:        " << (modInfo.SizeOfImage / 1024) << " KB\n";
    }

    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        std::cout << "  Ruta del ejecutable:        " << exePath << "\n";
    }

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)&mostrar_info_programa, &mbi, sizeof(mbi))) {
        std::cout << "\n  -- Segmento de Codigo en Memoria --\n";
        std::cout << "  Direccion de esta funcion:  0x" << std::hex << (void*)&mostrar_info_programa << std::dec << "\n";
        std::cout << "  Region base:                0x" << std::hex << mbi.BaseAddress << std::dec << "\n";
        std::cout << "  Tamano de la region:        " << (mbi.RegionSize / 1024) << " KB\n";
        std::cout << "  Proteccion:                 ";
        if (mbi.Protect & PAGE_EXECUTE_READ) std::cout << "EJECUTAR+LEER (codigo)\n";
        else if (mbi.Protect & PAGE_EXECUTE_READWRITE) std::cout << "EJECUTAR+LEER+ESCRIBIR\n";
        else if (mbi.Protect & PAGE_EXECUTE) std::cout << "SOLO EJECUTAR\n";
        else if (mbi.Protect & PAGE_READONLY) std::cout << "SOLO LECTURA (datos)\n";
        else if (mbi.Protect & PAGE_READWRITE) std::cout << "LECTURA+ESCRITURA (datos)\n";
        else std::cout << "0x" << std::hex << mbi.Protect << std::dec << "\n";
    }

    std::cout << "\n  -- Estructura del Proceso en Memoria --\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |          PILA (Stack)           | <- Variables locales\n";
    std::cout << "  |              ...                |\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |          HEAP (Monticulo)       | <- new, malloc\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |          DATOS (.data)          | <- Variables globales\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |          CODIGO (.text)         | <- Instrucciones\n";
    std::cout << "  +----------------------------------+\n";

    std::cout << "===================================================\n";
}

void mostrar_llamadas_sistema() {
    std::cout << "\n========== LLAMADAS AL SISTEMA (SYSCALLS) ==========\n";

    std::cout << "\n  Este programa SECUENCIAL utiliza las siguientes\n";
    std::cout << "  llamadas al sistema de Windows (API del Kernel):\n";

    std::cout << "\n  +------------------------------------------------------------+\n";
    std::cout << "  | CATEGORIA        | FUNCION API           | PROPOSITO       |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "  | PROCESOS         | GetCurrentProcess()   | Handle propio   |\n";
    std::cout << "  |                  | GetCurrentProcessId() | PID del proceso |\n";
    std::cout << "  |                  | GetProcessTimes()     | Tiempos CPU     |\n";
    std::cout << "  |                  | GetPriorityClass()    | Prioridad       |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "  | HILOS            | GetCurrentThread()    | Handle del hilo |\n";
    std::cout << "  |                  | GetCurrentThreadId()  | TID del hilo    |\n";
    std::cout << "  |                  | GetThreadPriority()   | Prioridad hilo  |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "  | MEMORIA          | VirtualQuery()        | Info de memoria |\n";
    std::cout << "  |                  | GetProcessMemoryInfo()| Uso de RAM      |\n";
    std::cout << "  |                  | GlobalMemoryStatusEx()| Memoria sistema |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "  | SISTEMA          | GetSystemInfo()       | Info del CPU    |\n";
    std::cout << "  |                  | GetCurrentProcessor() | Core actual     |\n";
    std::cout << "  |                  | QueryProcessCycleTime | Ciclos CPU      |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "  | ENTRADA/SALIDA   | GetStdHandle()        | Handles E/S     |\n";
    std::cout << "  |                  | GetProcessIoCounters()| Contadores I/O  |\n";
    std::cout << "  |                  | GetConsoleWindow()    | Ventana consola |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "  | MODULOS          | GetModuleHandle()     | Handle DLL      |\n";
    std::cout << "  |                  | EnumProcessModules()  | Lista modulos   |\n";
    std::cout << "  |                  | GetModuleInformation()| Info de modulo  |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "\n  -- Flujo de una Llamada al Sistema --\n";
    std::cout << "  \n";
    std::cout << "   MODO USUARIO                    MODO KERNEL\n";
    std::cout << "  +----------------+              +------------------+\n";
    std::cout << "  | Tu programa    |  syscall    | Kernel de Windows|\n";
    std::cout << "  | (MMS.exe)      | =========>  | (ntoskrnl.exe)   |\n";
    std::cout << "  |                |  resultado  |                  |\n";
    std::cout << "  |                | <=========  |                  |\n";
    std::cout << "  +----------------+              +------------------+\n";
    std::cout << "        |                                 |\n";
    std::cout << "        v                                 v\n";
    std::cout << "   Ring 3 (Usuario)                Ring 0 (Kernel)\n";
    std::cout << "   - Sin privilegios               - Acceso total\n";
    std::cout << "   - Memoria virtual               - Memoria fisica\n";
    std::cout << "   - CPU limitada                  - Control del HW\n";

    std::cout << "\n  -- Nota sobre Programa SECUENCIAL --\n";
    std::cout << "  Este programa usa UN SOLO HILO de ejecucion.\n";
    std::cout << "  No requiere sincronizacion (mutex, semaforos).\n";
    std::cout << "  Solo usa un core del procesador a la vez.\n";

    std::cout << "====================================================\n";
}

#endif

// ===================== Ejecucion del calculo =====================

struct Sample { double cpu_pct; double mem_mb; };

static void RunComputation(int rows_a, int cols_a, int cols_b) {
    std::cout << std::unitbuf;
    std::cout << "=== MULTIPLICACION DE MATRICES - SECUENCIAL (C++) ===\n\n";
    std::cout << "Filas de A: " << rows_a << "\n";
    std::cout << "Columnas de A (= Filas de B): " << cols_a << "\n";
    std::cout << "Columnas de B: " << cols_b << "\n";

    std::cout << "\nSemilla aleatoria: " << SEED << "\n";
    std::mt19937 rng(SEED);

    std::cout << "Generando matrices...\n";
    Matrix A = generate_matrix(rows_a, cols_a, rng);
    Matrix B = generate_matrix(cols_a, cols_b, rng);

    if (rows_a <= 10 && cols_b <= 10) {
        print_matrix(A, "A");
        print_matrix(B, "B");
    }

    std::cout << "\nIniciando multiplicacion secuencial con monitoreo...\n\n";

    std::vector<Sample> samples;
    std::mutex smtx;
    std::atomic<bool> running{true};

    double cpu_before = get_process_cpu_time();
    double mem_before = get_memory_mb();

    std::thread monitor([&]() {
        double prev_cpu = get_process_cpu_time();
        auto prev_wall = std::chrono::steady_clock::now();

        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (!running.load()) break;

            auto now = std::chrono::steady_clock::now();
            double cur_cpu = get_process_cpu_time();
            double dwall = std::chrono::duration<double>(now - prev_wall).count();
            double dcpu = cur_cpu - prev_cpu;
            double pct = (dwall > 0.001) ? (dcpu / dwall) * 100.0 : 0.0;
            double mem = get_memory_mb();

            {
                std::lock_guard<std::mutex> lk(smtx);
                samples.push_back({pct, mem});
            }

            std::cout << "  [Monitor] CPU: " << std::fixed << std::setprecision(1)
                      << std::setw(6) << pct << "%  |  Memoria RAM: "
                      << std::setprecision(2) << std::setw(8) << mem << " MB\n"
                      << std::flush;

            prev_cpu = cur_cpu;
            prev_wall = now;
        }
    });

    auto t0 = std::chrono::steady_clock::now();
    Matrix C = multiply(A, B);
    auto t1 = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    running.store(false);
    monitor.join();

    double cpu_after = get_process_cpu_time();
    double mem_after = get_memory_mb();
    double cpu_used = cpu_after - cpu_before;

    if (rows_a <= 10 && cols_b <= 10)
        print_matrix(C, "C = A x B");

    std::cout << "\nDimensiones: A(" << rows_a << "x" << cols_a << ") x B("
              << cols_a << "x" << cols_b << ") = C(" << rows_a << "x" << cols_b << ")\n";
    std::cout << std::fixed << std::setprecision(6)
              << "Tiempo de ejecucion: " << elapsed << " segundos\n";

    std::cout << "\n========== RESUMEN DE METRICAS ==========\n";
    std::cout << std::setprecision(6)
              << "  Tiempo de ejecucion:    " << elapsed << " s\n"
              << "  Tiempo CPU consumido:   " << cpu_used << " s\n"
              << std::setprecision(2)
              << "  Memoria antes:          " << mem_before << " MB\n"
              << "  Memoria despues:        " << mem_after << " MB\n";

    {
        std::lock_guard<std::mutex> lk(smtx);
        if (!samples.empty()) {
            double sum_cpu = 0, max_cpu = 0;
            double sum_mem = 0, max_mem = 0, min_mem = samples[0].mem_mb;
            for (const auto& s : samples) {
                sum_cpu += s.cpu_pct;
                if (s.cpu_pct > max_cpu) max_cpu = s.cpu_pct;
                sum_mem += s.mem_mb;
                if (s.mem_mb > max_mem) max_mem = s.mem_mb;
                if (s.mem_mb < min_mem) min_mem = s.mem_mb;
            }
            std::cout << "\n  -- Muestras en tiempo real --\n"
                      << "  Muestras recolectadas:  " << samples.size() << "\n"
                      << std::setprecision(1)
                      << "  CPU promedio:           " << sum_cpu / samples.size() << "%\n"
                      << "  CPU maximo:             " << max_cpu << "%\n"
                      << std::setprecision(2)
                      << "  Memoria promedio:       " << sum_mem / samples.size() << " MB\n"
                      << "  Memoria maxima:         " << max_mem << " MB\n"
                      << "  Memoria minima:         " << min_mem << " MB\n";
        } else {
            std::cout << "\n  (La multiplicacion termino muy rapido para capturar\n"
                      << "   muestras en tiempo real. Use matrices mas grandes\n"
                      << "   como 300x300 para ver el monitoreo en vivo.)\n";
        }
    }

    if (elapsed > 0) {
        double efficiency = (cpu_used / elapsed) * 100.0;
        std::cout << std::setprecision(1)
                  << "\n  Eficiencia CPU:         " << efficiency << "%\n"
                  << "  (Un valor cercano a 100% indica uso completo de 1 core)\n";
    }
    std::cout << "==========================================\n";

#ifdef _WIN32
    std::cout << "\n\n";
    std::cout << "######################################################################\n";
    std::cout << "#                                                                    #\n";
    std::cout << "#     INFORMACION DEL PROCESO - SISTEMAS OPERATIVOS                 #\n";
    std::cout << "#     Programa: MMS.cpp (Multiplicacion de Matrices SECUENCIAL)     #\n";
    std::cout << "#                                                                    #\n";
    std::cout << "######################################################################\n";

    mostrar_info_programa();
    mostrar_info_pila();
    mostrar_info_datos();
    mostrar_info_ipc();
    mostrar_acceso_nucleo();
    mostrar_llamadas_sistema();
    mostrar_modulos_proceso();
#endif
}

// ===================== Procedimiento de ventana =====================

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hI = ((LPCREATESTRUCT)lParam)->hInstance;

        g_fMono = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, "Consolas");
        g_fUI = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        auto mkLabel = [&](const char* txt, int x, int y, int w) {
            HWND h = CreateWindowExA(0, "STATIC", txt, WS_CHILD|WS_VISIBLE,
                x, y, w, 20, hWnd, NULL, hI, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fUI, TRUE);
        };
        auto mkEdit = [&](int id, int x, int y) -> HWND {
            HWND h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "5",
                WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_CENTER,
                x, y, 65, 24, hWnd, (HMENU)(INT_PTR)id, hI, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fUI, TRUE);
            return h;
        };

        mkLabel("Filas de A:", 15, 16, 90);
        g_hRows = mkEdit(IDC_ROWS, 110, 13);

        mkLabel("Columnas de A (Filas de B):", 195, 16, 210);
        g_hCols = mkEdit(IDC_COLS, 410, 13);

        mkLabel("Columnas de B:", 495, 16, 115);
        g_hColB = mkEdit(IDC_COLB, 615, 13);

        g_hRun = CreateWindowExA(0, "BUTTON", "Ejecutar",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 15, 50, 145, 32,
            hWnd, (HMENU)IDC_RUN, hI, NULL);
        SendMessageA(g_hRun, WM_SETFONT, (WPARAM)g_fUI, TRUE);

        HWND hClr = CreateWindowExA(0, "BUTTON", "Limpiar",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 170, 50, 145, 32,
            hWnd, (HMENU)IDC_CLR, hI, NULL);
        SendMessageA(hClr, WM_SETFONT, (WPARAM)g_fUI, TRUE);

        RECT rc; GetClientRect(hWnd, &rc);
        g_hOut = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|
            ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_READONLY,
            10, 95, rc.right - 20, rc.bottom - 105,
            hWnd, (HMENU)IDC_OUT, hI, NULL);
        SendMessageA(g_hOut, WM_SETFONT, (WPARAM)g_fMono, TRUE);
        SendMessageA(g_hOut, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

        SetTimer(hWnd, IDT_TMR, 100, NULL);
        return 0;
    }

    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        if (g_hOut) MoveWindow(g_hOut, 10, 95, rc.right - 20, rc.bottom - 105, TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* m = (MINMAXINFO*)lParam;
        m->ptMinTrackSize.x = 750;
        m->ptMinTrackSize.y = 400;
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_TMR) FlushGui();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_RUN: {
            int ra = GetEditInt(g_hRows);
            int ca = GetEditInt(g_hCols);
            int cb = GetEditInt(g_hColB);
            if (ra <= 0 || ca <= 0 || cb <= 0) {
                MessageBoxA(hWnd, "Todas las dimensiones deben ser mayores a 0.",
                    "Error de entrada", MB_OK|MB_ICONERROR);
                return 0;
            }
            EnableWindow(g_hRun, FALSE);
            SetWindowTextA(g_hRun, "Calculando...");
            std::thread([ra, ca, cb]() {
                RunComputation(ra, ca, cb);
                PostMessageA(g_hWnd, WM_DONE, 0, 0);
            }).detach();
            return 0;
        }
        case IDC_CLR:
            SetWindowTextA(g_hOut, "");
            return 0;
        }
        break;

    case WM_DONE:
        FlushGui();
        EnableWindow(g_hRun, TRUE);
        SetWindowTextA(g_hRun, "Ejecutar");
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, IDT_TMR);
        if (g_fMono) { DeleteObject(g_fMono); g_fMono = NULL; }
        if (g_fUI)   { DeleteObject(g_fUI);   g_fUI = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// ===================== Punto de entrada =====================

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    std::cout.rdbuf(&g_gbuf);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = "MMSClass";
    wc.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    g_hWnd = CreateWindowExA(0, "MMSClass",
        "Multiplicacion de Matrices - SECUENCIAL (C++)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 720,
        NULL, NULL, hInst, NULL);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
