# Analisis de Resultados: Secuencial vs Paralelo

## Objetivo
Comparar el rendimiento de la multiplicacion de matrices utilizando un enfoque secuencial (MMS.cpp, 1 hilo) frente a uno paralelo (MMP.cpp, N hilos), analizando metricas del sistema operativo.

---

## Configuracion de Pruebas

| Parametro | Valor |
|-----------|-------|
| Sistema Operativo | Windows 11 |
| Compilador | g++ / MSVC con -O2 |
| Semilla aleatoria | 42 (resultados reproducibles) |
| Rango de valores | Enteros 0-9 |

### Tamanos de Matriz Probados
| Prueba | Filas A | Cols A / Filas B | Cols B |
|--------|---------|------------------|--------|
| Pequena | 5 | 5 | 5 |
| Mediana | 100 | 100 | 100 |
| Grande | 300 | 300 | 300 |
| Muy grande | 500 | 500 | 500 |

---

## Resultados

> **Nota:** Completar esta tabla con los valores obtenidos al ejecutar ambos programas.

### Tiempo de Ejecucion (segundos)

| Tamano | Secuencial (MMS) | Paralelo (MMP) | Speedup |
|--------|-------------------|----------------|---------|
| 5x5 | ___ | ___ | ___ |
| 100x100 | ___ | ___ | ___ |
| 300x300 | ___ | ___ | ___ |
| 500x500 | ___ | ___ | ___ |

### Uso de CPU

| Tamano | MMS - CPU (%) | MMP - CPU Total (%) | MMP - CPU/Hilo (%) |
|--------|---------------|---------------------|---------------------|
| 5x5 | ___ | ___ | ___ |
| 100x100 | ___ | ___ | ___ |
| 300x300 | ___ | ___ | ___ |
| 500x500 | ___ | ___ | ___ |

### Consumo de Memoria (MB)

| Tamano | MMS - RAM | MMP - RAM | Diferencia |
|--------|-----------|-----------|------------|
| 5x5 | ___ | ___ | ___ |
| 100x100 | ___ | ___ | ___ |
| 300x300 | ___ | ___ | ___ |
| 500x500 | ___ | ___ | ___ |

### Tiempos de Kernel vs Usuario

| Programa | Tiempo Kernel (s) | Tiempo Usuario (s) | % en Kernel |
|----------|-------------------|---------------------|-------------|
| MMS | ___ | ___ | ___ |
| MMP | ___ | ___ | ___ |

---

## Analisis

### 1. Speedup y Eficiencia
- **Speedup** = Tiempo_secuencial / Tiempo_paralelo
- **Eficiencia** = Speedup / Numero_de_hilos
- Un speedup cercano al numero de cores indica buen paralelismo
- La Ley de Amdahl limita el speedup maximo segun la porcion secuencial del codigo

### 2. Uso de CPU
- **MMS (secuencial):** Se espera ~100% de un solo core, mientras los demas permanecen ociosos
- **MMP (paralelo):** Se espera uso distribuido entre todos los cores asignados, con cada hilo cercano al 100% de su core

### 3. Memoria
- **MMS:** Almacena las 3 matrices (A, B, C) en un solo espacio
- **MMP:** Mismas 3 matrices compartidas + overhead por estructuras de sincronizacion (mutex, metricas por hilo) y pilas adicionales (~1 MB por hilo)

### 4. Modo Kernel vs Usuario
- **MMS:** Mayor proporcion en modo usuario (calculo puro), poco tiempo en kernel
- **MMP:** Mas tiempo en kernel debido a: creacion de hilos, SetThreadAffinityMask, operaciones de mutex, cambios de contexto

### 5. Observaciones sobre IPC
- MMS no requiere sincronizacion entre hilos
- MMP usa mutex para proteger las metricas compartidas y atomic para senalizacion
- Las matrices A y B son solo lectura (no requieren sincronizacion)
- La matriz C se escribe en regiones disjuntas (cada hilo escribe filas diferentes)

---

## Conclusiones

> Completar despues de obtener los resultados de las ejecuciones.

1. **Rendimiento:** El programa paralelo es ___ veces mas rapido que el secuencial para matrices grandes
2. **Escalabilidad:** El speedup ___ (escala bien / no escala linealmente) con el numero de cores
3. **Overhead:** La version paralela consume ___ MB mas de memoria debido a las pilas adicionales
4. **Trade-off:** Para matrices pequenas, el overhead de crear hilos y sincronizar puede hacer que la version paralela sea ___ (mas lenta / similar / apenas mas rapida)
5. **Kernel:** La version paralela pasa ___ % mas de tiempo en modo kernel debido a las llamadas al sistema para gestion de hilos

---

## Capturas de Pantalla

> Agregar capturas en la carpeta `resultados/capturas/`:
> - Ejecucion de MMS con matrices 300x300
> - Ejecucion de MMP con matrices 300x300
> - Task Manager mostrando uso de CPU durante MMS
> - Task Manager mostrando uso de CPU durante MMP
> - Comparacion visual del uso de nucleos
