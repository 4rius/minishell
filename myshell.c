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
int umask_value;  // Valor del umask de la minishell

// Mandatos internos
int cd(char *dir);
void jobs();
int fg(char *pid);
int chumask(char *mask);
void help();
void salir();

// Manejador de señales
void manejador_sigint();

// Funciones
int inicializar();
void prompt();
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
    system("clear");
    printf("\x1b[35m------- Minishell - Santiago Arias ------\n");

    // Inicializar variables de entorno
    path = getenv("PATH");
    home = getenv("HOME");
 
    // Inicializar variables globales
    pids = (int *)malloc(sizeof(int));
    nombre_procesos = (char **)malloc(sizeof(char *));

    // Inicializar el umask de la minishell
    chumask(0000);
 
    return 0;
}

void prompt() {
    char *buf;  // Para leer el directorio actual

    buf = (char *)malloc(1024 * sizeof(char));
    printf("\033[0;32m %s\x1b[0m:\033[0;31mm$h>\x1b[0m ", getcwd(buf, 1024));  // Imprimir el prompt y el directorio actual
    free(buf);
}

void loop() {
    tline *line;
    
    while (1) {
        prompt();
        // Ignorar CTRL + C
        signal(SIGINT, SIG_IGN);
        line = leer_linea();
        if (line != NULL) {
            // Comprobar si hay algun proceso hijo terminado, útil para jobs y fg
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
    int i, j;
    int status;
    for (i = 0; i < num_procesos; i++) {
        if (waitpid(pids[i], &status, WNOHANG) != 0) { // WNOHANG testea si el hijo pid[i] ha terminado, si hubiera terminado, status nos diría cómo
            // Mantener el orden de los procesos por cómo se ha implementado fg
            for (j = i; j < num_procesos; j++) {
                pids[j] = pids[j + 1];
                nombre_procesos[j] = nombre_procesos[j + 1];
            }
            num_procesos--;
            pids = (int *)realloc(pids, num_procesos + 1 * sizeof(int));
            nombre_procesos = (char **)realloc(nombre_procesos, num_procesos + 1 * sizeof(char *));
        }
    }
}

// Manejador CTRL + C
void manejador_sigint()
{
    // Salir solo del proceso en foreground, no de la minishell
    printf("\n");
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

    // Comprobar si hay algo que redireccionar
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
    } else mandatos = 1;

    // Comprobar si el mandato es interno
    if (strcmp(linea->commands[0].argv[0], "cd") == 0) {
        cd(linea->commands[0].argv[1]);
    } else if (strcmp(linea->commands[0].argv[0], "jobs") == 0) {
        jobs();
    } else if (strcmp(linea->commands[0].argv[0], "fg") == 0) {
        fg(linea->commands[0].argv[1]);
    } else if (strcmp(linea->commands[0].argv[0], "umask") == 0) {
        chumask(linea->commands[0].argv[1]);
    } else if (strcmp(linea->commands[0].argv[0], "exit") == 0) {
        salir();
    } else if (strcmp(linea->commands[0].argv[0], "clear") == 0) {
        system("clear");
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
        if (waitpid(pids[i], &status, WNOHANG) == 0) {  // WNOHANG testea si el hijo pid[i] ha terminado, si no ha terminado, devuelve 0
            printf("[%d]+ Running        %s\n", pids[i], nombre_procesos[i]);
        }
    }
}

// Implementación fg
int fg(char *pid)
{
    int i;
    int pidv;
    int status;
    if (pid != NULL) {
        pidv = atoi(pid);
        for (i = 0; i < num_procesos; i++) { // Para sacar el nombre del proceso
            if (pids[i] == pidv)
                if (waitpid(pidv, &status, WNOHANG) == 0) {  // Comprobar si el proceso sigue activo
                    printf("%s\n", nombre_procesos[i]);
                    waitpid(pidv, &status, 0);
                    return 0;
                }
        }
    } else {
        pidv = pids[num_procesos - 1];
        printf("%s\n", nombre_procesos[num_procesos - 1]);
        waitpid(pidv, &status, 0);
        return 0;
    }

    printf("fg: No hay ningún proceso en segundo plano o no se ha podido ejecutar el pid proporcionado en primer plano.\n");
    return 1;
}

// Implementación umask
int chumask(char *mask)
{
    int mascara;
    if (mask != NULL) {
        mascara = atoi(mask);
        // Comprobar que es una máscara válida
        if (mask < 0 || mask > 0777) {
            printf("La máscara debe estar entre 0 y 777\n");
            return 1;
        }

        // Invertir la máscara para que represente los permisos con los que creamos los archivos
        mascara = 0777 - mascara;

        umask_value = mascara;
    } else printf("Valor de la máscara: %d\n", umask_value);

    return 0;
}

// Implementación exit
void salir()
{
    printf("------------Matando la minishell--------------\n");
    printf("------------Hasta la próxima------------------\n");
    // Liberar memoria
    free(pids);
    free(nombre_procesos);
    // Salir
    exit(0);
}

// Implementación help
void help()
{
    printf("\x1b[35m------- Minishell - Santiago Arias ------\n");
    printf("\x1b[35m----------------- Ayuda -----------------\x1b[0m\n");
    printf("Comandos internos:\n");
    printf("cd [dir] - Cambia el directorio actual a dir. Sin especificar dir, se cambia al directorio HOME.\n");
    printf("jobs - Muestra los procesos en segundo plano.\n");
    printf("fg [pid] - Pone el proceso con el pid indicado en primer plano, o el último proceso mandado a segundo plano si no se da un argumento.\n");
    printf("umask [mask] - Cambia la máscara de permisos de los archivos creados por la minishell, o imprimir la actual.\n");
    printf("exit - Cierra la minishell.\n");
    printf("clear - Limpia la pantalla.\n");
    printf("help - Muestra esta ayuda.\n");
    printf("El resto de comandos se ejecutan como en cualquier shell de Linux.\n");
}

// Ejecutar mandatos externos
int ejecutar_externo(tline *linea, int mandatos, int redireccion, int background)
{
    int i = 0;
    int j = 0;
    int pid;
    int tub[2];
    int in = 0;
    int out = 0;
    int err = 0;
    int status = 0;
    char *lineaenviada;
    lineaenviada = (char *)malloc(1024 * sizeof(char));

    // Comprobar si necesitamos un pipe
    if (mandatos > 1) {
        pipe(tub);
    }

    // Comprobar si hay que redireccionar la entrada o la salida
    if (redireccion == 1) {
        in = open(linea->redirect_input, O_RDONLY);
        if (in == -1) {
            printf("%s: Error. No se pudo abrir el archivo de entrada, %s", linea->commands[0].argv[0], strerror(errno));
            return 1;
        }
    } else if (redireccion == 2) {
        out = creat(linea->redirect_output, umask_value);
        if (out == -1) {
            printf("%s: Error. No se pudo crear el archivo de salida, %s", linea->commands[0].argv[0], strerror(errno));
            return 1;
        }
    } else if (redireccion == 3) {
        err = creat(linea->redirect_error, umask_value);
        if (err == -1) {
            printf("%s: Error. No se pudo crear el archivo de salida de error, %s", linea->commands[0].argv[0], strerror(errno));
            return 1;
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
                close(tub[0]);  // Cerrar el extremo de lectura del pipe
                dup2(tub[1], STDOUT_FILENO);  // Redireccionar la salida estándar al extremo de escritura del pipe
            } else if (redireccion == 1) {
                // Si hay que redireccionar la entrada estándar
                dup2(in, STDIN_FILENO);
            } else if (redireccion == 2) {
                // Si hay que redireccionar la salida
                dup2(out, STDOUT_FILENO);
            } else if (redireccion == 3) {
                // Si hay que redireccionar la salida de error
                dup2(err, STDERR_FILENO);
            }

            // Ejecutar el comando
            execv(linea->commands[i].filename, linea->commands[i].argv);

            printf("%s: Error al ejecutar el comando. %s\n", linea->commands[i].argv[0], strerror(errno));
            exit(1);
        } else { // Proceso padre
            // Añadir el proceso a la lista de procesos
            pids = realloc(pids, (num_procesos + 1) * sizeof(int));  // Añadir espacio para el nuevo proceso
            pids[num_procesos] = pid;  // Guardar el pid del proceso
            nombre_procesos = realloc(nombre_procesos, (num_procesos + 1) * sizeof(char *));  // Añadir espacio para el nuevo proceso
            for (j = 0; j < linea->commands[i].argc; j++) {  // Guardar el nombre completo del proceso
                strcat(lineaenviada, linea->commands[i].argv[j]);
                strcat(lineaenviada, " ");
            }
            nombre_procesos[num_procesos] = (char*)malloc((strlen(lineaenviada) + 1) * sizeof(char));  // Añadir espacio para el nombre del proceso
            strcpy(nombre_procesos[num_procesos], lineaenviada);
            num_procesos++;
            printf(" %s ejecutándose en segundo plano.\n", lineaenviada);
        }
    }
    // Ejecución en foreground
    else {
        signal(SIGINT, manejador_sigint);
        pid = fork();
        if (pid < 0) {
            printf("%s: Error al crear el proceso hijo. %s\n", linea->commands[i].filename, strerror(errno));
            return 1;
        } else if (pid == 0) {
            if (mandatos > 1) {
                close(tub[0]); // Cerrar el extremo de lectura del pipe
                dup2(tub[1], STDOUT_FILENO);
            } else if (redireccion == 1) {
                dup2(in, STDIN_FILENO);
            } else if (redireccion == 2) {
                dup2(out, STDOUT_FILENO);
            } else if (redireccion == 3) {
                dup2(err, STDERR_FILENO);
            }

            // Ejecutar el comando
            execv(linea->commands[i].filename, linea->commands[i].argv);
            
            if (strcmp(strerror(errno), "Bad address \0"))
                printf("%s: No se encuentra el mandatao.\n", linea->commands[i].argv[0]);
                 else printf("%s: Error al ejecutar el mandato.\n", linea->commands[i].argv[0]);

            exit(1);
        } else {

            // Esperar a que termine el proceso hijo
            wait(&status);

            // Comprobar si el proceso hijo ha terminado correctamente
            if (WIFEXITED(status) != 0) {
                if (WEXITSTATUS(status) != 0) {
                    printf("%s: Error al ejecutar el mandato.\n", linea->commands[i].argv[0]);
                    return 1;
                    }
                }
            }
    }

    // Liberar la línea reservada
    free(lineaenviada);

    return 0;
}