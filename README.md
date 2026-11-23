# Proyecto Final Introducción a la computación Gráfica

**Editor de figuras y transformaciones 2D sobre una grilla de píxeles simulados**

| | |
|---|---|
| **Autor** | Juan Sebastián Caicedo Vásquez |
| **Programa** | Estudiante de Ingeniería Multimedia |
| **Lenguaje** | C++ (Visual Studio) |
| **Tecnologías** | OpenGL 3.3 Core, GLFW, GLAD, Dear ImGui |

---

## Descripción

Editor gráfico 2D hecho en **C++** con **OpenGL 3.3**, **GLFW**, **GLAD** y **Dear ImGui**.

La pantalla es una grilla que simula una matriz de píxeles. Las figuras **no** se delegan a OpenGL: no hay un `GL_TRIANGLES` de tres vértices por cada triángulo ni `glRotatef`. Los bordes, los rellenos y las transformaciones se calculan con algoritmos y matrices propios, y el resultado se pinta **celda por celda**. OpenGL solo se usa como pantalla: dibuja las líneas de la grilla (`GL_LINES`) y cada celda pintada como dos triángulos (`GL_TRIANGLES`).

---

## Contenido

1. [Características](#características)
2. [Requisitos](#requisitos)
3. [Compilación](#compilación-visual-studio)
4. [Uso](#uso)
5. [Arquitectura](#arquitectura)
6. [Detalles del código](#detalles-del-código)
7. [Algoritmos](#algoritmos)
8. [Transformaciones con coordenadas homogéneas](#transformaciones-con-coordenadas-homogéneas)
9. [Relleno que sigue a la figura](#relleno-que-sigue-a-la-figura)
10. [Estructura del código](#estructura-del-código)
11. [Autor](#autor)

---

## Características

- **Primitivas:** línea, círculo, triángulo y cuadrado/rectángulo.
- **Creación** con el mouse (clics sobre la grilla, con vista previa) o escribiendo coordenadas.
- **Relleno automático** opcional ("Rellenar al crear") para triángulos, rectángulos y círculos.
- **Bote de pintura (Flood Fill)** para rellenar cualquier región cerrada, incluso la formada por líneas que se cruzan.
- **Transformaciones 2D con matrices homogéneas 3x3** escritas desde cero: traslación, rotación, escala, reflexión y cizallamiento, con composición en una sola matriz.
- **Pivote configurable:** respecto al origen o respecto al centroide de la figura.
- **Escena de objetos:** lista de figuras para seleccionar, transformar o eliminar.
- **Deshacer (Undo)**, limpiar lienzo y visualizador en tiempo real de la matriz `M_total`.
- Colores editables: borde, relleno, grilla y fondo.

---

## Requisitos

| Dependencia | Uso |
|---|---|
| Visual Studio (C++11 o superior) | Compilación |
| [GLFW](https://www.glfw.org/) | Ventana y entrada de mouse/teclado |
| [GLAD](https://glad.dav1d.de/) (OpenGL 3.3 Core) | Carga de funciones de OpenGL |
| [Dear ImGui](https://github.com/ocornut/imgui) | Interfaz (requiere `SeparatorText`, es decir, ImGui 1.89 o superior) |

De ImGui hay que incluir en el proyecto los archivos del núcleo y los backends de GLFW y OpenGL3:

```
imgui.cpp  imgui_draw.cpp  imgui_tables.cpp  imgui_widgets.cpp
imgui_impl_glfw.cpp  imgui_impl_opengl3.cpp
```

## Compilación (Visual Studio)

1. Crea un proyecto de consola de C++ y agrega `main.cpp`.
2. Agrega a los *Include Directories* las carpetas de GLFW, GLAD (`glad/glad.h`) e ImGui.
3. Agrega a los archivos del proyecto `glad.c` y los `.cpp` de ImGui indicados arriba.
4. Enlaza con `glfw3.lib` y `opengl32.lib`.
5. Compila y ejecuta. Si cambiaste código mientras el programa corría, usa una recompilación completa y no la Recarga activa (*Hot Reload*), porque no admite cambios de este tamaño.

---

## Uso

### Herramientas y atajos

| Herramienta | Atajo | Cómo se usa |
|---|---|---|
| Línea | `L` | 2 clics: extremos |
| Círculo | `C` | 2 clics: centro y un punto del borde |
| Triángulo | `T` | 3 clics: los tres vértices |
| Cuadrado / rectángulo | `R` | 2 clics: esquinas opuestas (opción "Forzar cuadrado") |
| Bote de pintura | `B` | 1 clic dentro de una región cerrada |
| Seleccionar | `S` | 1 clic sobre una figura |

Otros atajos: `Ctrl+Z` deshace, `Supr` elimina la figura seleccionada, clic derecho cancela la figura en creación (o quita la selección) y `Esc` cierra el programa.

### Panel de ImGui

- **Herramienta:** selector de herramienta.
- **Colores:** borde (figuras nuevas), relleno / bote, grilla, fondo, "Rellenar al crear" y la opción de poner el origen (0,0) en el centro de la grilla.
- **Crear por coordenadas:** crea la figura de la herramienta actual a partir de celdas `(col, fila)` escritas a mano.
- **Escena:** lista de figuras, eliminar, deshacer y limpiar lienzo.
- **Figura seleccionada:** relleno y colores propios de esa figura.
- **Transformaciones 2D:** traslación, rotación (0° a 360°), escala, cizallamiento, reflexiones, "Transformar respecto al centro de la figura", resetear y la matriz 3x3 resultante en solo lectura.

---

## Arquitectura

El programa tiene tres capas que se recalculan en cada cuadro:

```
Escena (std::vector<Figura>)
        │  rasterización (Bresenham, punto medio, scanline)
        ▼
Matriz lógica de píxeles (columnas × filas)
        │  cada celda ocupada = 2 triángulos con su color
        ▼
Buffer de OpenGL (posición + color por vértice) → pantalla
```

1. **Escena:** cada `Figura` guarda su tipo, sus **vértices originales** en coordenadas de la grilla, sus colores y sus parámetros de transformación. Los vértices originales nunca se modifican.
2. **Matriz lógica:** en cada cuadro se vacía y se rasteriza cada figura: primero el relleno y encima el borde. El Flood Fill opera directamente sobre esta matriz.
3. **Dibujo:** cada celda ocupada se convierte en dos triángulos con su color y se sube a la GPU. Las líneas de la grilla se dibujan con el mismo programa de shaders, tomando el color de un `uniform`.

Como todo se recalcula desde los vértices originales y solo se redondea a la celda entera al rasterizar, **las transformaciones no acumulan error**.

### Estructuras principales

| Estructura | Función |
|---|---|
| `Figura` | Objeto de la escena: tipo, vértices originales, colores, relleno y `Transformacion` |
| `Transformacion` | Parámetros: `tx, ty, angulo, sx, sy, shx, shy, refX, refY, respectoCentro` |
| `Mat3` | Matriz 3x3 explícita (`float m[3][3]`) con producto de matrices |
| `Pixel` | Celda de la matriz lógica: ocupada o no, y su color RGBA |

---

## Detalles del código

### Ciclo principal (cada cuadro)

El bucle de `main()` ejecuta siempre este orden:

1. `glfwPollEvents()`: procesa los eventos.
2. `actualizarOrigen()`: calcula qué celda actúa como origen (0,0).
3. Lectura del teclado (atajos), solo si ImGui no está usando el teclado.
4. Construcción de la interfaz de ImGui (herramientas, colores, escena y transformaciones) y `ImGui::Render()`.
5. `actualizarPintado()`: reconstruye la matriz lógica, genera los vértices y los sube a la GPU.
6. Dibujo: limpia la pantalla, dibuja la grilla, luego las celdas pintadas y por último ImGui encima.

### Variables globales de estado

| Variable | Función |
|---|---|
| `figuras` | Lista de figuras de la escena (el orden de la lista es el orden de dibujo) |
| `figuraSeleccionada` | Índice de la figura seleccionada (-1 si no hay ninguna) |
| `herramienta` | Herramienta activa (`HERR_LINEA`, `HERR_CIRCULO`, `HERR_TRIANGULO`, `HERR_CUADRADO`, `HERR_BOTE`, `HERR_SELECCIONAR`) |
| `puntosPendientes` | Celdas ya clicadas de la figura que se está creando |
| `matriz` | Matriz lógica de píxeles (`columnas * filas`, índice `fila * columnas + col`) |
| `historial` | Pila de estados anteriores de la escena para Deshacer (máximo 200) |
| `origenCol`, `origenFila` | Celda que actúa como origen (0,0) matemático |
| `siguienteId` | Contador para dar un identificador único a cada figura |

### Funciones principales

**Matemáticas (`Mat3`)**

| Función | Descripción |
|---|---|
| `operator*` | Producto de dos matrices 3x3 |
| `mat3Traslacion`, `mat3Rotacion`, `mat3Escala`, `mat3Reflexion`, `mat3Cizalla` | Construyen cada matriz de transformación |
| `mat3Aplicar` | Aplica una matriz a un punto `(x, y, 1)` |
| `mat3InversaAfin` | Inversa de una matriz afín 2D (se usa para que el relleno siga a su figura) |

**Rasterización**

| Función | Descripción |
|---|---|
| `bresenham` | Celdas de una línea con parámetro de decisión entero |
| `circulo` | Celdas del contorno de un círculo (punto medio y simetría de octante) |
| `rellenoPoligono` | Relleno por scanline de triángulos y rectángulos |
| `rellenoCirculo` | Relleno de un círculo, fila por fila |
| `bordeFigura`, `rellenoFigura` | Calculan el borde y el interior según el tipo de figura |
| `calcularCeldasFigura` | Entrega las celdas de borde e interior de una figura ya transformada |

**Transformación de figuras**

| Función | Descripción |
|---|---|
| `centroideRelativo` | Centroide de la figura respecto al origen, calculado con los vértices originales |
| `matrizTotal` | Compone la matriz `M_total` de la figura |
| `verticesTransformados` | Aplica `M_total` a los vértices originales y redondea a la celda |
| `celdasRellenoSeguido` | Calcula el relleno del bote de una figura transformada (mapeo inverso) |

**Matriz lógica y escena**

| Función | Descripción |
|---|---|
| `reconstruirMatriz` | Vacía la matriz y rasteriza todas las figuras (relleno y luego borde) |
| `ponerPixel`, `mismoPixel` | Escriben y comparan pixeles lógicos |
| `construirFigura`, `crearFiguraDesdePuntos` | Arman una figura a partir de las celdas clicadas o escritas y la agregan a la escena |
| `rellenoBote` | Flood Fill con BFS y asignación de la figura padre |
| `seleccionarFiguraEn` | Selecciona la figura que contiene la celda clicada |
| `apilarUndo`, `deshacer`, `eliminarSeleccionada`, `limpiarLienzo` | Gestión de la escena y del historial |

**Entrada y dibujo**

| Función | Descripción |
|---|---|
| `mousePosACelda` | Convierte la posición del mouse a celda de la grilla |
| `mouse_button_callback` | Maneja los clics según la herramienta activa |
| `generarGrilla`, `framebuffer_size_callback` | Generan la grilla y la recalculan al redimensionar la ventana |
| `pintarCelda` | Agrega una celda al buffer como 2 triángulos con color |
| `actualizarPintado` | Vuelca la matriz, la vista previa y los marcadores a la GPU |

### Dibujo en OpenGL

- **Formato de vértice del pintado:** 7 floats por vértice (`x, y, z, r, g, b, a`), con dos atributos: posición (`location = 0`) y color (`location = 1`).
- **Shaders:** el vertex shader recibe posición y color. El fragment shader elige el color según el `uniform int uPorVertice`: con 0 usa el `uniform vec4 uColor` (grilla) y con 1 usa el color de cada vértice (celdas pintadas).
- **Mezcla (blending):** activada para mostrar la vista previa de la figura semitransparente.
- **Interfaz sobre la grilla:** el *callback* del mouse y los atajos comprueban `WantCaptureMouse` y `WantCaptureKeyboard` de ImGui, para no dibujar en la grilla cuando se usa el panel.

### Creación de figuras

Cada clic con el mouse agrega una celda a `puntosPendientes`. Cuando se reúnen los puntos que necesita la herramienta (`puntosNecesarios`), `crearFiguraDesdePuntos` construye la figura y la agrega a la escena. Mientras faltan puntos, `actualizarPintado` dibuja una vista previa con la celda bajo el mouse como punto provisional. El cuadrado guarda sus 4 esquinas, y no solo 2, para poder rotarlo y cizallarlo.

### Deshacer (Undo)

Antes de cada cambio en la escena (crear, eliminar, limpiar, rellenar, editar una transformación o un color) se guarda una copia completa de `figuras` en `historial`. En los controles de ImGui, la copia se toma antes de dibujar los widgets y se apila cuando uno empieza a editarse, para que un solo arrastre del slider sea un solo paso de Deshacer.

---

## Algoritmos

| Algoritmo | Uso |
|---|---|
| **Bresenham** (parámetro de decisión entero) | Líneas y aristas de triángulos y rectángulos |
| **Punto medio** con simetría de octante | Contorno del círculo |
| **Scanline** (regla par-impar, intervalos semiabiertos) | Relleno de triángulos, rectángulos (incluso rotados o cizallados) y círculos |
| **Flood Fill con BFS y cola** (4-conectado) | Bote de pintura. No usa recursión, así que no desborda la pila |

Como los bordes de Bresenham son 8-conectados y el relleno avanza solo en 4 direcciones, el relleno no se escapa por las esquinas diagonales.

---

## Transformaciones con coordenadas homogéneas

Cada punto es el vector `(x, y, 1)ᵀ`. Las matrices implementadas son:

```
Traslación          Rotación                 Escala
| 1  0  Tx |        | cosθ  -sinθ  0 |       | Sx  0   0 |
| 0  1  Ty |        | sinθ   cosθ  0 |       | 0   Sy  0 |
| 0  0  1  |        | 0      0     1 |       | 0   0   1 |

Cizallamiento       Reflexión eje X          Reflexión eje Y
| 1   Shy  0 |      | 1   0  0 |             | -1  0  0 |
| Shx  1   0 |      | 0  -1  0 |             |  0  1  0 |
| 0    0   1 |      | 0   0  1 |             |  0  0  1 |
```

La reflexión respecto al origen es la combinación de las dos anteriores. La matriz total de cada figura es:

```
M_total = T(tx, ty) · T(c) · R(θ) · Sh · S · Ref · T(-c)
```

donde `c` es el centroide de la figura (si está marcada la opción "Transformar respecto al centro de la figura") o el origen `(0, 0)` en caso contrario. Con el centroide como pivote, la figura rota sobre sí misma y no sale disparada de la grilla. Se aplica a los vértices originales y el resultado se redondea a la celda entera solo al rasterizar.

### Convenciones

- **Coordenadas:** la celda `(0, 0)` es la esquina **inferior izquierda**. Las matrices trabajan en coordenadas relativas al origen. Por defecto el origen es esa celda; con el checkbox correspondiente se mueve al centro de la grilla (marcado con un cuadrito amarillo). Con el origen en la esquina, reflejar o rotar respecto al origen manda la figura fuera de la pantalla.
- **Cizallamiento:** se usa la matriz `[1 Shy 0; Shx 1 0]`, es decir, `x' = x + Shy·y` y `y' = Shx·x + y`.
- **Círculo:** guarda su centro y un punto del borde. Su radio sale de la distancia entre esos dos puntos ya transformados, así que se mantiene circular aunque la transformación sea no uniforme.
- **Marcadores:** al seleccionar una figura se marcan sus celdas de borde en blanco y su pivote en magenta.

---

## Relleno que sigue a la figura

El relleno hecho con el bote queda asociado a la figura más interna que encierra por completo la región. Para transformarlo se recorre el interior de la figura ya transformada y, con la **inversa** de `M_total`, se comprueba si cada celda estaba rellena en el espacio original. Así el relleno rota, escala, se refleja y se cizalla con la figura sin dejar huecos.

Limitaciones:

- Si la región la forman varias figuras o líneas cruzadas, o no está cerrada, el relleno queda **fijo** y no se transforma.
- Para transformar un relleno asociado hay que seleccionar su figura, no el relleno.
- Al borrar una figura se borran también los rellenos que dependían de ella.
- Con rotaciones que no son múltiplos de 90° puede aparecer ruido de una celda en el borde del relleno, por el redondeo a la grilla.

---

## Estructura del código

`main.cpp` conserva la arquitectura base y sigue este orden:

1. Variables globales, tipos de datos y estado de la escena
2. Matemáticas: `Mat3` y sus funciones
3. Grilla y *callback* de redimensionado
4. `pintarCelda`, `bresenham` y `circulo`
5. Rasterización: rellenos, bordes y transformación de vértices
6. Matriz lógica de píxeles
7. Operaciones de la escena: deshacer, crear figuras, bote de pintura y selección
8. Entrada de mouse
9. `actualizarPintado` (volcado de la matriz y marcadores a la GPU)
10. Shaders y `main` (bucle principal e interfaz de ImGui)

---

## Video explicativo

Video donde se explica la arquitectura del código, los algoritmos y se muestra el programa funcionando:

[![Ver el video explicativo en YouTube](https://img.youtube.com/vi/al7lnwVWVMw/0.jpg)](https://www.youtube.com/watch?v=al7lnwVWVMw)

▶️ [Ver en YouTube](https://www.youtube.com/watch?v=al7lnwVWVMw)

---

## Notas

- Si cambias el tamaño de la ventana, la grilla se recalcula. Las figuras conservan sus vértices, pero si el origen está en el centro, su posición relativa a él cambia.
- Al cambiar el color de borde o de relleno en el panel de colores solo se afectan las figuras **nuevas** y los rellenos nuevos del bote. Cada figura existente conserva sus propios colores, que se editan desde "Figura seleccionada".

---

## Autor

**Juan Sebastián Caicedo Vásquez**
Estudiante de Ingeniería Multimedia
