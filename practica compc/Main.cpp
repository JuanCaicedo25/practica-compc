#include"imgui.h"
#include"imgui_impl_glfw.h"
#include"imgui_impl_opengl3.h"

#include <glad/glad.h> // Incluir la libreria que carga las funciones de OpenGL
#include <GLFW/glfw3.h> // Incluir la libreria para crear la ventana y leer el mouse
#include <iostream> // Incluir la libreria para mostrar mensajes en consola
#include <vector> // Incluir la libreria para usar listas dinamicas
#include <queue> // Incluir la libreria para usar la cola del Flood Fill (BFS)
#include <set> // Incluir la libreria para usar conjuntos de celdas
#include <algorithm> // Incluir la libreria para usar std::min, std::max y std::sort
#include <cmath> // Incluir la libreria para usar sin, cos y floor
#include <cstdlib> // Incluir la libreria para usar std::abs con enteros
#include <cstdio> // Incluir la libreria para usar snprintf

/*
=====================================================================================
 RESUMEN DE LA ARQUITECTURA 
-------------------------------------------------------------------------------------
 - La pantalla sigue siendo una grilla de celdas (columnas x filas). La celda (0,0) es
   la esquina INFERIOR IZQUIERDA (igual que en el codigo base).
 - Cada figura (linea, circulo, triangulo, rectangulo) se guarda como un objeto con sus
   VERTICES ORIGINALES en coordenadas de la grilla. Nunca se modifican al transformar.
 - Cada cuadro: matriz logica (columnas*filas) -> se vacia -> se rasteriza cada figura
   (relleno por scanline + borde con Bresenham / punto medio) -> se vuelca a la GPU como
   2 triangulos por celda ocupada (la misma idea de pintarCelda del codigo base).
 - La transformacion se calcula SIEMPRE desde los vertices originales:
       M_total = T(tx,ty) * T(c) * R * Sh * S * Ref * T(-c)
   y solo se redondea al entero de la grilla al rasterizar, asi no hay error acumulado.
 - Las matrices trabajan en coordenadas relativas al ORIGEN (0,0) del sistema de
   coordenadas. Por defecto el origen es la celda (0,0) (esquina inferior izquierda);
   con el checkbox de la interfaz se puede mover al centro de la grilla.
 - El Bote de Pintura (Flood Fill) usa BFS con cola sobre la matriz logica.
 - Atajos: L linea, C circulo, T triangulo, R cuadrado/rectangulo, B bote, S seleccionar,
   Ctrl+Z deshacer, Supr eliminar figura seleccionada, clic derecho cancela, Esc sale.
=====================================================================================
*/

// --- DIMENSIONES DE LA VENTANA ---
int anchoVentana = 800; // Establecer el ancho inicial de la ventana
int altoVentana = 600; // Establecer el alto inicial de la ventana

// --- CONFIGURACION DE LA GRILLA ---
const int CELDA_PIXELES = 16; // Establecer el tamano de cada celda en pixeles

int columnas = 1; // Guardar la cantidad de columnas
int filas = 1; // Guardar la cantidad de filas

// Color de la grilla
float colorGrilla[4] = { 0.05f, 0.05f, 0.15f, 1.0f }; // Establecer el color de las lineas de la grilla

// Color del BORDE de las figuras nuevas (naranja)
float colorLinea[4] = { 1.0f, 0.55f, 0.0f, 1.0f }; // Establecer el color de los trazos

// Color de RELLENO (figuras nuevas y bote de pintura), independiente del borde
float colorRelleno[4] = { 0.20f, 0.70f, 1.0f, 1.0f }; // Establecer el color de relleno

// ===== [IMGUI] Color de fondo editable desde la barra de herramientas =====
float colorFondo[4] = { 0.05f, 0.05f, 0.15f, 1.0f }; // Establecer el color de fondo
// ===== [FIN IMGUI] =====

// --- BUFFERS DE LA GRILLA ---
GLuint gridVAO, gridVBO; // Declarar los identificadores de los buffers de la grilla
std::vector<float> gridVertices; // Guardar los vertices de la grilla

// --- BUFFERS DE LAS CELDAS PINTADAS ---
// Cada vertice ahora lleva 7 floats: x, y, z, r, g, b, a (cada celda puede tener su propio color)
GLuint paintVAO, paintVBO; // Declarar los identificadores de los buffers del pintado
std::vector<float> paintVertices; // Guardar los vertices de las celdas pintadas

// ============================== TIPOS DE DATOS ==============================

typedef std::pair<int, int> Celda; // Guardar una celda (columna, fila) de la grilla

// Una celda de la matriz logica de pixeles
struct Pixel { // Declarar la estructura del pixel logico
    bool ocupado = false; // Indicar si la celda esta pintada
    float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Guardar el color RGBA de la celda
};

std::vector<Pixel> matriz; // Matriz logica de pixeles (columnas * filas), indice = fila * columnas + col

// Matriz 3x3 explicita para coordenadas homogeneas 2D (x, y, 1)^T
struct Mat3 { // Declarar la estructura de matriz 3x3
    float m[3][3]; // Guardar los 9 valores de la matriz (m[fila][columna])
};

// Tipos de figura que puede contener la escena
enum TipoFigura { FIG_LINEA, FIG_CIRCULO, FIG_TRIANGULO, FIG_RECTANGULO, FIG_RELLENO };

// Parametros de transformacion de una figura (la matriz se construye a partir de ellos)
struct Transformacion { // Declarar la estructura de transformacion
    float tx = 0.0f, ty = 0.0f; // Traslacion
    float angulo = 0.0f; // Rotacion en grados
    float sx = 1.0f, sy = 1.0f; // Escalamiento
    float shx = 0.0f, shy = 0.0f; // Cizallamiento
    bool refX = false; // Reflexion respecto al eje X (y -> -y)
    bool refY = false; // Reflexion respecto al eje Y (x -> -x)
    bool respectoCentro = true; // Usar el centroide de la figura como pivote
};

// Una figura de la escena
struct Figura { // Declarar la estructura de figura
    TipoFigura tipo = FIG_LINEA; // Tipo de figura
    std::vector<Celda> vertices; // Vertices ORIGINALES en coordenadas de la grilla
    std::vector<Celda> celdasFijas; // Solo para FIG_RELLENO: celdas rellenadas por el bote (en espacio ORIGINAL de la figura padre, o absolutas si no tiene padre)
    int id = 0; // Identificador unico de la figura
    int idPadre = -1; // Solo para FIG_RELLENO: id de la figura que lo contiene (-1 = relleno fijo)
    float colBorde[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Color del borde
    float colRelleno[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Color del relleno
    bool relleno = false; // Indicar si la figura se rellena automaticamente
    Transformacion transf; // Parametros de transformacion
};

// Herramientas disponibles
enum Herramienta { HERR_LINEA, HERR_CIRCULO, HERR_TRIANGULO, HERR_CUADRADO, HERR_BOTE, HERR_SELECCIONAR };

// ============================== ESTADO DE LA ESCENA ==============================

std::vector<Figura> figuras; // Lista de figuras de la escena (orden de dibujo = orden de la lista)
std::vector<std::vector<Figura>> historial; // Pila de estados anteriores para Deshacer
int figuraSeleccionada = -1; // Indice de la figura seleccionada (-1 = ninguna)
int siguienteId = 1; // Contador para asignar identificadores unicos a las figuras

Herramienta herramienta = HERR_LINEA; // Herramienta activa
std::vector<Celda> puntosPendientes; // Celdas clicadas de la figura que se esta creando
bool rellenarAlCrear = false; // Rellenar automaticamente al crear
bool forzarCuadrado = false; // Forzar lados iguales con la herramienta cuadrado

bool origenCentro = false; // Falso: origen en la celda (0,0). Verdadero: origen en el centro de la grilla
int origenCol = 0, origenFila = 0; // Celda que actua como origen (0,0) matematico

// ============================== MATEMATICAS: MAT3 ==============================

// Multiplicar dos matrices 3x3 (resultado = a * b)
Mat3 operator*(const Mat3& a, const Mat3& b) { // Declarar el producto de matrices
    Mat3 c; // Crear la matriz resultado
    for (int i = 0; i < 3; i++) { // Recorrer las filas
        for (int j = 0; j < 3; j++) { // Recorrer las columnas
            c.m[i][j] = 0.0f; // Iniciar el acumulador
            for (int k = 0; k < 3; k++) c.m[i][j] += a.m[i][k] * b.m[k][j]; // Producto fila por columna
        }
    }
    return c; // Retornar el producto
}

Mat3 mat3Identidad() { // Crear la matriz identidad
    Mat3 r = { { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } } };
    return r;
}

Mat3 mat3Traslacion(float tx, float ty) { // Matriz de traslacion
    Mat3 r = { { { 1, 0, tx },{ 0, 1, ty },{ 0, 0, 1 } } };
    return r;
}

Mat3 mat3Rotacion(float rad) { // Matriz de rotacion libre (radianes, sentido antihorario)
    float c = std::cos(rad), s = std::sin(rad);
    Mat3 r = { { { c, -s, 0 },{ s, c, 0 },{ 0, 0, 1 } } };
    return r;
}

Mat3 mat3Escala(float sx, float sy) { // Matriz de escalamiento
    Mat3 r = { { { sx, 0, 0 },{ 0, sy, 0 },{ 0, 0, 1 } } };
    return r;
}

Mat3 mat3Reflexion(bool ejeX, bool ejeY) { // Matriz de reflexion (ejeX: y -> -y, ejeY: x -> -x, ambos = origen)
    float a = ejeY ? -1.0f : 1.0f; // Factor aplicado a x
    float d = ejeX ? -1.0f : 1.0f; // Factor aplicado a y
    Mat3 r = { { { a, 0, 0 },{ 0, d, 0 },{ 0, 0, 1 } } };
    return r;
}

Mat3 mat3Cizalla(float shx, float shy) { // Matriz de cizallamiento: x' = x + Shy*y ; y' = Shx*x + y
    Mat3 r = { { { 1, shy, 0 },{ shx, 1, 0 },{ 0, 0, 1 } } };
    return r;
}

// Aplicar una matriz a un punto (x, y, 1)^T
void mat3Aplicar(const Mat3& M, float x, float y, float& xr, float& yr) { // Declarar la funcion
    xr = M.m[0][0] * x + M.m[0][1] * y + M.m[0][2]; // Calcular la nueva x
    yr = M.m[1][0] * x + M.m[1][1] * y + M.m[1][2]; // Calcular la nueva y
}

// Inversa de una matriz afin 2D (ultima fila 0 0 1). Retorna false si es singular (por ejemplo escala 0)
bool mat3InversaAfin(const Mat3& M, Mat3& R) { // Declarar la funcion
    float det = M.m[0][0] * M.m[1][1] - M.m[0][1] * M.m[1][0]; // Determinante de la parte lineal 2x2
    if (std::fabs(det) < 1e-6f) return false; // No se puede invertir
    float a = M.m[1][1] / det, b = -M.m[0][1] / det; // Primera fila de la inversa lineal
    float c = -M.m[1][0] / det, d = M.m[0][0] / det; // Segunda fila de la inversa lineal
    float tx = -(a * M.m[0][2] + b * M.m[1][2]); // Traslacion inversa en X
    float ty = -(c * M.m[0][2] + d * M.m[1][2]); // Traslacion inversa en Y
    Mat3 r = { { { a, b, tx },{ c, d, ty },{ 0, 0, 1 } } }; // Armar la inversa
    R = r; // Entregar el resultado
    return true; // Inversion correcta
}

// --- ACTUALIZAR LA CELDA QUE ACTUA COMO ORIGEN (0,0) ---
void actualizarOrigen() { // Declarar la funcion
    origenCol = origenCentro ? columnas / 2 : 0; // Calcular la columna del origen
    origenFila = origenCentro ? filas / 2 : 0; // Calcular la fila del origen
}

// --- GENERAR LOS VERTICES DE LA GRILLA SEGUN EL TAMANO ACTUAL DE LA VENTANA ---
void generarGrilla() { // Declarar la funcion que genera la grilla
    columnas = anchoVentana / CELDA_PIXELES; // Calcular la cantidad de columnas
    filas = altoVentana / CELDA_PIXELES; // Calcular la cantidad de filas
    if (columnas < 1) columnas = 1; // Establecer al menos 1 columna para evitar division por cero
    if (filas < 1) filas = 1; // Establecer al menos 1 fila para evitar division por cero

    gridVertices.clear(); // Borrar la grilla vieja antes de recalcular la nueva

    float pasoX = 2.0f / columnas; // Calcular el ancho de una celda en coordenadas OpenGL
    float pasoY = 2.0f / filas; // Calcular el alto de una celda en coordenadas OpenGL

    for (int i = 0; i <= columnas; i++) { // Recorrer todas las lineas verticales
        float x = -1.0f + i * pasoX; // Calcular la posicion X de la linea
        gridVertices.push_back(x); gridVertices.push_back(-1.0f); gridVertices.push_back(0.0f); // Guardar el punto inferior de la linea
        gridVertices.push_back(x); gridVertices.push_back(1.0f);  gridVertices.push_back(0.0f); // Guardar el punto superior de la linea
    } // Cerrar el ciclo de lineas verticales

    for (int j = 0; j <= filas; j++) { // Recorrer todas las lineas horizontales
        float y = -1.0f + j * pasoY; // Calcular la posicion Y de la linea
        gridVertices.push_back(-1.0f); gridVertices.push_back(y); gridVertices.push_back(0.0f); // Guardar el punto izquierdo de la linea
        gridVertices.push_back(1.0f);  gridVertices.push_back(y); gridVertices.push_back(0.0f); // Guardar el punto derecho de la linea
    } // Cerrar el ciclo de lineas horizontales

    glBindBuffer(GL_ARRAY_BUFFER, gridVBO); // Activar el buffer de la grilla
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW); // Subir los vertices a la GPU
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Desactivar el buffer
} // Cerrar la funcion generarGrilla

// Funcion para ajustar el lienzo y la grilla cuando cambia el tamano de la ventana
void framebuffer_size_callback(GLFWwindow* window, int width, int height) { // Declarar la funcion de cambio de tamano
    glViewport(0, 0, width, height); // Indicarle a OpenGL el nuevo tamano del area de dibujo

    anchoVentana = width;   // Actualizar el ancho global
    altoVentana = height; // Actualizar el alto global

    generarGrilla(); // Recalcular y volver a subir la grilla con la nueva cantidad de celdas
} // Cerrar la funcion framebuffer_size_callback

// --- AGREGAR UNA CELDA (col, fila) AL BUFFER DE PINTADO COMO UN CUADRADO SOLIDO (2 TRIANGULOS) ---
// escala = 1.0 cubre toda la celda; valores menores dibujan un cuadrito centrado (marcadores)
void pintarCelda(int col, int fila, const float* color, float escala) { // Declarar la funcion que pinta una celda
    if (col < 0 || col >= columnas || fila < 0 || fila >= filas) return; // Salir si la celda esta fuera de la grilla

    float pasoX = 2.0f / columnas; // Calcular el ancho de una celda en coordenadas OpenGL
    float pasoY = 2.0f / filas; // Calcular el alto de una celda en coordenadas OpenGL
    float margenX = pasoX * (1.0f - escala) * 0.5f; // Calcular el margen horizontal del cuadrito
    float margenY = pasoY * (1.0f - escala) * 0.5f; // Calcular el margen vertical del cuadrito

    float x0 = -1.0f + col * pasoX + margenX; // Calcular el borde izquierdo de la celda
    float y0 = -1.0f + fila * pasoY + margenY; // Calcular el borde inferior de la celda
    float x1 = -1.0f + (col + 1) * pasoX - margenX; // Calcular el borde derecho de la celda
    float y1 = -1.0f + (fila + 1) * pasoY - margenY; // Calcular el borde superior de la celda

    float v[6][2] = { { x0, y0 },{ x1, y0 },{ x1, y1 },{ x0, y0 },{ x1, y1 },{ x0, y1 } }; // Los 6 vertices de los 2 triangulos
    for (int i = 0; i < 6; i++) { // Recorrer cada vertice
        paintVertices.push_back(v[i][0]); paintVertices.push_back(v[i][1]); paintVertices.push_back(0.0f); // Guardar la posicion
        paintVertices.push_back(color[0]); paintVertices.push_back(color[1]); // Guardar el color (r, g)
        paintVertices.push_back(color[2]); paintVertices.push_back(color[3]); // Guardar el color (b, a)
    }
} // Cerrar la funcion pintarCelda

// ALGORITMO DE BRESENHAM (parametro de decision entero)
std::vector<std::pair<int, int>> bresenham(int x0, int y0, int x1, int y1) { // Declarar la funcion de Bresenham
    std::vector<std::pair<int, int>> puntos; // Crear la lista de celdas de la linea

    int dx, dy; // Declarar las distancias en X y en Y
    int sx, sy; // Declarar las direcciones en X y en Y (1 o -1)
    int p;      // Declarar el parametro de decision

    if (x0 < x1) { dx = x1 - x0; sx = 1; }   // La linea va hacia la derecha
    else { dx = x0 - x1; sx = -1; }          // La linea va hacia la izquierda (o es vertical)

    if (y0 < y1) { dy = y1 - y0; sy = 1; }   // La linea va hacia arriba
    else { dy = y0 - y1; sy = -1; }          // La linea va hacia abajo (o es horizontal)

    if (dx >= dy) { // La linea es mas horizontal que vertical
        p = 2 * dy - dx; // Calcular el parametro de decision inicial
        while (true) { // Recorrer la linea avanzando siempre en X
            puntos.push_back({ x0, y0 }); // Guardar la celda actual
            if (x0 == x1) break; // Terminar al llegar al punto final
            if (p >= 0) { // Hay que subir (o bajar) en Y
                y0 += sy; // Avanzar en Y
                p += 2 * (dy - dx); // Actualizar el parametro de decision
            }
            else { // Mantener la misma Y
                p += 2 * dy; // Actualizar el parametro de decision
            }
            x0 += sx; // Avanzar en X
        }
    }
    else { // La linea es mas vertical que horizontal
        p = 2 * dx - dy; // Calcular el parametro de decision inicial
        while (true) { // Recorrer la linea avanzando siempre en Y
            puntos.push_back({ x0, y0 }); // Guardar la celda actual
            if (y0 == y1) break; // Terminar al llegar al punto final
            if (p >= 0) { // Hay que mover en X
                x0 += sx; // Avanzar en X
                p += 2 * (dx - dy); // Actualizar el parametro de decision
            }
            else { // Mantener la misma X
                p += 2 * dx; // Actualizar el parametro de decision
            }
            y0 += sy; // Avanzar en Y
        }
    }

    return puntos; // Retornar la lista de celdas de la linea
} // Cerrar la funcion bresenham

// ALGORITMO DE CIRCUNFERENCIA (punto medio)
std::vector<std::pair<int, int>> circulo(int xc, int yc, int r) { // Declarar la funcion del circulo
    std::vector<std::pair<int, int>> puntos; // Crear la lista de celdas del circulo

    int x = 0; // Inicializar la coordenada x en el tope del circulo
    int y = r; // Inicializar la coordenada y con el radio
    int p = 1 - r; // Calcular el parametro de decision inicial

    while (x <= y) { // Recorrer unicamente un octante
        puntos.push_back({ xc + x, yc + y });
        puntos.push_back({ xc - x, yc + y });
        puntos.push_back({ xc + x, yc - y });
        puntos.push_back({ xc - x, yc - y });
        puntos.push_back({ xc + y, yc + x });
        puntos.push_back({ xc - y, yc + x });
        puntos.push_back({ xc + y, yc - x });
        puntos.push_back({ xc - y, yc - x });

        x++; // Incrementar la coordenada x
        if (p < 0) { // El punto medio esta dentro del circulo
            p = p + 2 * x + 1;
        }
        else { // El punto medio esta fuera del circulo
            y--;
            p = p + 2 * (x - y) + 1;
        }
    }

    return puntos; // Retornar la lista de celdas del circulo
} // Cerrar la funcion circulo

// ============================== RASTERIZACION DE FIGURAS ==============================

int redondear(float v) { // Redondear un real al entero mas cercano (celda de la grilla)
    return (int)std::floor(v + 0.5f); // Sumar 0.5 y truncar hacia abajo
}

int raizEntera(int n) { // Raiz cuadrada entera sin usar sqrt (igual que en el codigo base)
    int r = 0; // Inicializar la raiz
    while ((r + 1) * (r + 1) <= n) r++; // Aumentar mientras el cuadrado siga cabiendo
    return r; // Retornar la raiz entera
}

void copiarColor(float* destino, const float* origen) { // Copiar un color RGBA
    for (int i = 0; i < 4; i++) destino[i] = origen[i]; // Copiar los 4 canales
}

// Relleno de un circulo por scanline: para cada fila se rellena de -w a +w
std::vector<Celda> rellenoCirculo(int xc, int yc, int r) { // Declarar la funcion
    std::vector<Celda> celdas; // Lista de celdas del interior
    for (int dy = -r; dy <= r; dy++) { // Recorrer cada fila del circulo
        int y = yc + dy; // Calcular la fila real
        if (y < 0 || y >= filas) continue; // Saltar filas fuera de la grilla
        int w = raizEntera(r * r - dy * dy); // Calcular el semiancho de la fila
        int xa = std::max(xc - w, 0); // Recortar el extremo izquierdo a la grilla
        int xb = std::min(xc + w, columnas - 1); // Recortar el extremo derecho a la grilla
        for (int x = xa; x <= xb; x++) celdas.push_back({ x, y }); // Guardar las celdas de la fila
    }
    return celdas; // Retornar el interior
}

// Relleno de un poligono (triangulo, rectangulo rotado, etc.) por scanline con regla par-impar
std::vector<Celda> rellenoPoligono(const std::vector<Celda>& v) { // Declarar la funcion
    std::vector<Celda> celdas; // Lista de celdas del interior
    int n = (int)v.size(); // Cantidad de vertices
    if (n < 3) return celdas; // Un poligono necesita al menos 3 vertices

    int minY = v[0].second, maxY = v[0].second; // Inicializar los limites verticales
    for (int i = 1; i < n; i++) { // Recorrer los vertices
        minY = std::min(minY, v[i].second); // Actualizar el minimo
        maxY = std::max(maxY, v[i].second); // Actualizar el maximo
    }
    minY = std::max(minY, 0); // Recortar a la grilla
    maxY = std::min(maxY, filas - 1); // Recortar a la grilla

    for (int y = minY; y <= maxY; y++) { // Recorrer cada scanline
        std::vector<float> cortes; // Guardar las X donde las aristas cortan la scanline
        for (int i = 0; i < n; i++) { // Recorrer las aristas
            Celda a = v[i]; // Extremo inicial de la arista
            Celda b = v[(i + 1) % n]; // Extremo final de la arista
            if (a.second == b.second) continue; // Ignorar aristas horizontales
            int yBajo = std::min(a.second, b.second); // Y menor de la arista
            int yAlto = std::max(a.second, b.second); // Y mayor de la arista
            if (y < yBajo || y >= yAlto) continue; // Intervalo semiabierto para no contar vertices dos veces
            float t = (float)(y - a.second) / (float)(b.second - a.second); // Parametro de interpolacion
            cortes.push_back(a.first + t * (b.first - a.first)); // Guardar la X del corte
        }
        std::sort(cortes.begin(), cortes.end()); // Ordenar los cortes de izquierda a derecha
        for (size_t k = 0; k + 1 < cortes.size(); k += 2) { // Rellenar entre pares de cortes
            int xa = std::max(redondear(cortes[k]), 0); // Extremo izquierdo recortado
            int xb = std::min(redondear(cortes[k + 1]), columnas - 1); // Extremo derecho recortado
            for (int x = xa; x <= xb; x++) celdas.push_back({ x, y }); // Guardar las celdas del tramo
        }
    }
    return celdas; // Retornar el interior
}

// Calcular el centroide de la figura (en coordenadas RELATIVAS al origen) usando los vertices originales
void centroideRelativo(const Figura& f, float& cx, float& cy) { // Declarar la funcion
    cx = 0.0f; cy = 0.0f; // Inicializar el centroide
    if (f.vertices.empty()) return; // Salir si no hay vertices
    if (f.tipo == FIG_CIRCULO) { // En el circulo el centroide es el centro (vertice 0)
        cx = (float)(f.vertices[0].first - origenCol); // Usar la X del centro
        cy = (float)(f.vertices[0].second - origenFila); // Usar la Y del centro
        return; // Terminar
    }
    for (size_t i = 0; i < f.vertices.size(); i++) { // Promediar los vertices
        cx += (float)(f.vertices[i].first - origenCol); // Sumar X relativa
        cy += (float)(f.vertices[i].second - origenFila); // Sumar Y relativa
    }
    cx /= (float)f.vertices.size(); // Calcular el promedio en X
    cy /= (float)f.vertices.size(); // Calcular el promedio en Y
}

// Construir la matriz homogenea total de la figura:
// M = T(tx,ty) * T(c) * R(theta) * Sh * S * Ref * T(-c)   (c = centroide o (0,0))
Mat3 matrizTotal(const Figura& f) { // Declarar la funcion
    const Transformacion& t = f.transf; // Atajo a los parametros
    float cx = 0.0f, cy = 0.0f; // Pivote (origen por defecto)
    if (t.respectoCentro) centroideRelativo(f, cx, cy); // Usar el centroide como pivote si se pidio
    float rad = t.angulo * 3.14159265358979f / 180.0f; // Convertir grados a radianes

    Mat3 lineal = mat3Rotacion(rad) * mat3Cizalla(t.shx, t.shy) * mat3Escala(t.sx, t.sy) * mat3Reflexion(t.refX, t.refY); // Parte lineal
    return mat3Traslacion(t.tx, t.ty) * mat3Traslacion(cx, cy) * lineal * mat3Traslacion(-cx, -cy); // Composicion final
}

// Aplicar M_total a los vertices ORIGINALES y redondear a celdas enteras (aqui se redondea, no antes)
std::vector<Celda> verticesTransformados(const Figura& f) { // Declarar la funcion
    Mat3 M = matrizTotal(f); // Calcular la matriz total
    std::vector<Celda> res; // Lista de vertices resultantes
    for (size_t i = 0; i < f.vertices.size(); i++) { // Recorrer los vertices originales
        float x = (float)(f.vertices[i].first - origenCol); // Pasar a coordenadas relativas al origen
        float y = (float)(f.vertices[i].second - origenFila); // Pasar a coordenadas relativas al origen
        float xr, yr; // Declarar el punto transformado
        mat3Aplicar(M, x, y, xr, yr); // Aplicar la matriz
        res.push_back({ redondear(xr) + origenCol, redondear(yr) + origenFila }); // Volver a la grilla y redondear
    }
    return res; // Retornar los vertices transformados
}

// Celdas del borde de una figura (reutiliza Bresenham y el circulo de punto medio)
std::vector<Celda> bordeFigura(TipoFigura tipo, const std::vector<Celda>& v) { // Declarar la funcion
    std::vector<Celda> celdas; // Lista de celdas del borde
    if (tipo == FIG_LINEA && v.size() >= 2) { // Linea: un solo segmento
        celdas = bresenham(v[0].first, v[0].second, v[1].first, v[1].second); // Rasterizar el segmento
    }
    else if (tipo == FIG_CIRCULO && v.size() >= 2) { // Circulo: centro + punto del borde
        int difCol = v[1].first - v[0].first; // Diferencia de columnas
        int difFila = v[1].second - v[0].second; // Diferencia de filas
        int radio = raizEntera(difCol * difCol + difFila * difFila); // Radio entero
        celdas = circulo(v[0].first, v[0].second, radio); // Rasterizar el contorno
    }
    else if (tipo == FIG_TRIANGULO || tipo == FIG_RECTANGULO) { // Poligonos cerrados
        int n = (int)v.size(); // Cantidad de vertices
        for (int i = 0; i < n; i++) { // Recorrer las aristas
            Celda a = v[i]; // Vertice inicial
            Celda b = v[(i + 1) % n]; // Vertice final (el ultimo cierra con el primero)
            std::vector<Celda> arista = bresenham(a.first, a.second, b.first, b.second); // Rasterizar la arista
            celdas.insert(celdas.end(), arista.begin(), arista.end()); // Agregar las celdas
        }
    }
    return celdas; // Retornar el borde
}

// Celdas del interior de una figura (scanline)
std::vector<Celda> rellenoFigura(TipoFigura tipo, const std::vector<Celda>& v) { // Declarar la funcion
    if (tipo == FIG_CIRCULO && v.size() >= 2) { // Circulo
        int difCol = v[1].first - v[0].first; // Diferencia de columnas
        int difFila = v[1].second - v[0].second; // Diferencia de filas
        int radio = raizEntera(difCol * difCol + difFila * difFila); // Radio entero
        return rellenoCirculo(v[0].first, v[0].second, radio); // Rellenar el circulo
    }
    if (tipo == FIG_TRIANGULO || tipo == FIG_RECTANGULO) return rellenoPoligono(v); // Rellenar el poligono
    return std::vector<Celda>(); // Las lineas no tienen interior
}

// Celdas de un relleno de bote que sigue a su figura padre: se recorre el interior de la figura YA
// transformada y, por mapeo inverso (destino -> original), se decide si cada celda estaba rellena.
// Asi el relleno rota, escala, se refleja y se cizalla junto con la figura sin dejar huecos.
std::vector<Celda> celdasRellenoSeguido(const Figura& hijo) { // Declarar la funcion
    std::vector<Celda> res; // Lista de celdas resultantes
    const Figura* p = nullptr; // Figura padre
    for (size_t i = 0; i < figuras.size(); i++) if (figuras[i].id == hijo.idPadre) p = &figuras[i]; // Buscar el padre por id
    if (p == nullptr) return res; // Sin padre no hay relleno

    Mat3 inv; // Matriz inversa de la transformacion del padre
    if (!mat3InversaAfin(matrizTotal(*p), inv)) return res; // Salir si no se puede invertir

    std::set<Celda> permitidas(hijo.celdasFijas.begin(), hijo.celdasFijas.end()); // Celdas originales rellenadas
    std::vector<Celda> bordeOrig = bordeFigura(p->tipo, p->vertices); // Borde original del padre
    permitidas.insert(bordeOrig.begin(), bordeOrig.end()); // Incluirlo para evitar huecos junto al borde

    std::vector<Celda> v = verticesTransformados(*p); // Vertices del padre ya transformados
    std::vector<Celda> bordeDest = bordeFigura(p->tipo, v); // Borde actual (no se pisa)
    std::set<Celda> setBorde(bordeDest.begin(), bordeDest.end()); // Pasarlo a conjunto
    std::vector<Celda> interior = rellenoFigura(p->tipo, v); // Interior actual del padre

    for (size_t k = 0; k < interior.size(); k++) { // Recorrer cada celda del interior actual
        if (setBorde.count(interior[k])) continue; // No pintar sobre el borde
        float x = (float)(interior[k].first - origenCol); // Coordenada relativa X
        float y = (float)(interior[k].second - origenFila); // Coordenada relativa Y
        float xo, yo; // Punto en el espacio original
        mat3Aplicar(inv, x, y, xo, yo); // Mapeo inverso
        Celda orig(redondear(xo) + origenCol, redondear(yo) + origenFila); // Celda original correspondiente
        if (permitidas.count(orig)) res.push_back(interior[k]); // Rellenar si estaba rellena
    }
    return res; // Retornar el relleno transformado
}

// Calcular las celdas de una figura ya transformada: interior y borde por separado
void calcularCeldasFigura(const Figura& f, std::vector<Celda>& interior, std::vector<Celda>& borde) { // Declarar la funcion
    interior.clear(); borde.clear(); // Vaciar las listas
    if (f.tipo == FIG_RELLENO) { // Relleno del bote
        if (f.idPadre >= 0) interior = celdasRellenoSeguido(f); // Sigue a su figura padre
        else interior = f.celdasFijas; // Relleno fijo (region formada por varias figuras)
        return; // Terminar
    }
    std::vector<Celda> v = verticesTransformados(f); // Transformar los vertices originales
    borde = bordeFigura(f.tipo, v); // Calcular el borde
    if (f.relleno) interior = rellenoFigura(f.tipo, v); // Calcular el interior si corresponde
}

// ============================== MATRIZ LOGICA DE PIXELES ==============================

bool mismoPixel(const Pixel& a, const Pixel& b) { // Comparar dos pixeles logicos
    if (a.ocupado != b.ocupado) return false; // Uno vacio y otro pintado: distintos
    if (!a.ocupado) return true; // Ambos vacios: iguales
    for (int i = 0; i < 4; i++) if (a.color[i] != b.color[i]) return false; // Comparar los canales
    return true; // Mismo color
}

void ponerPixel(int col, int fila, const float* color) { // Pintar una celda de la matriz logica
    if (col < 0 || col >= columnas || fila < 0 || fila >= filas) return; // Ignorar celdas fuera de la grilla
    Pixel& p = matriz[fila * columnas + col]; // Obtener el pixel
    p.ocupado = true; // Marcar como pintado
    copiarColor(p.color, color); // Guardar el color
}

// Reconstruir la matriz completa desde las figuras (relleno primero, borde encima)
void reconstruirMatriz() { // Declarar la funcion
    matriz.assign(columnas * filas, Pixel()); // Vaciar la matriz con el tamano actual
    for (size_t i = 0; i < figuras.size(); i++) { // Recorrer las figuras en orden
        std::vector<Celda> interior, borde; // Listas de celdas
        calcularCeldasFigura(figuras[i], interior, borde); // Rasterizar la figura
        for (size_t k = 0; k < interior.size(); k++) ponerPixel(interior[k].first, interior[k].second, figuras[i].colRelleno); // Pintar el interior
        for (size_t k = 0; k < borde.size(); k++) ponerPixel(borde[k].first, borde[k].second, figuras[i].colBorde); // Pintar el borde encima
    }
}

// ============================== OPERACIONES DE LA ESCENA ==============================

void apilarUndo(const std::vector<Figura>& estado) { // Guardar un estado en la pila de Deshacer
    historial.push_back(estado); // Guardar la copia
    if (historial.size() > 200) historial.erase(historial.begin()); // Limitar el tamano de la pila
}

void deshacer() { // Volver al estado anterior de la escena
    if (historial.empty()) return; // Salir si no hay nada que deshacer
    figuras = historial.back(); // Restaurar las figuras
    historial.pop_back(); // Quitar el estado de la pila
    if (figuraSeleccionada >= (int)figuras.size()) figuraSeleccionada = -1; // Corregir la seleccion
    puntosPendientes.clear(); // Cancelar cualquier figura en creacion
}

void eliminarSeleccionada() { // Eliminar la figura seleccionada
    if (figuraSeleccionada < 0 || figuraSeleccionada >= (int)figuras.size()) return; // Salir si no hay seleccion
    apilarUndo(figuras); // Guardar para Deshacer
    int idBorrado = figuras[figuraSeleccionada].id; // Guardar el id de la figura
    figuras.erase(figuras.begin() + figuraSeleccionada); // Quitar la figura
    for (int i = (int)figuras.size() - 1; i >= 0; i--) // Quitar tambien los rellenos de bote que dependian de ella
        if (figuras[i].tipo == FIG_RELLENO && figuras[i].idPadre == idBorrado) figuras.erase(figuras.begin() + i);
    figuraSeleccionada = -1; // Quitar la seleccion
}

void limpiarLienzo() { // Borrar todas las figuras
    apilarUndo(figuras); // Guardar para Deshacer
    figuras.clear(); // Borrar las figuras
    figuraSeleccionada = -1; // Quitar la seleccion
    puntosPendientes.clear(); // Cancelar cualquier punto pendiente
}

void cambiarHerramienta(Herramienta h) { // Cambiar de herramienta
    if (h != herramienta) { herramienta = h; puntosPendientes.clear(); } // Cancelar la figura pendiente al cambiar
}

int puntosNecesarios(Herramienta h) { // Cantidad de clics que necesita cada herramienta
    switch (h) { // Comparar la herramienta
    case HERR_LINEA: return 2; // Dos extremos
    case HERR_CIRCULO: return 2; // Centro y punto del borde
    case HERR_TRIANGULO: return 3; // Tres vertices
    case HERR_CUADRADO: return 2; // Dos esquinas opuestas
    default: return 0; // Bote y seleccionar no crean figuras
    }
}

// Construir una figura a partir de las celdas clicadas (o escritas en la interfaz)
bool construirFigura(Herramienta h, const std::vector<Celda>& p, Figura& f) { // Declarar la funcion
    f = Figura(); // Reiniciar la figura
    copiarColor(f.colBorde, colorLinea); // Usar el color de borde actual
    copiarColor(f.colRelleno, colorRelleno); // Usar el color de relleno actual
    f.relleno = rellenarAlCrear; // Usar la opcion de rellenar al crear

    if (h == HERR_LINEA) { // Linea
        if (p.size() < 2) return false; // Faltan puntos
        f.tipo = FIG_LINEA; f.relleno = false; // Las lineas no se rellenan
        f.vertices = { p[0], p[1] }; // Guardar los extremos
    }
    else if (h == HERR_CIRCULO) { // Circulo
        if (p.size() < 2) return false; // Faltan puntos
        f.tipo = FIG_CIRCULO; // Establecer el tipo
        f.vertices = { p[0], p[1] }; // Guardar centro y punto del borde
    }
    else if (h == HERR_TRIANGULO) { // Triangulo
        if (p.size() < 3) return false; // Faltan puntos
        f.tipo = FIG_TRIANGULO; // Establecer el tipo
        f.vertices = { p[0], p[1], p[2] }; // Guardar los 3 vertices
    }
    else if (h == HERR_CUADRADO) { // Cuadrado / rectangulo
        if (p.size() < 2) return false; // Faltan puntos
        int x0 = p[0].first, y0 = p[0].second; // Primera esquina
        int x1 = p[1].first, y1 = p[1].second; // Esquina opuesta
        if (forzarCuadrado) { // Forzar lados iguales
            int dx = x1 - x0, dy = y1 - y0; // Diferencias
            int lado = std::max(std::abs(dx), std::abs(dy)); // Lado del cuadrado
            x1 = x0 + (dx < 0 ? -lado : lado); // Ajustar la X
            y1 = y0 + (dy < 0 ? -lado : lado); // Ajustar la Y
        }
        f.tipo = FIG_RECTANGULO; // Establecer el tipo
        f.vertices = { Celda(x0, y0), Celda(x1, y0), Celda(x1, y1), Celda(x0, y1) }; // Guardar las 4 esquinas (para poder rotarlo)
    }
    else return false; // Herramienta que no crea figuras
    return true; // Figura construida
}

void crearFiguraDesdePuntos(Herramienta h, const std::vector<Celda>& puntos) { // Crear y agregar una figura a la escena
    Figura f; // Declarar la figura
    if (!construirFigura(h, puntos, f)) return; // Salir si no se pudo construir
    apilarUndo(figuras); // Guardar para Deshacer
    f.id = siguienteId++; // Asignar un id unico
    figuras.push_back(f); // Agregar la figura a la escena
    figuraSeleccionada = (int)figuras.size() - 1; // Seleccionar la figura recien creada
    std::cout << "Figura creada con " << f.vertices.size() << " vertices." << std::endl; // Mostrar el mensaje
}

// --- BOTE DE PINTURA: FLOOD FILL (BFS con cola, 4-conectado) SOBRE LA MATRIZ LOGICA ---
void rellenoBote(int col, int fila) { // Declarar la funcion
    if (col < 0 || col >= columnas || fila < 0 || fila >= filas) return; // Salir si esta fuera de la grilla
    reconstruirMatriz(); // Asegurar que la matriz refleje el estado actual

    Pixel objetivo = matriz[fila * columnas + col]; // Guardar lo que hay en la celda clicada
    Pixel nuevo; // Crear el pixel con el color de relleno
    nuevo.ocupado = true; // Marcarlo como pintado
    copiarColor(nuevo.color, colorRelleno); // Copiar el color de relleno
    if (mismoPixel(objetivo, nuevo)) return; // Salir si ya tiene ese color (evita ciclos inutiles)

    std::vector<char> visitado(columnas * filas, 0); // Marcar celdas ya encoladas
    std::queue<Celda> cola; // Cola del BFS (no usa recursion, no desborda la pila)
    std::vector<Celda> region; // Celdas que se van a rellenar

    cola.push({ col, fila }); // Encolar la semilla
    visitado[fila * columnas + col] = 1; // Marcar la semilla

    const int dc[4] = { 1, -1, 0, 0 }; // Desplazamientos en columnas (vecinos 4-conectados)
    const int df[4] = { 0, 0, 1, -1 }; // Desplazamientos en filas

    while (!cola.empty()) { // Recorrer mientras haya celdas por visitar
        Celda c = cola.front(); cola.pop(); // Sacar la siguiente celda
        region.push_back(c); // Agregarla a la region
        for (int k = 0; k < 4; k++) { // Revisar los 4 vecinos
            int nc = c.first + dc[k]; // Columna del vecino
            int nf = c.second + df[k]; // Fila del vecino
            if (nc < 0 || nc >= columnas || nf < 0 || nf >= filas) continue; // Ignorar fuera de la grilla
            int idx = nf * columnas + nc; // Indice del vecino
            if (visitado[idx]) continue; // Ignorar ya visitados
            if (!mismoPixel(matriz[idx], objetivo)) continue; // Detenerse al chocar con un borde u otro color
            visitado[idx] = 1; // Marcar el vecino
            cola.push({ nc, nf }); // Encolar el vecino
        }
    }

    Figura f; // Guardar el relleno como un elemento de la escena (asi se puede deshacer/eliminar)
    f.tipo = FIG_RELLENO; // Establecer el tipo
    f.id = siguienteId++; // Asignar un id unico
    f.celdasFijas = region; // Guardar las celdas rellenadas (absolutas, por defecto)

    // Buscar la figura MAS INTERNA que encierra por completo la region. Si existe, el relleno queda
    // asociado a ella y se transformara junto con ella.
    std::set<Celda> setRegion(region.begin(), region.end()); // Region como conjunto
    int mejor = -1; size_t mejorTam = 0; // Mejor candidato (menor area)
    for (int i = 0; i < (int)figuras.size(); i++) { // Recorrer las figuras
        const Figura& p = figuras[i]; // Candidato a padre
        if (p.tipo == FIG_RELLENO || p.tipo == FIG_LINEA) continue; // Solo figuras cerradas
        std::vector<Celda> v = verticesTransformados(p); // Vertices actuales
        std::vector<Celda> inte = rellenoFigura(p.tipo, v); // Interior actual
        std::vector<Celda> bor = bordeFigura(p.tipo, v); // Borde actual
        std::set<Celda> cobertura(inte.begin(), inte.end()); // Interior como conjunto
        bool clicDentro = cobertura.count({ col, fila }) > 0; // El clic debe caer en el interior
        cobertura.insert(bor.begin(), bor.end()); // Agregar el borde
        if (!clicDentro) continue; // Descartar si el clic no esta dentro
        bool contiene = true; // Indicar si la region cabe en la figura
        for (std::set<Celda>::const_iterator it = setRegion.begin(); it != setRegion.end(); ++it)
            if (!cobertura.count(*it)) { contiene = false; break; } // La region se sale de la figura
        if (!contiene) continue; // Descartar
        if (mejor < 0 || cobertura.size() < mejorTam) { mejor = i; mejorTam = cobertura.size(); } // Quedarse con la mas pequena
    }
    if (mejor >= 0) { // Hay figura padre: guardar la region en el espacio ORIGINAL de esa figura
        const Figura& p = figuras[mejor]; // Figura padre
        Mat3 M = matrizTotal(p); // Transformacion actual del padre
        std::vector<Celda> srcInterior = rellenoFigura(p.tipo, p.vertices); // Interior original del padre
        f.celdasFijas.clear(); // Vaciar las celdas absolutas
        for (size_t k = 0; k < srcInterior.size(); k++) { // Recorrer las celdas originales
            float x = (float)(srcInterior[k].first - origenCol); // Coordenada relativa X
            float y = (float)(srcInterior[k].second - origenFila); // Coordenada relativa Y
            float xr, yr; // Punto transformado
            mat3Aplicar(M, x, y, xr, yr); // Llevarlo a su posicion actual
            Celda d(redondear(xr) + origenCol, redondear(yr) + origenFila); // Celda actual
            if (setRegion.count(d)) f.celdasFijas.push_back(srcInterior[k]); // Si cae en la region, era parte del relleno
        }
        f.idPadre = p.id; // Asociar el relleno a la figura padre
    }
    copiarColor(f.colRelleno, colorRelleno); // Guardar el color
    copiarColor(f.colBorde, colorRelleno); // Guardar el color (sin borde propio)
    f.relleno = true; // Marcar como relleno
    apilarUndo(figuras); // Guardar para Deshacer
    figuras.push_back(f); // Agregar a la escena
    std::cout << "Bote de pintura: " << region.size() << " celdas rellenadas." << std::endl; // Mostrar el mensaje
    if ((int)region.size() == columnas * filas) std::cout << "Aviso: la region no estaba cerrada, se lleno toda la grilla." << std::endl; // Avisar region abierta
}

// Seleccionar la figura que contiene la celda clicada (de arriba hacia abajo)
void seleccionarFiguraEn(int col, int fila) { // Declarar la funcion
    for (int i = (int)figuras.size() - 1; i >= 0; i--) { // Recorrer desde la ultima figura dibujada
        std::vector<Celda> interior, borde; // Listas de celdas
        calcularCeldasFigura(figuras[i], interior, borde); // Rasterizar la figura
        bool hallada = false; // Indicar si la celda pertenece a la figura
        for (size_t k = 0; k < borde.size() && !hallada; k++) hallada = (borde[k].first == col && borde[k].second == fila); // Buscar en el borde
        for (size_t k = 0; k < interior.size() && !hallada; k++) hallada = (interior[k].first == col && interior[k].second == fila); // Buscar en el interior
        if (hallada) { // Se encontro la figura
            figuraSeleccionada = i; // Seleccionarla
            std::cout << "Figura " << i + 1 << " seleccionada." << std::endl; // Mostrar el mensaje
            return; // Terminar
        }
    }
    figuraSeleccionada = -1; // No hay figura en esa celda
}

const char* nombreFigura(const Figura& f) { // Nombre legible de la figura para la lista
    switch (f.tipo) { // Comparar el tipo
    case FIG_LINEA: return "Linea"; // Linea
    case FIG_CIRCULO: return "Circulo"; // Circulo
    case FIG_TRIANGULO: return "Triangulo"; // Triangulo
    case FIG_RECTANGULO: return "Rectangulo"; // Rectangulo
    case FIG_RELLENO: return "Relleno (bote)"; // Relleno del bote
    }
    return "Figura"; // Valor por defecto
}

// --- CONVERTIR UNA POSICION DE MOUSE (pixeles) A CELDA DE LA GRILLA (col, fila) ---
void mousePosACelda(GLFWwindow* window, int& col, int& fila) { // Declarar la funcion de conversion
    double xpos, ypos; // Declarar las posiciones del mouse en pixeles
    glfwGetCursorPos(window, &xpos, &ypos); // Obtener la posicion actual del mouse

    float openGL_X = ((float)xpos / anchoVentana) * 2.0f - 1.0f; // Convertir la posicion X a coordenadas OpenGL
    float openGL_Y = -(((float)ypos / altoVentana) * 2.0f - 1.0f); // Convertir la posicion Y a coordenadas OpenGL

    float pasoX = 2.0f / columnas; // Calcular el ancho de una celda en coordenadas OpenGL
    float pasoY = 2.0f / filas; // Calcular el alto de una celda en coordenadas OpenGL

    col = (int)std::floor((openGL_X + 1.0f) / pasoX); // Determinar la columna del mouse
    fila = (int)std::floor((openGL_Y + 1.0f) / pasoY); // Determinar la fila del mouse
} // Cerrar la funcion mousePosACelda

// --- CAPTURAR CLICS DEL MOUSE ---
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) { // Declarar la funcion de clics
    // ===== [IMGUI] Si el mouse esta sobre la barra de herramientas, no dibujar en la grilla =====
    if (ImGui::GetIO().WantCaptureMouse) return; // Salir si ImGui esta usando el mouse
    // ===== [FIN IMGUI] =====

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) { // Comparar si se presiono el clic derecho
        if (!puntosPendientes.empty()) { // Hay una figura a medio crear
            puntosPendientes.clear(); // Cancelar los puntos pendientes
            std::cout << "Figura cancelada." << std::endl; // Mostrar el mensaje de cancelacion
        }
        else figuraSeleccionada = -1; // Si no hay nada pendiente, quitar la seleccion
        return; // Salir de la funcion
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return; // Salir si no fue clic izquierdo

    int col, fila; // Declarar la celda del clic
    mousePosACelda(window, col, fila); // Determinar la celda donde se hizo clic

    if (col < 0 || col >= columnas || fila < 0 || fila >= filas) return; // Salir si el clic esta fuera de la grilla

    if (herramienta == HERR_BOTE) { rellenoBote(col, fila); return; } // Bote de pintura: rellenar la region clicada
    if (herramienta == HERR_SELECCIONAR) { seleccionarFiguraEn(col, fila); return; } // Seleccionar: elegir la figura clicada

    puntosPendientes.push_back({ col, fila }); // Guardar la celda clicada como vertice
    std::cout << "Punto " << puntosPendientes.size() << " en celda (" << col << ", " << fila << ")" << std::endl; // Mostrar el mensaje

    if ((int)puntosPendientes.size() >= puntosNecesarios(herramienta)) { // Comparar si ya estan todos los vertices
        crearFiguraDesdePuntos(herramienta, puntosPendientes); // Crear la figura
        puntosPendientes.clear(); // Quedar listos para trazar otra figura
    }
} // Cerrar la funcion mouse_button_callback

// --- RECONSTRUIR EL BUFFER DE PINTADO (matriz logica + marcadores) Y SUBIRLO A LA GPU ---
void actualizarPintado(GLFWwindow* window) { // Declarar la funcion
    reconstruirMatriz(); // Rasterizar todas las figuras en la matriz logica
    paintVertices.clear(); // Vaciar los vertices anteriores

    for (int fila = 0; fila < filas; fila++) { // Recorrer las filas de la matriz
        for (int col = 0; col < columnas; col++) { // Recorrer las columnas de la matriz
            const Pixel& p = matriz[fila * columnas + col]; // Obtener el pixel logico
            if (p.ocupado) pintarCelda(col, fila, p.color, 1.0f); // Pintar la celda completa
        }
    }

    // --- Vista previa de la figura que se esta creando ---
    if (!puntosPendientes.empty() && !ImGui::GetIO().WantCaptureMouse) { // Hay figura en creacion y el mouse esta en la grilla
        int mc, mf; // Declarar la celda bajo el mouse
        mousePosACelda(window, mc, mf); // Obtener la celda bajo el mouse
        if (mc >= 0 && mc < columnas && mf >= 0 && mf < filas) { // El mouse esta dentro de la grilla
            std::vector<Celda> pts = puntosPendientes; // Copiar los puntos ya clicados
            pts.push_back({ mc, mf }); // Agregar la celda actual como punto provisional
            Herramienta hPrev = herramienta; // Herramienta usada para la vista previa
            if (hPrev == HERR_TRIANGULO && pts.size() == 2) hPrev = HERR_LINEA; // Con 1 vertice solo se ve una linea
            Figura previa; // Declarar la figura de vista previa
            if (construirFigura(hPrev, pts, previa)) { // Construir la figura provisional
                std::vector<Celda> interior, borde; // Listas de celdas
                calcularCeldasFigura(previa, interior, borde); // Rasterizar la vista previa
                float cRel[4], cBor[4]; // Colores semitransparentes
                copiarColor(cRel, colorRelleno); cRel[3] *= 0.35f; // Relleno muy tenue
                copiarColor(cBor, colorLinea); cBor[3] *= 0.6f; // Borde tenue
                for (size_t k = 0; k < interior.size(); k++) pintarCelda(interior[k].first, interior[k].second, cRel, 1.0f); // Dibujar el interior
                for (size_t k = 0; k < borde.size(); k++) pintarCelda(borde[k].first, borde[k].second, cBor, 1.0f); // Dibujar el borde
            }
        }
    }

    // --- Marcadores ---
    float colBlanco[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Color de los marcadores de seleccion
    float colOrigen[4] = { 1.0f, 1.0f, 0.0f, 1.0f }; // Color del marcador del origen (amarillo)
    float colPivote[4] = { 1.0f, 0.2f, 1.0f, 1.0f }; // Color del marcador del pivote (magenta)

    for (size_t k = 0; k < puntosPendientes.size(); k++) pintarCelda(puntosPendientes[k].first, puntosPendientes[k].second, colBlanco, 0.45f); // Marcar los vertices ya clicados

    if (figuraSeleccionada >= 0 && figuraSeleccionada < (int)figuras.size()) { // Hay una figura seleccionada
        const Figura& f = figuras[figuraSeleccionada]; // Obtener la figura
        std::vector<Celda> interior, borde; // Listas de celdas
        calcularCeldasFigura(f, interior, borde); // Rasterizar la figura
        const std::vector<Celda>& marcar = (f.tipo == FIG_RELLENO) ? interior : borde; // Celdas a marcar
        for (size_t k = 0; k < marcar.size(); k++) pintarCelda(marcar[k].first, marcar[k].second, colBlanco, 0.30f); // Marcar la figura seleccionada
        if (f.tipo != FIG_RELLENO) { // Mostrar el pivote de la transformacion
            float cx = 0.0f, cy = 0.0f; // Pivote relativo al origen
            if (f.transf.respectoCentro) centroideRelativo(f, cx, cy); // Usar el centroide si corresponde
            pintarCelda(redondear(cx + f.transf.tx) + origenCol, redondear(cy + f.transf.ty) + origenFila, colPivote, 0.55f); // Dibujar el pivote
        }
    }

    pintarCelda(origenCol, origenFila, colOrigen, 0.40f); // Marcar el origen (0,0) del sistema de coordenadas

    glBindBuffer(GL_ARRAY_BUFFER, paintVBO); // Activar el buffer del pintado
    glBufferData(GL_ARRAY_BUFFER, paintVertices.size() * sizeof(float), paintVertices.data(), GL_DYNAMIC_DRAW); // Subir los vertices pintados a la GPU
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Desactivar el buffer
} // Cerrar la funcion actualizarPintado

// --- SHADERS ---
// El color puede venir de un uniform (grilla) o de cada vertice (celdas pintadas)
const char* vertexShaderSource = "#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec4 aColor;\n"
"out vec4 vColor;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
"   vColor = aColor;\n"
"}\0";

const char* fragmentShaderSource = "#version 330 core\n"
"in vec4 vColor;\n"
"out vec4 FragColor;\n"
"uniform vec4 uColor;\n"
"uniform int uPorVertice;\n"
"void main()\n"
"{\n"
"   FragColor = (uPorVertice == 1) ? vColor : uColor;\n"
"}\n\0";

int main() { // Iniciar la funcion principal del programa
    glfwInit(); // Inicializar GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // Establecer la version mayor de OpenGL
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // Establecer la version menor de OpenGL
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Establecer el perfil core de OpenGL

    GLFWwindow* window = glfwCreateWindow(anchoVentana, altoVentana, "Figuras 2D en grilla", NULL, NULL); // Crear la ventana
    if (window == NULL) { // Comparar si la ventana no se creo
        glfwTerminate(); // Cerrar GLFW
        return -1; // Retornar el codigo de error
    }
    glfwMakeContextCurrent(window); // Establecer la ventana como contexto actual
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback); // Establecer la funcion de cambio de tamano
    glfwSetMouseButtonCallback(window, mouse_button_callback); // Establecer la funcion de clics

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress); // Cargar las funciones de OpenGL

    glEnable(GL_BLEND); // Activar la mezcla para la vista previa semitransparente
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Establecer la funcion de mezcla estandar

    // ===== [IMGUI] Inicializar Dear ImGui =====
    // IMPORTANTE: se inicializa DESPUES de glfwSetMouseButtonCallback. Con install_callbacks = true,
    // ImGui guarda tu callback anterior y lo sigue llamando (encadenado), asi que ambos funcionan.
    IMGUI_CHECKVERSION(); // Verificar que la version de ImGui sea consistente
    ImGui::CreateContext(); // Crear el contexto de ImGui
    ImGui::StyleColorsDark(); // Establecer el tema oscuro
    ImGui_ImplGlfw_InitForOpenGL(window, true); // Conectar ImGui con GLFW (true = instalar callbacks)
    ImGui_ImplOpenGL3_Init("#version 330"); // Conectar ImGui con OpenGL 3.3
    // ===== [FIN IMGUI] =====

    // Compilar shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER); // Crear el vertex shader
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL); // Asignar el codigo al vertex shader
    glCompileShader(vertexShader); // Compilar el vertex shader

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER); // Crear el fragment shader
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL); // Asignar el codigo al fragment shader
    glCompileShader(fragmentShader); // Compilar el fragment shader

    GLuint shaderProgram = glCreateProgram(); // Crear el programa de shaders
    glAttachShader(shaderProgram, vertexShader); // Unir el vertex shader al programa
    glAttachShader(shaderProgram, fragmentShader); // Unir el fragment shader al programa
    glLinkProgram(shaderProgram); // Enlazar el programa
    glDeleteShader(vertexShader); // Borrar el vertex shader ya enlazado
    glDeleteShader(fragmentShader); // Borrar el fragment shader ya enlazado

    GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor"); // Obtener la ubicacion del color en el shader
    GLint porVerticeLoc = glGetUniformLocation(shaderProgram, "uPorVertice"); // Obtener la ubicacion del selector de color

    // --- PREPARAR BUFFERS DE LA GRILLA ---
    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    generarGrilla(); // Generar la grilla inicial ahora que gridVBO ya existe

    // --- PREPARAR BUFFERS DEL PINTADO (posicion + color por vertice) ---
    glGenVertexArrays(1, &paintVAO);
    glGenBuffers(1, &paintVBO);
    glBindVertexArray(paintVAO);
    glBindBuffer(GL_ARRAY_BUFFER, paintVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    std::cout << "L: linea | C: circulo | T: triangulo | R: cuadrado | B: bote | S: seleccionar | Ctrl+Z: deshacer | Supr: eliminar" << std::endl;

    bool zAntes = false, supAntes = false; // Guardar el estado previo de las teclas para detectar solo la pulsacion

    // Valores del panel "Crear por coordenadas"
    int coordP1[2] = { 5, 5 }, coordP2[2] = { 15, 5 }, coordP3[2] = { 10, 15 }; // Coordenadas escritas (col, fila)

    while (!glfwWindowShouldClose(window)) { // Recorrer el ciclo principal mientras la ventana siga abierta
        glfwPollEvents(); // [IMGUI] Procesar eventos al inicio del frame (ImGui lo necesita antes de NewFrame)
        actualizarOrigen(); // Actualizar la celda que actua como origen (0,0)

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) // Comparar si se presiono la tecla Escape
            glfwSetWindowShouldClose(window, true); // Establecer el cierre de la ventana

        // ===== [IMGUI] Solo leer las teclas si ImGui no esta usando el teclado =====
        if (!ImGui::GetIO().WantCaptureKeyboard) { // Comparar si el teclado esta libre
            bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS; // Leer Ctrl
            if (!ctrl) { // Los atajos de herramienta no deben chocar con Ctrl+tecla
                if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) cambiarHerramienta(HERR_LINEA); // Modo linea
                if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) cambiarHerramienta(HERR_CIRCULO); // Modo circulo
                if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) cambiarHerramienta(HERR_TRIANGULO); // Modo triangulo
                if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) cambiarHerramienta(HERR_CUADRADO); // Modo cuadrado
                if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) cambiarHerramienta(HERR_BOTE); // Modo bote de pintura
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cambiarHerramienta(HERR_SELECCIONAR); // Modo seleccionar
            }
            bool z = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS; // Leer la tecla Z
            if (ctrl && z && !zAntes) deshacer(); // Ctrl+Z: deshacer (solo al pulsar)
            zAntes = z; // Recordar el estado
            bool sup = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS; // Leer la tecla Supr
            if (sup && !supAntes) eliminarSeleccionada(); // Supr: eliminar la figura seleccionada
            supAntes = sup; // Recordar el estado
        }
        // ===== [FIN IMGUI] =====

        // ===== [IMGUI] Empezar un nuevo frame de ImGui y construir la barra de herramientas =====
        ImGui_ImplOpenGL3_NewFrame(); // Preparar el backend de OpenGL
        ImGui_ImplGlfw_NewFrame(); // Preparar el backend de GLFW
        ImGui::NewFrame(); // Empezar el frame de ImGui

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver); // Posicion inicial de la barra
        ImGui::SetNextWindowSize(ImVec2(350, 580), ImGuiCond_FirstUseEver); // Tamano inicial (con scroll si falta espacio)
        ImGui::Begin("Herramientas"); // Abrir la ventana de la barra

        // ---------- HERRAMIENTA ----------
        if (ImGui::CollapsingHeader("Herramienta", ImGuiTreeNodeFlags_DefaultOpen)) { // Seccion de herramientas
            if (ImGui::RadioButton("Linea (L)", herramienta == HERR_LINEA)) cambiarHerramienta(HERR_LINEA); // Elegir linea
            ImGui::SameLine(); // Poner el siguiente control en la misma fila
            if (ImGui::RadioButton("Circulo (C)", herramienta == HERR_CIRCULO)) cambiarHerramienta(HERR_CIRCULO); // Elegir circulo
            ImGui::SameLine(); // Poner el siguiente control en la misma fila
            if (ImGui::RadioButton("Triangulo (T)", herramienta == HERR_TRIANGULO)) cambiarHerramienta(HERR_TRIANGULO); // Elegir triangulo
            if (ImGui::RadioButton("Cuadrado (R)", herramienta == HERR_CUADRADO)) cambiarHerramienta(HERR_CUADRADO); // Elegir cuadrado
            ImGui::SameLine(); // Poner el siguiente control en la misma fila
            if (ImGui::RadioButton("Bote (B)", herramienta == HERR_BOTE)) cambiarHerramienta(HERR_BOTE); // Elegir bote de pintura
            ImGui::SameLine(); // Poner el siguiente control en la misma fila
            if (ImGui::RadioButton("Seleccionar (S)", herramienta == HERR_SELECCIONAR)) cambiarHerramienta(HERR_SELECCIONAR); // Elegir seleccionar

            if (herramienta == HERR_CUADRADO) ImGui::Checkbox("Forzar cuadrado (lados iguales)", &forzarCuadrado); // Opcion de cuadrado
            if (!puntosPendientes.empty()) ImGui::TextDisabled("Punto %d de %d... (clic derecho cancela)", (int)puntosPendientes.size() + 1, puntosNecesarios(herramienta)); // Mostrar estado
            if (herramienta == HERR_BOTE) ImGui::TextDisabled("Clic dentro de una region cerrada para rellenarla."); // Ayuda del bote
            if (herramienta == HERR_SELECCIONAR) ImGui::TextDisabled("Clic sobre una figura para seleccionarla."); // Ayuda de seleccionar
        }

        // ---------- COLORES ----------
        if (ImGui::CollapsingHeader("Colores", ImGuiTreeNodeFlags_DefaultOpen)) { // Seccion de colores
            ImGui::ColorEdit4("Borde (nuevas)", colorLinea); // Selector del color de borde de las figuras nuevas
            ImGui::ColorEdit4("Relleno / Bote", colorRelleno); // Selector del color de relleno (independiente del borde)
            ImGui::Checkbox("Rellenar al crear", &rellenarAlCrear); // Opcion de rellenar automaticamente
            ImGui::ColorEdit4("Grilla", colorGrilla); // Selector del color de la grilla
            ImGui::ColorEdit4("Fondo", colorFondo); // Selector del color de fondo
            ImGui::Checkbox("Origen (0,0) en el centro de la grilla", &origenCentro); // Elegir donde esta el origen
        }

        // ---------- CREAR POR COORDENADAS ----------
        if (ImGui::CollapsingHeader("Crear por coordenadas")) { // Seccion de creacion numerica
            ImGui::TextDisabled("Celdas (col, fila); (0,0) = esquina inferior izquierda"); // Ayuda
            ImGui::InputInt2("P1", coordP1); // Primer punto
            ImGui::InputInt2("P2", coordP2); // Segundo punto
            if (herramienta == HERR_TRIANGULO) ImGui::InputInt2("P3", coordP3); // Tercer punto (solo triangulo)
            if (herramienta == HERR_BOTE) { // Bote: P1 es la semilla
                if (ImGui::Button("Rellenar desde P1")) rellenoBote(coordP1[0], coordP1[1]); // Ejecutar el bote
            }
            else if (herramienta == HERR_SELECCIONAR) { // Seleccionar: P1 es la celda
                if (ImGui::Button("Seleccionar en P1")) seleccionarFiguraEn(coordP1[0], coordP1[1]); // Ejecutar la seleccion
            }
            else if (ImGui::Button("Crear con la herramienta actual")) { // Crear la figura con las coordenadas escritas
                std::vector<Celda> pts; // Lista de puntos
                pts.push_back({ coordP1[0], coordP1[1] }); // Agregar P1
                pts.push_back({ coordP2[0], coordP2[1] }); // Agregar P2
                if (herramienta == HERR_TRIANGULO) pts.push_back({ coordP3[0], coordP3[1] }); // Agregar P3
                crearFiguraDesdePuntos(herramienta, pts); // Crear la figura
            }
        }

        // ---------- ESCENA ----------
        if (ImGui::CollapsingHeader("Escena", ImGuiTreeNodeFlags_DefaultOpen)) { // Seccion de la escena
            if (ImGui::BeginListBox("##figuras", ImVec2(-1, 6 * ImGui::GetTextLineHeightWithSpacing()))) { // Lista de figuras
                for (int i = 0; i < (int)figuras.size(); i++) { // Recorrer las figuras
                    char etiqueta[64]; // Texto de la fila
                    snprintf(etiqueta, sizeof(etiqueta), "#%d %s", i + 1, nombreFigura(figuras[i])); // Armar el texto
                    ImGui::PushID(i); // Identificador unico de la fila
                    if (ImGui::Selectable(etiqueta, i == figuraSeleccionada)) figuraSeleccionada = i; // Seleccionar al hacer clic
                    ImGui::PopID(); // Cerrar el identificador
                }
                ImGui::EndListBox(); // Cerrar la lista
            }
            if (ImGui::Button("Eliminar seleccionada")) eliminarSeleccionada(); // Boton para eliminar
            ImGui::SameLine(); // Poner el siguiente control en la misma fila
            if (ImGui::Button("Deshacer (Ctrl+Z)")) deshacer(); // Boton de Undo
            if (ImGui::Button("Limpiar lienzo")) limpiarLienzo(); // Boton para borrar todo lo pintado
        }

        // ---------- FIGURA SELECCIONADA + TRANSFORMACIONES ----------
        if (figuraSeleccionada >= 0 && figuraSeleccionada < (int)figuras.size()) { // Hay una figura seleccionada
            Figura& f = figuras[figuraSeleccionada]; // Obtener la figura
            std::vector<Figura> estadoPrevio = figuras; // Copia ANTES de tocar widgets (para Deshacer)
            bool activado = false; // Indicar si algun widget empezo a editarse en este cuadro

            if (ImGui::CollapsingHeader("Figura seleccionada", ImGuiTreeNodeFlags_DefaultOpen)) { // Seccion de la figura
                ImGui::Text("#%d %s", figuraSeleccionada + 1, nombreFigura(f)); // Mostrar el nombre
                if (f.tipo != FIG_RELLENO) { // Los rellenos del bote no tienen borde ni transformacion
                    if (f.tipo != FIG_LINEA) { // Las lineas no se rellenan
                        ImGui::Checkbox("Rellenar", &f.relleno); // Activar o quitar el relleno
                        activado |= ImGui::IsItemActivated(); // Detectar edicion
                        ImGui::ColorEdit4("Color relleno", f.colRelleno); // Editar el color de relleno
                        activado |= ImGui::IsItemActivated(); // Detectar edicion
                    }
                    ImGui::ColorEdit4("Color borde", f.colBorde); // Editar el color de borde
                    activado |= ImGui::IsItemActivated(); // Detectar edicion
                }
                else { // Relleno del bote
                    ImGui::ColorEdit4("Color relleno", f.colRelleno); // Editar el color del relleno
                    activado |= ImGui::IsItemActivated(); // Detectar edicion
                }
            }

            if (ImGui::CollapsingHeader("Transformaciones 2D", ImGuiTreeNodeFlags_DefaultOpen)) { // Seccion de transformaciones
                if (f.tipo == FIG_RELLENO) { // Los rellenos del bote son celdas fijas
                    if (f.idPadre >= 0) ImGui::TextDisabled("Este relleno sigue a su figura: selecciona la figura para transformarlo."); // Aviso
                    else ImGui::TextDisabled("Relleno fijo (region formada por varias figuras): no se transforma."); // Aviso
                }
                else { // Figura transformable
                    Transformacion& t = f.transf; // Atajo a los parametros

                    ImGui::Checkbox("Transformar respecto al centro de la figura", &t.respectoCentro); // Pivote: centroide u origen
                    activado |= ImGui::IsItemActivated(); // Detectar edicion

                    ImGui::SeparatorText("Traslacion"); // Titulo de seccion
                    ImGui::DragFloat("Tx", &t.tx, 0.1f, -1000.0f, 1000.0f, "%.2f"); // Traslacion en X
                    activado |= ImGui::IsItemActivated(); // Detectar edicion
                    ImGui::DragFloat("Ty", &t.ty, 0.1f, -1000.0f, 1000.0f, "%.2f"); // Traslacion en Y
                    activado |= ImGui::IsItemActivated(); // Detectar edicion

                    ImGui::SeparatorText("Rotacion"); // Titulo de seccion
                    ImGui::SliderFloat("Angulo (grados)", &t.angulo, 0.0f, 360.0f, "%.1f"); // Rotacion libre
                    activado |= ImGui::IsItemActivated(); // Detectar edicion

                    ImGui::SeparatorText("Escala"); // Titulo de seccion
                    ImGui::DragFloat("Sx", &t.sx, 0.01f, -20.0f, 20.0f, "%.2f"); // Escala en X
                    activado |= ImGui::IsItemActivated(); // Detectar edicion
                    ImGui::DragFloat("Sy", &t.sy, 0.01f, -20.0f, 20.0f, "%.2f"); // Escala en Y
                    activado |= ImGui::IsItemActivated(); // Detectar edicion

                    ImGui::SeparatorText("Cizallamiento"); // Titulo de seccion
                    ImGui::DragFloat("Sh_x  (y' += Shx*x)", &t.shx, 0.01f, -10.0f, 10.0f, "%.2f"); // Cizalla Sh_x
                    activado |= ImGui::IsItemActivated(); // Detectar edicion
                    ImGui::DragFloat("Sh_y  (x' += Shy*y)", &t.shy, 0.01f, -10.0f, 10.0f, "%.2f"); // Cizalla Sh_y
                    activado |= ImGui::IsItemActivated(); // Detectar edicion

                    ImGui::SeparatorText("Reflexion"); // Titulo de seccion
                    if (ImGui::Button("Eje X (y -> -y)")) { apilarUndo(figuras); t.refX = !t.refX; } // Alternar reflexion en X
                    ImGui::SameLine(); // Poner el siguiente control en la misma fila
                    if (ImGui::Button("Eje Y (x -> -x)")) { apilarUndo(figuras); t.refY = !t.refY; } // Alternar reflexion en Y
                    ImGui::SameLine(); // Poner el siguiente control en la misma fila
                    if (ImGui::Button("Origen")) { apilarUndo(figuras); t.refX = !t.refX; t.refY = !t.refY; } // Alternar ambas (reflexion por el origen)
                    ImGui::TextDisabled("Activas: X=%s  Y=%s", t.refX ? "si" : "no", t.refY ? "si" : "no"); // Mostrar el estado

                    if (ImGui::Button("Resetear transformacion")) { // Volver a la figura original
                        apilarUndo(figuras); // Guardar para Deshacer
                        bool centro = t.respectoCentro; // Conservar la opcion del pivote
                        t = Transformacion(); // Restaurar los valores por defecto
                        t.respectoCentro = centro; // Restaurar la opcion del pivote
                    }

                    // ---- Visualizador de la matriz homogenea resultante (solo lectura) ----
                    ImGui::SeparatorText("Matriz homogenea M_total (3x3)"); // Titulo de seccion
                    Mat3 M = matrizTotal(f); // Calcular la matriz resultante
                    if (ImGui::BeginTable("matrizTotal", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) { // Tabla 3x3
                        for (int r = 0; r < 3; r++) { // Recorrer las filas
                            for (int c = 0; c < 3; c++) { // Recorrer las columnas
                                ImGui::TableNextColumn(); // Pasar a la siguiente celda
                                ImGui::Text("%9.3f", M.m[r][c]); // Mostrar el valor
                            }
                        }
                        ImGui::EndTable(); // Cerrar la tabla
                    }
                    ImGui::TextDisabled("M = T(t) * T(c) * R * Sh * S * Ref * T(-c)"); // Orden de composicion
                    ImGui::TextDisabled("Origen (0,0) = celda (%d, %d)", origenCol, origenFila); // Mostrar el origen

                    std::vector<Celda> vt = verticesTransformados(f); // Vertices tras la transformacion
                    for (size_t i = 0; i < f.vertices.size() && i < vt.size(); i++) { // Recorrer los vertices
                        ImGui::TextDisabled("V%d (%d,%d) -> (%d,%d)", (int)i + 1, f.vertices[i].first, f.vertices[i].second, vt[i].first, vt[i].second); // Mostrar original y transformado
                    }
                }
            }

            if (activado) apilarUndo(estadoPrevio); // Guardar el estado anterior al empezar a editar un widget
        }

        ImGui::End(); // Cerrar la ventana de la barra
        ImGui::Render(); // Generar los datos de dibujo de ImGui
        // ===== [FIN IMGUI] =====

        actualizarPintado(window); // Rasterizar la escena en la matriz logica y subirla a la GPU

        glClearColor(colorFondo[0], colorFondo[1], colorFondo[2], colorFondo[3]); // [IMGUI] Usar el color de fondo editable
        glClear(GL_COLOR_BUFFER_BIT); // Limpiar la pantalla

        glUseProgram(shaderProgram); // Activar el programa de shaders

        // 1) Grilla (color por uniform)
        glUniform1i(porVerticeLoc, 0); // Indicar que el color viene del uniform
        glUniform4fv(colorLoc, 1, colorGrilla); // Enviar el color de la grilla al shader
        glBindVertexArray(gridVAO); // Activar el VAO de la grilla
        glDrawArrays(GL_LINES, 0, gridVertices.size() / 3); // Dibujar las lineas de la grilla
        glBindVertexArray(0); // Desactivar el VAO

        // 2) Celdas pintadas (color por vertice)
        if (!paintVertices.empty()) { // Comparar si hay celdas pintadas
            glUniform1i(porVerticeLoc, 1); // Indicar que el color viene de cada vertice
            glBindVertexArray(paintVAO); // Activar el VAO del pintado
            glDrawArrays(GL_TRIANGLES, 0, paintVertices.size() / 7); // Dibujar las celdas pintadas
            glBindVertexArray(0); // Desactivar el VAO
        }

        // ===== [IMGUI] Dibujar ImGui ENCIMA de todo lo demas (justo antes de SwapBuffers) =====
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); // Renderizar la barra de herramientas
        // ===== [FIN IMGUI] =====

        glfwSwapBuffers(window); // Mostrar el cuadro dibujado
    } // Cerrar el ciclo principal

    // ===== [IMGUI] Cerrar ImGui (antes de destruir la ventana y GLFW) =====
    ImGui_ImplOpenGL3_Shutdown(); // Cerrar el backend de OpenGL
    ImGui_ImplGlfw_Shutdown(); // Cerrar el backend de GLFW
    ImGui::DestroyContext(); // Destruir el contexto de ImGui
    // ===== [FIN IMGUI] =====

    glDeleteVertexArrays(1, &gridVAO); // Borrar el VAO de la grilla
    glDeleteBuffers(1, &gridVBO); // Borrar el VBO de la grilla
    glDeleteVertexArrays(1, &paintVAO); // Borrar el VAO del pintado
    glDeleteBuffers(1, &paintVBO); // Borrar el VBO del pintado
    glDeleteProgram(shaderProgram); // Borrar el programa de shaders

    glfwDestroyWindow(window); // Destruir la ventana
    glfwTerminate(); // Cerrar GLFW
    return 0; // Retornar el codigo de exito
} // Cerrar la funcion main