#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "parser.h"


// Variables de entorno
char *path;
char *home;

// Variables globales
int *pids;
char **pids_names;
int umask_value;

// Mandatos internos
int cd(char *dir);
void jobs();
int fg(int pid);
int bg(int pid);
int chumask(int mask);
void clear();
void salir();

// Manejadores de señales
void manejador_sigint(int sig);
// Manejador de sigint para los hijos, en ese caso sí que se mata el proceso dentro de la minishell
void manejador_sigint_hijos(int sig);

// Funciones
int inicializar();
void loop();
tline *leer_linea();
int ejecutar(tline *linea);
int ejecutar_externo(tline *linea, int mandatos, int redireccion, int background);



int main() {
    // Inicializar variables de entorno
    inicializar();

    // Comprobar si se han establecido correctamente las variables de entorno
    if (path == NULL || home == NULL) {
        printf("Error al inicializar variables de entorno\n");
        return 1;
    }
    // Loop principal
    loop();
 
    return 0;
}

int inicializar() {
    printf("---------------- Minishell - Santiago Arias --------------\n");

    // Inicializar variables de entorno
    path = getenv("PATH");
    home = getenv("HOME");
 
    // Inicializar variables globales
    pids = malloc(sizeof(int));
    pids_names = malloc(sizeof(char *));

    // Deshabilitar el comportamiento por defecto de SIGINT (CTRL+C)
    signal(SIGINT, manejador_sigint);

    // Inicializar nuestro umask local
    chumask(0666);
 
    return 0;
}

void manejador_sigint(int sig) {
    // No hacer nada
}

void loop() {
    tline *line;
 
    while (1) {
        printf("msh> ");
        line = leer_linea();
        if (line == NULL) {
            continue;
        } else ejecutar(line);
    }
}

tline *leer_linea() {
    char buffer[1024];
    tline *line;
    // Leer línea de entrada estándar
    fgets(buffer, 1024, stdin);
    // Tokenizar línea
    line = tokenize(buffer);
    // Devolver línea tokenizada
    return line;
}

int ejecutar(tline *linea)
{
    int mandatos;
    int redireccion;
    int background;

    // Comprobar si hay que ejecutar en background
    if (linea->background) {
        background = 1;
    } else background = 0;

    // Comprobar si hay que redireccionar la entrada
    if (linea->redirect_input != NULL) {
        redireccion = 1;
    } else redireccion = 0;

    // Comprobar si hay que redireccionar la salida
    if (linea->redirect_output != NULL) {
        redireccion = 2;
    } else redireccion = 0;

    // Comprobar si hay que redireccionar la salida de error
    if (linea->redirect_error != NULL) {
        redireccion = 3;
    } else redireccion = 0;

    // Comprobar si hay que ejecutar más de un mandato (pipe)
    if (linea->ncommands > 1) {
        mandatos = 1;
    } else mandatos = 0;

    // Comprobar si el mandato es interno
    if (strcmp(linea->commands[0].filename, "cd") == 0) {
        cd(linea->commands[0].argv[1]);
    } else if (strcmp(linea->commands[0].filename, "jobs") == 0) {
        jobs();
    } else if (strcmp(linea->commands[0].filename, "fg") == 0) {
        fg(atoi(linea->commands[0].argv[1]));
    } else if (strcmp(linea->commands[0].filename, "bg") == 0) {
        bg(atoi(linea->commands[0].argv[1]));
    } else if (strcmp(linea->commands[0].filename, "umask") == 0) {
        chumask(atoi(linea->commands[0].argv[1]));
    } else if (strcmp(linea->commands[0].filename, "exit") == 0) {
        salir();
    } else if (strcmp(linea->commands[0].filename, "clear") == 0) {
        clear();
    } else ejecutar_externo(linea, mandatos, redireccion, background); // Ejecutar mandato externo

    return 0;

}

// Implementación cd
int cd(char *dir)
{
    if (dir == NULL) {
        chdir(home);
    } else if (chdir(dir) != 0) {
        printf("No se ha podido cambiar al directorio %s\n", dir);
        return 1;
    }

    return 0;
}

// Implementación jobs
void jobs()
{
    int i;
    for (i = 0; i < sizeof(pids); i++) {
        // Comprobar si el proceso está activo
        if (kill(pids[i], 0) == 0) {  // kill devuelve 0 si el proceso está activo, kill con un 0 no mata el proceso, solo comprueba si está activo
            printf("[%d]+ Running %s\n", pids[i], pids_names[i]);
        }
    }
}

// Implementación fg
int fg(int pid)
{
    int i;
    for (i = 0; i < sizeof(pids); i++) {
        // Comprobar si el proceso está activo
        if (kill(pids[i], 0) == 0) {
            // Comprobar si el proceso es el que se quiere poner en primer plano
            if (pids[i] == pid) {
                // Poner el proceso en primer plano
                kill(pids[i], SIGCONT);  // SIGCONT para reanudar el proceso
                waitpid(pids[i], NULL, 0);  // Esperar a que el proceso termine
                return 0;
            }
        }
    }

    printf("No se ha encontrado el proceso %d\n", pid);
    return 1;
}

// Implementación bg
int bg(int pid)
{
    int i;
    for (i = 0; i < sizeof(pids); i++) {
        // Comprobar si el proceso está activo
        if (kill(pids[i], 0) == 0) {
            // Comprobar si el proceso es el que se quiere poner en segundo plano
            if (pids[i] == pid) {
                // Poner el proceso en segundo plano
                kill(pids[i], SIGCONT);
                return 0;
            }
        }
    }

    printf("No se ha encontrado el proceso %d\n", pid);
    return 1;
}

// Implementación umask
int chumask(int mask)
{
    // Comprobar que es una máscara válida
    if (mask < 0 || mask > 0777) {
        printf("La máscara debe estar entre 0 y 777\n");
        return 1;
    }

    umask_value = mask;
    return 0;
}

// Implementación clear
void clear()
{
    printf("\033[H\033[J");  // Secuencia de escape ANSI para limpiar la pantalla
}

// Implementación exit
void salir()
{
    // Mensaje de despedida
    printf("------------Matando la minishell--------------\n");
    // Liberar memoria
    free(pids);
    free(pids_names);
    // Salir
    exit(0);
}