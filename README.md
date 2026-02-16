<<<<<<< HEAD
# TRABAJO-AN-LISIS-COMPARATIVO-DE-PARALELIZACI-N-CON-HILOS
=======
# Multiplicacion de Matrices - Secuencial vs Paralelo

Proyecto de Sistemas Operativos que implementa la multiplicacion de matrices en C++ con dos enfoques: **secuencial** (un solo hilo) y **paralelo** (multiples hilos/cores), permitiendo comparar el rendimiento y analizar metricas del proceso a nivel de sistema operativo.

## Estructura del Proyecto

```
multiprocesos/
├── MMP.cpp                     # Multiplicacion paralela (multihilo)
├── MMS.cpp                     # Multiplicacion secuencial (un hilo, con GUI)
├── README.md                   # Este archivo
├── consulta_claude.md          # Consultas realizadas con Claude AI
├── analisis_resultados.md      # Analisis comparativo de resultados
├── resultados/
│   ├── metricas_secuencial.json    # Metricas de ejecucion secuencial
│   ├── metricas_paralelo.json      # Metricas de ejecucion paralela
│   ├── comparacion_resultados.txt  # Resumen comparativo
│   └── capturas/                   # Capturas de pantalla del sistema
└──
```

## Codigo Fuente

### MMS.cpp - Multiplicacion Secuencial
- Usa **un solo hilo** de ejecucion
- Interfaz grafica con Win32 API
- Monitor de CPU y memoria en tiempo real
- Muestra informacion detallada del proceso (pila, datos, IPC, kernel, syscalls, modulos)

### MMP.cpp - Multiplicacion Paralela
- Usa **multiples hilos** (uno por core logico disponible)
- Cada hilo se fija a un core especifico con `SetThreadAffinityMask`
- Distribucion equitativa de filas entre hilos
- Monitor en tiempo real con metricas por hilo
- Sincronizacion con `std::mutex` y `std::atomic`
- Muestra informacion detallada del proceso incluyendo analisis de paralelismo

## Compilacion

### Con MSVC (Visual Studio)
```bash
# Secuencial (con GUI)
cl /O2 /EHsc MMS.cpp /link psapi.lib user32.lib gdi32.lib

# Paralelo
cl /O2 /EHsc MMP.cpp /link psapi.lib
```

### Con g++ (MinGW)
```bash
# Secuencial (con GUI)
g++ -O2 -std=c++17 -o MMS.exe MMS.cpp -lpsapi -lgdi32 -luser32 -mwindows

# Paralelo
g++ -O2 -std=c++17 -o MMP.exe MMP.cpp -lpsapi
```

## Ejecucion

### MMS.exe (Secuencial)
Se abre una ventana grafica donde se ingresan las dimensiones de las matrices y se presiona "Ejecutar".

### MMP.exe (Paralelo)
Se ejecuta desde la terminal:
```
MMP.exe
```
Se ingresan las dimensiones por consola (ejemplo: 300 300 300 para matrices 300x300).

## Metricas Reportadas

Ambos programas reportan:
- Tiempo de ejecucion (wall clock)
- Uso de CPU (modo kernel vs modo usuario)
- Consumo de memoria RAM (Working Set, Private Bytes)
- Informacion de la pila (stack)
- Handles abiertos (IPC)
- Modulos/DLLs cargados
- Llamadas al sistema (syscalls) utilizadas
- Afinidad de nucleos y prioridad del proceso

Adicionalmente, MMP.cpp reporta:
- Metricas individuales por hilo
- Speedup obtenido vs ejecucion secuencial
- Distribucion de trabajo entre cores

## Requisitos
- Windows 10/11
- Compilador C++17 (MSVC o g++)
- Librerias del sistema: psapi.lib (ambos), user32.lib y gdi32.lib (solo MMS)
>>>>>>> 5ddd857 (Subir Proyecto)
