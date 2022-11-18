#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "parser.h"

// Variables de entorno
char *path;
char *home;

// Variables globales
int *pids;  // Pids de los procesos hijos (background)
char **nombre_procesos; // Nombres de los procesos hijos (background)
int num_procesos = 0; // Numero de procesos hijos (background)
int pidfg = 0; // Pid del proceso foreground
int umask_value;

// Mandatos internos
int cd(char *dir);
void jobs();
int fg(int pid);
int bg(int pid);
int chumask(int mask);
void help();
void clear();
void salir();

// Manejadores de señales
void manejador_sigint();

// Funciones
int inicializar();
void loop();
tline *leer_linea();
int procesar(tline *linea);
int ejecutar_externo(tline *linea, int mandatos, int redireccion, int background);
void comprobar_procesos_terminados();



int main() {
    // Inicializar variables de entorno
    inicializar();

    // Comprobar si se han establecido correctamente las variables de entorno
    if (path == NULL || home == NULL) {
        printf("Error al inicializar la minishell\n");
        return 1;
    }
    // Loop principal
    loop();
 
    return 0;
}

int inicializar() {
    printf("\x1b[35m---------------- Minishell - Santiago Arias --------------\n");

    // Inicializar variables de entorno
    path = getenv("PATH");
    home = getenv("HOME");
 
    // Inicializar variables globales
    pids = (int *)malloc(sizeof(int));
    nombre_procesos = (char **)malloc(sizeof(char *));

    // Deshabilitar el comportamiento por defecto de SIGINT (CTRL+C)
    signal(SIGINT, manejador_sigint);

    // Inicializar el umask de la minishell
    chumask(0000);
 
    return 0;
}

void manejador_sigint() {
    // Solo cortar un proceso hijo si hay alguno en ejecucion
    if (pidfg != 0) {
        kill(pidfg, SIGINT);
    } 
    printf("\n");
    // Si no hay ningun proceso hijo en ejecucion, no hacer nada, solo volver a la linea de comandos
    return;
}

void loop() {
    tline *line;
    char *buf;  // Buffer para colocar el directorio actual
 
    while (1) {
        // Comprobar si hay algun proceso hijo terminado
        buf = (char *)malloc(1024 * sizeof(char));
        printf("\033[0;32m %s - \033[0;31mmsh>\x1b[0m ", getcwd(buf, 1024));  // Imprimir el prompt y el directorio actual
        free(buf);
        line = leer_linea();
        if (line == NULL) {
            continue;
        } else {
            comprobar_procesos_terminados();
            procesar(line);
        }
    }
}

tline *leer_linea() {
    char buffer[1024];
    tline *line;
    // Leer línea de entrada estándar
    fgets(buffer, 1024, stdin);
    // Hacer que no se rompa si se introduce una cadena vacía
    if (buffer[0] == '\n') {
        return NULL;
    }
    // Tokenizar línea
    line = tokenize(buffer);
    // Devolver línea tokenizada
    return line;
}

// Comprobar si hay algun proceso hijo terminado
void comprobar_procesos_terminados() {
    int i;
    int status;
    for (i = 0; i < num_procesos; i++) {
        if (waitpid(pids[i], &status, WNOHANG) != 0) { // WNOHANG testea si el hijo pid[i] ha terminado
            // Eliminar el proceso de la lista de procesos hijos y redimensionar el array
            printf("\n[%d] %s terminado\n", pids[i], nombre_procesos[i]);
            pids[i] = pids[num_procesos - 1];  // Copiar el ultimo elemento en el hueco del proceso terminado, para no dejar huecos en el array y poder usar realloc
            nombre_procesos[i] = nombre_procesos[num_procesos - 1];
            pids = (int *)realloc(pids, sizeof(int) * (num_procesos - 1));
            nombre_procesos = (char **)realloc(nombre_procesos, sizeof(char *) * (num_procesos - 1));
            num_procesos--;
        }
    }
}

int procesar(tline *linea)
{
    int mandatos;
    int redireccion;
    int background;

    // Comprobar si hay que ejecutar en background
    if (linea->background) {
        background = 1;
    } else background = 0;

    // Comprobar si hay que redireccionar la entrada y ek tipo de redireccion
    if (linea->redirect_input != NULL) {
        redireccion = 1;
    } else if (linea->redirect_output != NULL) {
        redireccion = 2;
    } else if (linea->redirect_error != NULL) {
        redireccion = 3;
    } else redireccion = 0;

    // Comprobar si hay que ejecutar más de un mandato (pipe)
    if (linea->ncommands > 1) {
        mandatos = linea->ncommands;
    } else mandatos = 0;

    // Comprobar si el mandato es interno
    if (strcmp(linea->commands[0].argv[0], "cd") == 0) {
        cd(linea->commands[0].argv[1]);
    } else if (strcmp(linea->commands[0].argv[0], "jobs") == 0) {
        jobs();
    } else if (strcmp(linea->commands[0].argv[0], "fg") == 0) {
        fg(atoi(linea->commands[0].argv[1]));
    } else if (strcmp(linea->commands[0].argv[0], "umask") == 0) {
        chumask(atoi(linea->commands[0].argv[1]));
    } else if (strcmp(linea->commands[0].argv[0], "exit") == 0) {
        salir();
    } else if (strcmp(linea->commands[0].argv[0], "clear") == 0) {
        clear();
    } else if(strcmp(linea->commands[0].argv[0], "help") == 0) {
        help();
    } else ejecutar_externo(linea, mandatos, redireccion, background); // Ejecutar mandato externo

    return 0;

}

// Implementación cd
int cd(char *dir)
{
    if (dir == NULL) {
        if (chdir(home) == -1) {
            perror("cd");
        }
    } else if (chdir(dir) != 0) {
        printf("cd: Error: no se ha podido cambiar al directorio %s\n", dir);
        return 1;
    }

    return 0;
}

// Implementación jobs
void jobs()
{
    int i;
    int status;
    for (i = 0; i < num_procesos; i++) {
        // Comprobar si el proceso está activo
        if (waitpid(pids[i], &status, WNOHANG) != 0) {
            printf("[%d]+ Running        %s\n", pids[i], nombre_procesos[i]);
        }
    }
}

// Implementación fg
int fg(int pid)
{
    int i;
    int status;
    for (i = 0; i < num_procesos; i++) {
        // Comprobar si el proceso está activo
        if (waitpid(pids[i], &status, WNOHANG) != 0) {  // kill devuelve 0 si el proceso está activo, kill con un 0 no mata el proceso, solo comprueba si está activo
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
    printf("------------Matando la minishell--------------\n");
    printf("------------Hasta la próxima------------------\n");
    // Liberar memoria y terminar procesos en segundo plano
    free(pids);
    free(nombre_procesos);
    // Salir
    exit(0);
}

// Implementación help
void help()
{
    printf("-----------------Ayuda-----------------\n");
    printf("Comandos internos:\n");
    printf("cd [dir] - Cambia el directorio actual a dir. Sin especificar dir, se cambia al directorio HOME.\n");
    printf("jobs - Muestra los procesos en segundo plano.\n");
    printf("fg [pid] - Pone el proceso con el pid indicado en primer plano.\n");
    printf("bg [pid] - Pone el proceso con el pid indicado en segundo plano.\n");
    printf("umask [mask] - Cambia la máscara de permisos de los archivos creados por la minishell.\n");
    printf("exit - Cierra la minishell.\n");
    printf("clear - Limpia la pantalla.\n");
    printf("help - Muestra esta ayuda.\n");
}

// Ejecutar mandatos externos
int ejecutar_externo(tline *linea, int mandatos, int redireccion, int background)
{
    int i = 0;
    int pid;
    int fd[2];
    int in = 0;
    int out = 0;
    int err = 0;
    int status = 0;

    // Comprobar si necesitamos un pipe
    if (mandatos > 1) {
        pipe(fd);
    }

    // Comprobar si hay que redireccionar la entrada o la salida
    if (redireccion == 1) {
        in = open(linea->redirect_input, O_RDONLY);
        if (in == -1) {
            printf("%s: Error. No se pudo abrir el archivo de entrada, %s", linea->redirect_input, strerror(errno));
        }
    } else if (redireccion == 2) {
        out = creat(linea->redirect_output, umask_value);
        if (out == -1) {
            printf("%s: Error. No se pudo crear el archivo de salida, %s", linea->redirect_output, strerror(errno));
        }
    } else if (redireccion == 3) {
        err = creat(linea->redirect_error, umask_value);
        if (err == -1) {
            printf("%s: Error. No se pudo crear el archivo de error, %s", linea->redirect_error, strerror(errno));
        }
    }

    // Ejecutar en foreground o background
    // Ejecución en background
    if (background) {
        pid = fork();
        if (pid < 0) {
            printf("%s: Error al crear el proceso hijo. %s\n", linea->commands[i].filename, strerror(errno));
            return 1;
        } else if (pid == 0) { // Proceso hijo
            // Ejecutar el comando
            if (mandatos > 1) {
                // Si hay más de un comando, redireccionar la salida del primer comando al pipe
                close(fd[0]);  // Cerrar el extremo de lectura del pipe
                dup2(fd[1], 1);  // Redireccionar la salida estándar al extremo de escritura del pipe
            } else if (redireccion == 1) {
                // Si hay que redireccionar la entrada estándar
                dup2(in, 0);
            } else if (redireccion == 2) {
                // Si hay que redireccionar la salida
                dup2(out, 1);
            } else if (redireccion == 3) {
                // Si hay que redireccionar la salida de error
                dup2(err, 2);
            } else {
                // Cerrar los descriptores de fichero que no se usan y el pipe
                close(fd[0]);
                close(fd[1]);
                close(in);
                close(out);
                close(err);
            }

            // Ejecutar el comando
            execvp(linea->commands[i].filename, linea->commands[i].argv);

            //Si había pipe, ejecutar el resto de comandos
            while (mandatos > 1) {
                i++;
                // Cerrar el extremo de escritura del pipe
                close(fd[1]);
                // Redireccionar la entrada estándar al extremo de lectura del pipe
                dup2(fd[0], 0);
                // Si no es el último comando, crear otro pipe
                if (i < mandatos) {
                    pipe(fd);
                    // Redireccionar la salida estándar al extremo de escritura del pipe
                    dup2(fd[1], 1);
                }
                // Ejecutar el comando
                execvp(linea->commands[i].filename, linea->commands[i].argv);
                mandatos--;  // Decrementar el número de mandatos
            }

            printf("%s: Error al ejecutar el comando. %s\n", linea->commands[i].filename, strerror(errno));
            exit(1);
        } else { // Proceso padre
            // Añadir el proceso a la lista de procesos
            pids = realloc(pids, (num_procesos + 1) * sizeof(int));
            pids[num_procesos] = pid;
            nombre_procesos = realloc(nombre_procesos, (num_procesos + 1) * sizeof(char *));
            nombre_procesos[num_procesos] = (char*)malloc((strlen(linea->commands[i].argv[0]) + 1) * sizeof(char));
            strcpy(nombre_procesos[num_procesos], linea->commands[i].argv[0]);
            num_procesos++;
        }
    }
    // Ejecución en foreground
    else {
        pid = fork();
        if (pid < 0) {
            printf("%s: Error al crear el proceso hijo. %s\n", linea->commands[i].filename, strerror(errno));
            return 1;
        } else if (pid == 0) {
            if (mandatos > 1) {
                close(fd[0]);
                dup2(fd[1], 1);
            } else if (redireccion == 1) {
                dup2(in, 0);
            } else if (redireccion == 2) {
                dup2(out, 1);
            } else if (redireccion == 3) {
                dup2(err, 2);
            }

            // Ejecutar el comando
            execvp(linea->commands[i].filename, linea->commands[i].argv);

            //Si había pipe, ejecutar el resto de comandos
            while (mandatos > 1) {
                i++;
                close(fd[1]);
                dup2(fd[0], 0);
                if (i < mandatos) {
                    pipe(fd);
                    dup2(fd[1], 1);
                }
                execvp(linea->commands[i].filename, linea->commands[i].argv);
                mandatos--;
            }

            printf("%s: Error al ejecutar el comando. %s\n", linea->commands[i].filename, strerror(errno));
            exit(1);
        } else {
            // Activar el proceso en foreground activo
            pidfg = pid;

            // Esperar a que termine el proceso hijo
            wait(&status);

            // Desactivar el proceso en foreground activo
            pidfg = 0;

            // Comprobar si el proceso hijo ha terminado correctamente
            if (WIFEXITED(status) != 0) {
                if (WEXITSTATUS(status) != 0) {
                    printf("%s: Error al ejecutar el comando. %s\n", linea->commands[i].filename, strerror(errno));
                    return 1;
                    }
                }
            }
    }

    return 0;
}