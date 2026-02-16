// Multiplicacion de matrices PARALELO (multiples hilos, multiples cores) en C++
//
// Compilar con MSVC:  cl /O2 /EHsc MMP.cpp /link psapi.lib
// Compilar con g++:   g++ -O2 -std=c++17 -o MMP.exe MMP.cpp -lpsapi

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <string>
#include <memory>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "psapi.lib")
#endif

using Matrix = std::vector<std::vector<int>>;
static constexpr int SEED = 42;

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

double get_memory_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
#endif
    return 0.0;
}

double get_thread_cpu_time() {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) {
        ULARGE_INTEGER ki, ui;
        ki.LowPart = k.dwLowDateTime; ki.HighPart = k.dwHighDateTime;
        ui.LowPart = u.dwLowDateTime; ui.HighPart = u.dwHighDateTime;
        return (ki.QuadPart + ui.QuadPart) / 10000000.0;
    }
#endif
    return 0.0;
}

// ===================== Metricas por hilo =====================

struct ThreadMetrics {
    int thread_id = 0;
    int core_id = 0;
    int row_start = 0;
    int row_end = 0;
    unsigned long native_tid = 0;
    int rows_done = 0;
    int total_rows = 0;
    double progress = 0.0;
    double cpu_pct = 0.0;
    double elapsed = 0.0;
    double total_time = 0.0;
    bool started = false;
    bool done = false;
    std::vector<double> cpu_samples;
    std::mutex mtx;
};

// ===================== Funcion del hilo worker =====================

void worker_func(const Matrix& A, const Matrix& B, Matrix& C, ThreadMetrics& info) {
    // Fijar hilo a un core especifico
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << info.core_id);
    info.native_tid = GetCurrentThreadId();
#endif

    int row_start = info.row_start;
    int row_end = info.row_end;
    int total = row_end - row_start;
    int cols_b = (int)B[0].size();
    int cols_a = (int)A[0].size();
    int report_interval = std::max(1, total / 20);

    double prev_cpu = get_thread_cpu_time();
    auto prev_wall = std::chrono::steady_clock::now();
    auto start_wall = prev_wall;

    {
        std::lock_guard<std::mutex> lk(info.mtx);
        info.total_rows = total;
        info.started = true;
    }

    for (int idx = 0; idx < total; ++idx) {
        int i = row_start + idx;
        for (int j = 0; j < cols_b; ++j) {
            int sum = 0;
            for (int k = 0; k < cols_a; ++k)
                sum += A[i][k] * B[k][j];
            C[i][j] = sum;
        }

        if ((idx + 1) % report_interval == 0 || idx == total - 1) {
            auto now = std::chrono::steady_clock::now();
            double cur_cpu = get_thread_cpu_time();
            double dwall = std::chrono::duration<double>(now - prev_wall).count();
            double dcpu = cur_cpu - prev_cpu;
            double pct = (dwall > 0.001) ? (dcpu / dwall) * 100.0 : 0.0;
            double el = std::chrono::duration<double>(now - start_wall).count();

            {
                std::lock_guard<std::mutex> lk(info.mtx);
                info.rows_done = idx + 1;
                info.progress = (idx + 1) * 100.0 / total;
                info.cpu_pct = pct;
                info.elapsed = el;
                info.cpu_samples.push_back(pct);
                if (idx == total - 1) {
                    info.done = true;
                    info.total_time = el;
                }
            }

            prev_cpu = cur_cpu;
            prev_wall = now;
        }
    }
}

// ===================== FUNCIONES DE INFORMACION DEL PROCESO =====================

#ifdef _WIN32

// Obtener informacion de IPC (handles abiertos del proceso) - VERSION MULTIHILO
void mostrar_info_ipc(int num_threads) {
    std::cout << "\n========== INFORMACION IPC (Inter-Process Communication) ==========\n";

    DWORD handleCount = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) {
        std::cout << "  Handles abiertos:           " << handleCount << "\n";
    }

    // Informacion del proceso actual
    DWORD pid = GetCurrentProcessId();
    std::cout << "  PID del proceso:            " << pid << "\n";

    // Verificar si hay consola (forma de IPC)
    HWND consoleWnd = GetConsoleWindow();
    std::cout << "  Consola asociada:           " << (consoleWnd ? "Si" : "No") << "\n";

    // Entrada/Salida estandar (pipes de IPC)
    HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);

    std::cout << "  Handle STDIN:               " << hStdIn << "\n";
    std::cout << "  Handle STDOUT:              " << hStdOut << "\n";
    std::cout << "  Handle STDERR:              " << hStdErr << "\n";

    // Informacion especifica de IPC para multihilo
    std::cout << "\n  -- IPC entre Hilos (Sincronizacion) --\n";
    std::cout << "  Hilos worker creados:       " << num_threads << "\n";
    std::cout << "  Hilo monitor:               1\n";
    std::cout << "  Total hilos del proceso:    " << (num_threads + 2) << " (incluye main)\n";
    std::cout << "  Mecanismos IPC usados:\n";
    std::cout << "    - std::mutex              (exclusion mutua para metricas)\n";
    std::cout << "    - std::atomic<bool>       (senalizacion de finalizacion)\n";
    std::cout << "    - std::lock_guard         (RAII para locks)\n";
    std::cout << "    - Memoria compartida      (matrices A, B, C)\n";

    std::cout << "===================================================================\n";
}

// Obtener informacion de la PILA (Stack) de cada hilo
void mostrar_info_pila(const std::vector<std::unique_ptr<ThreadMetrics>>& metrics) {
    std::cout << "\n========== INFORMACION DE LA PILA (STACK) ==========\n";

    // Pila del hilo principal (main)
    MEMORY_BASIC_INFORMATION mbi;
    volatile int stackVar = 0;
    void* stackAddr = (void*)&stackVar;

    std::cout << "\n  -- Pila del Hilo Principal (main) --\n";
    if (VirtualQuery(stackAddr, &mbi, sizeof(mbi))) {
        std::cout << "  Direccion base:             0x" << std::hex << mbi.AllocationBase << std::dec << "\n";
        std::cout << "  Direccion actual (aprox):   0x" << std::hex << stackAddr << std::dec << "\n";
        std::cout << "  Tamano de region:           " << (mbi.RegionSize / 1024) << " KB\n";
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

    DWORD mainThreadId = GetCurrentThreadId();
    std::cout << "  ID del hilo principal:      " << mainThreadId << "\n";

    // Informacion de los hilos worker
    std::cout << "\n  -- Hilos Worker (cada uno tiene su propia pila) --\n";
    std::cout << "  " << std::left << std::setw(10) << "HILO"
              << std::setw(12) << "TID"
              << std::setw(10) << "CORE"
              << std::setw(15) << "FILAS" << "\n";
    std::cout << "  " << std::string(47, '-') << "\n";

    for (size_t i = 0; i < metrics.size(); ++i) {
        std::lock_guard<std::mutex> lk(metrics[i]->mtx);
        auto& m = *metrics[i];
        std::cout << "  " << std::left << std::setw(10) << ("Worker " + std::to_string(i))
                  << std::setw(12) << m.native_tid
                  << std::setw(10) << m.core_id
                  << m.row_start << " - " << (m.row_end - 1) << "\n";
    }

    std::cout << "\n  Nota: Cada hilo tiene su propia pila independiente\n";
    std::cout << "        (tipicamente 1 MB por defecto en Windows)\n";

    std::cout << "====================================================\n";
}

// Obtener informacion de DATOS del programa (segmentos de memoria)
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

    // Informacion de memoria virtual
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

// Mostrar los modulos/DLLs cargados por el proceso
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

// Mostrar informacion de acceso al NUCLEO (Kernel) - VERSION MULTIHILO
void mostrar_acceso_nucleo(int num_threads, const std::vector<std::unique_ptr<ThreadMetrics>>& metrics) {
    std::cout << "\n========== ACCESO AL NUCLEO (KERNEL) - MULTIHILO ==========\n";

    // ---- TIEMPO EN MODO KERNEL vs MODO USUARIO ----
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

        std::cout << "\n  -- Tiempo de CPU del Proceso (TODOS los hilos) --\n";
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

    // ---- INFORMACION DEL SISTEMA Y PROCESADORES ----
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
        default: std::cout << "         Desconocida\n";
    }
    std::cout << "  Tamano de pagina:           " << std::setw(10) << (sysInfo.dwPageSize / 1024) << " KB\n";

    // ---- AFINIDAD DEL PROCESO ----
    DWORD_PTR processAffinity, systemAffinity;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
        std::cout << "\n  -- Afinidad de Nucleos --\n";
        std::cout << "  Mascara del proceso:        0x" << std::hex << processAffinity << std::dec << "\n";
        std::cout << "  Mascara del sistema:        0x" << std::hex << systemAffinity << std::dec << "\n";

        std::cout << "  Nucleos disponibles:        ";
        bool first = true;
        int coreCount = 0;
        for (int i = 0; i < 64; i++) {
            if (processAffinity & (1ULL << i)) {
                if (!first) std::cout << ", ";
                std::cout << i;
                first = false;
                coreCount++;
            }
        }
        std::cout << "\n";
        std::cout << "  Total nucleos asignados:    " << std::setw(12) << coreCount << "\n";
        std::cout << "  Hilos worker usando:        " << std::setw(12) << num_threads << " nucleos\n";
    }

    // ---- USO DE NUCLEOS POR HILO ----
    std::cout << "\n  -- Distribucion de Hilos en Nucleos --\n";
    std::cout << "  " << std::left << std::setw(12) << "HILO"
              << std::setw(10) << "TID"
              << std::setw(15) << "CORE ASIGNADO"
              << std::setw(15) << "TIEMPO (s)" << "\n";
    std::cout << "  " << std::string(52, '-') << "\n";

    for (size_t i = 0; i < metrics.size(); ++i) {
        std::lock_guard<std::mutex> lk(metrics[i]->mtx);
        auto& m = *metrics[i];
        std::cout << "  " << std::left << std::setw(12) << ("Worker " + std::to_string(i))
                  << std::setw(10) << m.native_tid
                  << std::setw(15) << ("Core " + std::to_string(m.core_id))
                  << std::fixed << std::setprecision(4) << m.total_time << "\n";
    }

    // ---- PRIORIDAD DEL PROCESO ----
    DWORD priorityClass = GetPriorityClass(GetCurrentProcess());
    std::cout << "\n  -- Prioridad del Proceso --\n";
    std::cout << "  Clase de prioridad:         ";
    switch (priorityClass) {
        case IDLE_PRIORITY_CLASS:         std::cout << "IDLE (Baja)\n"; break;
        case BELOW_NORMAL_PRIORITY_CLASS: std::cout << "BELOW_NORMAL\n"; break;
        case NORMAL_PRIORITY_CLASS:       std::cout << "NORMAL\n"; break;
        case ABOVE_NORMAL_PRIORITY_CLASS: std::cout << "ABOVE_NORMAL\n"; break;
        case HIGH_PRIORITY_CLASS:         std::cout << "HIGH (Alta)\n"; break;
        case REALTIME_PRIORITY_CLASS:     std::cout << "REALTIME\n"; break;
        default: std::cout << "Desconocida\n";
    }

    // ---- CICLOS DE CPU ----
    ULONG64 cycleTime = 0;
    typedef BOOL (WINAPI *QueryProcessCycleTimeFunc)(HANDLE, PULONG64);
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32) {
        QueryProcessCycleTimeFunc pQueryProcessCycleTime =
            (QueryProcessCycleTimeFunc)GetProcAddress(hKernel32, "QueryProcessCycleTime");
        if (pQueryProcessCycleTime && pQueryProcessCycleTime(GetCurrentProcess(), &cycleTime)) {
            std::cout << "\n  -- Ciclos de CPU (todos los hilos) --\n";
            std::cout << "  Ciclos totales:             " << cycleTime << "\n";

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

    // ---- CONTADORES DE I/O ----
    IO_COUNTERS ioCounters;
    if (GetProcessIoCounters(GetCurrentProcess(), &ioCounters)) {
        std::cout << "\n  -- Operaciones de I/O (Llamadas al Kernel) --\n";
        std::cout << "  Operaciones de lectura:     " << std::setw(12) << ioCounters.ReadOperationCount << "\n";
        std::cout << "  Operaciones de escritura:   " << std::setw(12) << ioCounters.WriteOperationCount << "\n";
        std::cout << "  Otras operaciones:          " << std::setw(12) << ioCounters.OtherOperationCount << "\n";
        std::cout << "  Bytes leidos:               " << std::setw(12) << (ioCounters.ReadTransferCount / 1024) << " KB\n";
        std::cout << "  Bytes escritos:             " << std::setw(12) << (ioCounters.WriteTransferCount / 1024) << " KB\n";
    }

    // ---- CONTEXTO DE EJECUCION ----
    std::cout << "\n  -- Contexto de Ejecucion --\n";
    std::cout << "  PID del proceso:            " << std::setw(12) << GetCurrentProcessId() << "\n";
    std::cout << "  TID del hilo main:          " << std::setw(12) << GetCurrentThreadId() << "\n";
    std::cout << "  Nucleo actual (main):       " << std::setw(12) << GetCurrentProcessorNumber() << "\n";

    // Tipo de proceso
    BOOL isWow64 = FALSE;
    typedef BOOL (WINAPI *IsWow64ProcessFunc)(HANDLE, PBOOL);
    IsWow64ProcessFunc pIsWow64Process =
        (IsWow64ProcessFunc)GetProcAddress(hKernel32, "IsWow64Process");
    if (pIsWow64Process) {
        pIsWow64Process(GetCurrentProcess(), &isWow64);
    }
    std::cout << "  Proceso WoW64 (32 en 64):   " << (isWow64 ? "Si" : "No") << "\n";

    // ---- COMPARACION SECUENCIAL VS PARALELO ----
    std::cout << "\n  -- Analisis de Paralelismo en Kernel --\n";
    std::cout << "  Hilos worker:               " << num_threads << "\n";
    std::cout << "  Cada hilo tiene:\n";
    std::cout << "    - Su propia pila (stack)\n";
    std::cout << "    - Su propio contexto de CPU\n";
    std::cout << "    - Afinidad fijada a un core especifico\n";
    std::cout << "  Recursos compartidos:\n";
    std::cout << "    - Matrices A, B (solo lectura)\n";
    std::cout << "    - Matriz C (escritura en regiones disjuntas)\n";
    std::cout << "    - Metricas (protegidas por mutex)\n";

    std::cout << "===========================================================\n";
}

// ========== INFORMACION DEL SEGMENTO DE PROGRAMA (CODIGO) ==========
void mostrar_info_programa() {
    std::cout << "\n========== SEGMENTO DE PROGRAMA (CODIGO) ==========\n";

    // Obtener el modulo principal (el ejecutable)
    HMODULE hModule = GetModuleHandle(NULL);
    MODULEINFO modInfo;

    if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        std::cout << "\n  -- Ejecutable Principal --\n";
        std::cout << "  Direccion base del codigo:  0x" << std::hex << modInfo.lpBaseOfDll << std::dec << "\n";
        std::cout << "  Punto de entrada:           0x" << std::hex << modInfo.EntryPoint << std::dec << "\n";
        std::cout << "  Tamano de la imagen:        " << (modInfo.SizeOfImage / 1024) << " KB\n";
    }

    // Nombre del ejecutable
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        std::cout << "  Ruta del ejecutable:        " << exePath << "\n";
    }

    // Informacion sobre el codigo en memoria
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

    std::cout << "\n  -- Estructura del Proceso MULTIHILO en Memoria --\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |     PILA Hilo Principal (main)  | <- Variables locales main\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |     PILA Hilo Worker 0          | <- Variables locales hilo 0\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |     PILA Hilo Worker 1          | <- Variables locales hilo 1\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |            ...                  |\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |     HEAP (Monticulo)            | <- new, malloc, matrices\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |     DATOS (.data)               | <- Variables globales\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "  |     CODIGO (.text)              | <- Instrucciones (compartido)\n";
    std::cout << "  +----------------------------------+\n";
    std::cout << "\n  Nota: Cada hilo tiene su PROPIA PILA pero comparten\n";
    std::cout << "        el mismo CODIGO, DATOS y HEAP.\n";

    std::cout << "===================================================\n";
}

// ========== LLAMADAS AL SISTEMA UTILIZADAS (VERSION MULTIHILO) ==========
void mostrar_llamadas_sistema(int num_threads) {
    std::cout << "\n========== LLAMADAS AL SISTEMA (SYSCALLS) ==========\n";

    std::cout << "\n  Este programa PARALELO utiliza las siguientes\n";
    std::cout << "  llamadas al sistema de Windows (API del Kernel):\n";

    std::cout << "\n  +------------------------------------------------------------+\n";
    std::cout << "  | CATEGORIA        | FUNCION API           | PROPOSITO       |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    // Gestion de Procesos
    std::cout << "  | PROCESOS         | GetCurrentProcess()   | Handle propio   |\n";
    std::cout << "  |                  | GetCurrentProcessId() | PID del proceso |\n";
    std::cout << "  |                  | GetProcessTimes()     | Tiempos CPU     |\n";
    std::cout << "  |                  | GetPriorityClass()    | Prioridad       |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    // Gestion de Hilos (MAS EN PARALELO)
    std::cout << "  | HILOS            | GetCurrentThread()    | Handle del hilo |\n";
    std::cout << "  | (IMPORTANTE!)    | GetCurrentThreadId()  | TID del hilo    |\n";
    std::cout << "  |                  | GetThreadTimes()      | Tiempos por hilo|\n";
    std::cout << "  |                  | SetThreadAffinityMask | Fijar a un core |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    // Memoria
    std::cout << "  | MEMORIA          | VirtualQuery()        | Info de memoria |\n";
    std::cout << "  |                  | GetProcessMemoryInfo()| Uso de RAM      |\n";
    std::cout << "  |                  | GlobalMemoryStatusEx()| Memoria sistema |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    // Sistema
    std::cout << "  | SISTEMA          | GetSystemInfo()       | Info del CPU    |\n";
    std::cout << "  |                  | GetCurrentProcessor() | Core actual     |\n";
    std::cout << "  |                  | QueryProcessCycleTime | Ciclos CPU      |\n";
    std::cout << "  |                  | GetProcessAffinityMask| Cores permitidos|\n";
    std::cout << "  +------------------------------------------------------------+\n";

    // I/O
    std::cout << "  | ENTRADA/SALIDA   | GetStdHandle()        | Handles E/S     |\n";
    std::cout << "  |                  | GetProcessIoCounters()| Contadores I/O  |\n";
    std::cout << "  |                  | GetConsoleWindow()    | Ventana consola |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    // Modulos
    std::cout << "  | MODULOS          | GetModuleHandle()     | Handle DLL      |\n";
    std::cout << "  |                  | EnumProcessModules()  | Lista modulos   |\n";
    std::cout << "  |                  | GetModuleInformation()| Info de modulo  |\n";
    std::cout << "  +------------------------------------------------------------+\n";

    std::cout << "\n  -- Flujo de una Llamada al Sistema --\n";
    std::cout << "  \n";
    std::cout << "   MODO USUARIO                    MODO KERNEL\n";
    std::cout << "  +----------------+              +------------------+\n";
    std::cout << "  | Tu programa    |  syscall    | Kernel de Windows|\n";
    std::cout << "  | (MMP.exe)      | =========>  | (ntoskrnl.exe)   |\n";
    std::cout << "  |                |  resultado  |                  |\n";
    std::cout << "  |                | <=========  |                  |\n";
    std::cout << "  +----------------+              +------------------+\n";
    std::cout << "        |                                 |\n";
    std::cout << "        v                                 v\n";
    std::cout << "   Ring 3 (Usuario)                Ring 0 (Kernel)\n";
    std::cout << "   - Sin privilegios               - Acceso total\n";
    std::cout << "   - Memoria virtual               - Memoria fisica\n";
    std::cout << "   - CPU limitada                  - Control del HW\n";

    std::cout << "\n  -- Nota sobre Programa PARALELO (MULTIHILO) --\n";
    std::cout << "  Este programa usa " << num_threads << " HILOS de ejecucion.\n";
    std::cout << "  \n";
    std::cout << "  Mecanismos de SINCRONIZACION usados:\n";
    std::cout << "    - std::mutex          : Exclusion mutua\n";
    std::cout << "    - std::lock_guard     : RAII para locks seguros\n";
    std::cout << "    - std::atomic<bool>   : Operaciones atomicas\n";
    std::cout << "  \n";
    std::cout << "  Cada hilo puede ejecutarse en un CORE diferente,\n";
    std::cout << "  logrando PARALELISMO REAL en CPUs multicore.\n";

    std::cout << "====================================================\n";
}

#endif

// ===================== Main =====================

int main() {
    std::cout << std::unitbuf;

    int rows_a, cols_a, cols_b;

    std::cout << "=== MULTIPLICACION DE MATRICES - PARALELO (C++) ===\n\n";
    std::cout << "Filas de A: " << std::flush;                    std::cin >> rows_a;
    std::cout << "Columnas de A (= Filas de B): " << std::flush;  std::cin >> cols_a;
    std::cout << "Columnas de B: " << std::flush;                  std::cin >> cols_b;

    std::cout << "\nSemilla aleatoria: " << SEED << "\n";
    std::mt19937 rng(SEED);

    std::cout << "Generando matrices...\n";
    Matrix A = generate_matrix(rows_a, cols_a, rng);
    Matrix B = generate_matrix(cols_a, cols_b, rng);

    if (rows_a <= 10 && cols_b <= 10) {
        print_matrix(A, "A");
        print_matrix(B, "B");
    }

    // --- Configuracion de hilos ---
    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 4;
    int num_threads = std::min((int)num_cores, rows_a);

    // --- Distribuir filas entre hilos ---
    std::vector<std::pair<int, int>> distribution;
    int base = rows_a / num_threads;
    int remainder = rows_a % num_threads;
    int start = 0;
    for (int i = 0; i < num_threads; ++i) {
        int count = base + (i < remainder ? 1 : 0);
        if (count > 0) {
            distribution.push_back({start, start + count});
            start += count;
        }
    }
    num_threads = (int)distribution.size();

    std::cout << "\nCores logicos disponibles: " << num_cores << "\n";
    std::cout << "Hilos a utilizar:          " << num_threads << "\n";

    // --- Tabla de distribucion ---
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  DISTRIBUCION DEL TRABAJO\n";
    std::cout << std::string(70, '=') << "\n";
    for (int i = 0; i < num_threads; ++i) {
        auto [s, e] = distribution[i];
        std::cout << "  Hilo " << std::setw(2) << i
                  << "  |  Core " << std::setw(2) << i
                  << "  |  Filas " << std::setw(5) << s
                  << " - " << std::setw(5) << e - 1
                  << "  (" << e - s << " filas)\n";
    }
    std::cout << std::string(70, '=') << "\n";

    // --- Pre-asignar matriz resultado ---
    Matrix C(rows_a, std::vector<int>(cols_b, 0));

    // --- Crear metricas por hilo (unique_ptr porque mutex no es movible) ---
    std::vector<std::unique_ptr<ThreadMetrics>> metrics;
    for (int i = 0; i < num_threads; ++i) {
        auto m = std::make_unique<ThreadMetrics>();
        m->thread_id = i;
        m->core_id = i;
        m->row_start = distribution[i].first;
        m->row_end = distribution[i].second;
        metrics.push_back(std::move(m));
    }

    std::cout << "\nIniciando multiplicacion paralela con monitoreo...\n\n";

    // --- Lanzar hilos worker ---
    auto global_start = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads; ++i) {
        workers.emplace_back(
            worker_func,
            std::cref(A), std::cref(B), std::ref(C),
            std::ref(*metrics[i])
        );
    }

    // --- Hilo monitor: muestra metricas en tiempo real ---
    std::atomic<bool> all_done{false};

    std::thread monitor([&]() {
        // Esperar activamente a que al menos un hilo arranque
        while (!all_done.load()) {
            bool any_started = false;
            for (int i = 0; i < num_threads; ++i) {
                std::lock_guard<std::mutex> lk(metrics[i]->mtx);
                if (metrics[i]->started) { any_started = true; break; }
            }
            if (any_started) break;
            std::this_thread::yield();
        }
        // Imprimir metricas: primero inmediatamente, luego cada 50ms
        bool first_print = true;
        while (!all_done.load()) {
            if (!first_print) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (all_done.load()) break;
            }
            first_print = false;

            double mem = get_memory_mb();
            bool any_active = false;

            for (int i = 0; i < num_threads; ++i) {
                std::lock_guard<std::mutex> lk(metrics[i]->mtx);
                auto& m = *metrics[i];
                if (!m.started) continue;
                any_active = true;

                std::cout << "  [Hilo " << std::setw(2) << m.thread_id
                          << " | TID " << std::setw(6) << m.native_tid
                          << " | Core " << std::setw(2) << m.core_id << "]  "
                          << std::fixed << std::setprecision(1)
                          << "Progreso: " << std::setw(5) << m.progress << "%  |  "
                          << "CPU: " << std::setw(5) << m.cpu_pct << "%  |  "
                          << "RAM: " << std::setw(7) << mem << " MB  |  "
                          << "Filas: " << std::setw(5) << m.rows_done << "/"
                          << std::setw(5) << m.total_rows;
                if (m.done) std::cout << "  [LISTO]";
                std::cout << "\n";
            }

            if (any_active) std::cout << "\n" << std::flush;
        }
    });

    // --- Esperar a que terminen todos los workers ---
    for (auto& w : workers)
        w.join();

    auto global_end = std::chrono::steady_clock::now();
    double global_elapsed = std::chrono::duration<double>(global_end - global_start).count();

    all_done.store(true);
    monitor.join();

    // --- Resultado ---
    double final_mem = get_memory_mb();

    if (rows_a <= 10 && cols_b <= 10)
        print_matrix(C, "C = A x B");

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  RESULTADO\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << "  Dimensiones: A(" << rows_a << "x" << cols_a << ") x B("
              << cols_a << "x" << cols_b << ") = C(" << rows_a << "x" << cols_b << ")\n";
    std::cout << std::fixed << std::setprecision(6)
              << "  Tiempo total (wall clock): " << global_elapsed << " segundos\n";
    std::cout << "  Hilos utilizados:          " << num_threads << "\n";
    std::cout << std::setprecision(2)
              << "  Memoria del proceso:       " << final_mem << " MB\n";
    std::cout << std::string(70, '=') << "\n";

    // --- Metricas detalladas por hilo ---
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  METRICAS POR HILO\n";
    std::cout << std::string(70, '=') << "\n";

    double total_cpu_time = 0;
    for (int i = 0; i < num_threads; ++i) {
        std::lock_guard<std::mutex> lk(metrics[i]->mtx);
        auto& m = *metrics[i];
        auto [s, e] = distribution[i];

        double avg_cpu = 0.0, max_cpu = 0.0;
        if (!m.cpu_samples.empty()) {
            for (double c : m.cpu_samples) {
                avg_cpu += c;
                if (c > max_cpu) max_cpu = c;
            }
            avg_cpu /= m.cpu_samples.size();
        }
        total_cpu_time += m.total_time;

        std::cout << "\n  --- Hilo " << i << " (Core " << m.core_id
                  << ", TID " << m.native_tid << ") ---\n"
                  << "  Filas asignadas:  " << s << " - " << e - 1
                  << " (" << e - s << " filas)\n"
                  << std::setprecision(4)
                  << "  Tiempo ejecucion: " << m.total_time << " s\n"
                  << std::setprecision(1)
                  << "  CPU promedio:     " << avg_cpu << "%\n"
                  << "  CPU maximo:       " << max_cpu << "%\n";
    }

    // --- Resumen de paralelismo (SIEMPRE se muestra) ---
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  RESUMEN DE PARALELISMO\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::setprecision(6)
              << "  Tiempo real (wall clock):               " << global_elapsed << " s\n"
              << std::setprecision(4)
              << "  Tiempo CPU acumulado (todos los hilos): " << total_cpu_time << " s\n"
              << std::setprecision(2)
              << "  Memoria del proceso:                    " << final_mem << " MB\n";

    if (global_elapsed > 0 && total_cpu_time > 0) {
        double speedup = total_cpu_time / global_elapsed;
        std::cout << "  Speedup aproximado:                     " << speedup << "x\n";
        std::cout << "\n  Si el speedup es cercano a " << num_threads
                  << ", los hilos trabajaron\n  en paralelo de forma efectiva.\n";
    } else {
        std::cout << "\n  (La multiplicacion termino muy rapido para medir speedup.\n"
                  << "   Use matrices mas grandes como 300x300 para ver resultados.)\n";
    }
    std::cout << std::string(70, '=') << "\n";

    // ===================== INFORMACION ADICIONAL DEL PROCESO =====================
#ifdef _WIN32
    std::cout << "\n\n";
    std::cout << "######################################################################\n";
    std::cout << "#                                                                    #\n";
    std::cout << "#     INFORMACION DEL PROCESO - SISTEMAS OPERATIVOS                 #\n";
    std::cout << "#     Programa: MMP.cpp (Multiplicacion de Matrices PARALELO)       #\n";
    std::cout << "#                                                                    #\n";
    std::cout << "######################################################################\n";

    mostrar_info_programa();                      // SEGMENTO DE PROGRAMA (codigo)
    mostrar_info_pila(metrics);                   // PILA (Stack) - cada hilo tiene la suya
    mostrar_info_datos();                         // DATOS (variables, heap)
    mostrar_info_ipc(num_threads);                // IPC (comunicacion entre procesos/hilos)
    mostrar_acceso_nucleo(num_threads, metrics);  // ACCESO AL NUCLEO (kernel)
    mostrar_llamadas_sistema(num_threads);        // LLAMADAS AL SISTEMA (syscalls)
    mostrar_modulos_proceso();                    // MODULOS/DLLs cargados
#endif

    return 0;
}
