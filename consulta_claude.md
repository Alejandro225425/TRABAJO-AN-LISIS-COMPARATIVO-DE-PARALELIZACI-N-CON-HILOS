# Consultas Realizadas con Claude AI

## Contexto
Este documento registra las consultas realizadas al asistente de IA Claude para el desarrollo del proyecto de multiplicacion de matrices (secuencial vs paralelo) de la materia de Sistemas Operativos.

---

## Consulta 1: Desarrollo de los programas MMS.cpp y MMP.cpp

**Objetivo:** Crear dos programas en C++ para Windows que multipliquen matrices e informen metricas detalladas del proceso a nivel de sistema operativo.

**Requerimientos solicitados:**
1. Programa secuencial (MMS.cpp) con interfaz grafica Win32
2. Programa paralelo (MMP.cpp) con ejecucion por consola
3. Monitoreo de CPU y memoria en tiempo real
4. Informacion del proceso: pila, datos, IPC, acceso al kernel, syscalls, modulos cargados

**Resultado:** Se generaron ambos programas funcionales con las siguientes caracteristicas:

### MMS.cpp (Secuencial)
- Multiplicacion con un solo hilo
- GUI Win32 con campos de entrada y area de salida
- Monitor de CPU y memoria cada 300ms
- Reporte completo de metricas del proceso

### MMP.cpp (Paralelo)
- Multiplicacion con N hilos (uno por core logico)
- Afinidad de hilo a core con `SetThreadAffinityMask`
- Sincronizacion con `std::mutex` y `std::atomic`
- Monitor por hilo con progreso, CPU y memoria
- Calculo de speedup

---

## Consulta 2: Organizacion del proyecto

**Objetivo:** Estructurar la carpeta del proyecto para la entrega final.

**Estructura definida:**
- Codigo fuente: MMP.cpp y MMS.cpp
- Documentacion: README.md, consulta_claude.md, analisis_resultados.md
- Resultados: archivos JSON/TXT con metricas y capturas de pantalla

---

## Conceptos Clave Discutidos

### Procesos vs Hilos
- Un **proceso** tiene su propio espacio de direcciones
- Los **hilos** comparten el espacio de direcciones del proceso pero cada uno tiene su propia pila

### Mecanismos de Sincronizacion
- `std::mutex`: exclusion mutua para proteger datos compartidos
- `std::lock_guard`: RAII para adquirir/liberar locks de forma segura
- `std::atomic<bool>`: operaciones atomicas sin necesidad de mutex

### API de Windows Utilizada
| Funcion | Proposito |
|---------|-----------|
| `GetProcessMemoryInfo()` | Obtener uso de memoria RAM |
| `GetProcessTimes()` | Tiempos en modo kernel vs usuario |
| `SetThreadAffinityMask()` | Fijar hilo a un core especifico |
| `GetCurrentProcessorNumber()` | Core actual de ejecucion |
| `VirtualQuery()` | Informacion de regiones de memoria |
| `EnumProcessModules()` | Listar DLLs cargados |
| `GetProcessIoCounters()` | Contadores de operaciones I/O |

### Conceptos de Sistemas Operativos Aplicados
- **Modo kernel vs modo usuario** (Ring 0 vs Ring 3)
- **Segmentos de memoria**: codigo (.text), datos (.data), heap, pila (stack)
- **IPC**: mecanismos de comunicacion entre hilos (memoria compartida, mutex)
- **Planificacion**: afinidad de nucleos y prioridades
- **Llamadas al sistema**: transicion de modo usuario a modo kernel
