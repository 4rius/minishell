#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include "parser.h"

//Variables globales
char path[1024];
char home[1024];
// Comandos implementados en la propia shell
char *internos[] = {"cd", "jobs", "fg", "bg", "exit", "chumask", "clear", "help"};
// Procesos en ejecución
tline *procesos[1024];
int numprocesos = 0;
// Semáforo para proteger la variable numprocesos
sem_t semaforo;

// Funciones
int ejecutar(char *comando, char *argv[], int argc, int background, int redir_in, int redir_out, int redir_err);
int ejecutar_interno(char *comando, char *argv[], int argc, int background);
int ejecutar_externo(char *comando, char *argv[], int argc, int background);
int ejecutar_redireccion(char *comando, char *argv[], int argc, int background, int tiporedir, char *fichero);
//int ejecutar_tuberia(char *comando1, char *argv1[], int argc1, char *comando2, char *argv2[], int argc2);
void loop();
void manejador_interrupciones(int sig);
void help();
int iniciar_shell();
int cd(char *path);
void jobs();
void fg();
void bg();
void exitshell();
int chumask(int mask);
void clear();
void help();

// Funcion principal
int main()
{
    iniciar_shell();
    loop();
    return 0;
}

// Funcion que inicializa la shell
int iniciar_shell()
{
    // Inicializar semáforo
    sem_init(&semaforo, 0, 1);

    printf("------ MINISHELL - Santiago Arias -------\n");

    // Inicializar variables de entorno
    strcpy(path, getenv("PATH"));
    strcpy(home, getenv("HOME"));
    signal(SIGINT, manejador_interrupciones);
    signal(SIGQUIT, manejador_interrupciones);

    // Inicializar variables de entorno
    setenv("PATH", path, 1);
    setenv("HOME", home, 1);

    return 0;

    // Ver si se ha producido un error
    if (errno != 0)
    {
        perror("Error");
        exit(EXIT_FAILURE);
    }
}

// Funcion que ejecuta el bucle de la shell
void loop()
{
    tline *linea; // Linea de comandos
    char buffer[1024]; // Buffer para leer la linea de comandos
    int nummandatos; // Numero de mandatos
    int i; // Contador
    int fd; // Descriptor de fichero
    int tipo; // Tipo de mandato (redireccion, pipe)

    while (1)
    {
        // Mostrar el prompt
        printf("msh> ");
        fflush(stdout);

        // Leer la linea de comandos de la entrada estandar
        if (fgets(buffer, 1024, stdin) == NULL) linea = tokenize(buffer);

        // Si no hay comandos, volver a pedirlo
        if (linea == NULL)
        {
            continue;
        }

        nummandatos = linea->ncommands;

        // Ejecutar la linea de mandatos
        for (i = 0; i < nummandatos; i++)
        {
            ejecutar(linea->commands[i].filename, linea->commands[i].argv, linea->commands[i].argc, linea->background, linea->redirect_input, linea->redirect_output, linea->redirect_error);
        }


        // Si no se ejecuta en segundo plano, esperar a que acabe
        if (!linea->background)
        {
            wait(NULL);
        }

        // Liberar memoria
        free(linea);
    }
}

void manejador_interrupciones(int sig)
{
    //Hacer que ctrl c no mate la shell, solo el proceso que se este ejecutando
    if (sig == SIGINT)
    {
        fflush(stdout);
    }
}

// Funcion que ejecuta un comando
int ejecutar(char *comando, char *argv[], int argc, int background, int redir_in, int redir_out, int redir_err)
{
    int i; // Contador
    // Comando tiene la ruta del mandato

    // Comprobar si es un comando interno
    for (i = 0; i < 8; i++)
    {
        if (strcmp(argv[0], internos[i]) == 0)  // Usamos argv[0] porque es el nombre del mandato, y prevenimos ejecutar un comando interno con su comando externo
        {
            ejecutar_interno(comando, argv, argc, background);
            return 0;
        }
    }

    // Comprobar si es un comando externo
    if (access(comando, X_OK) == 0)
    {
        ejecutar_externo(comando, argv, argc, background);
        return 0;
    }

    // Comprobar si es un comando con redirección
    if (redir_err != NULL || redir_out != NULL || redir_in != NULL)
    {
        if (redir_err != NULL)
        {
            ejecutar_redireccion(comando, argv, argc, background, 0, redir_err);
            return 0;
        } else if (redir_out != NULL)
        {
            ejecutar_redireccion(comando, argv, argc, background, 1, redir_out);
            return 0;
        } else if (redir_in != NULL)
        {
            ejecutar_redireccion(comando, argv, argc, background, 2, redir_in);
            return 0;
        }
    }

    // Comprobar si es un comando con tubería
    for (i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "|") == 0)
        {
            //ejecutar_tuberia(comando, argv, argc, argv[i+1], argv+i+1, argc-i-1);
            return 0;
        }
    }

    // Si no es ninguno de los anteriores, mostrar error
    printf("%s: No se encuentra el mandato\n", comando);
    return 1;
}

// Funcion que ejecuta un comando interno
int ejecutar_interno(char *comando, char *argv[], int argc, int background)
{
    int i; // Contador
    int pid; // PID del proceso hijo
    int status; // Estado del proceso hijo

    // Mandatos que no se pueden ejecutar en segundo plano
    if (background)
    {
        if (strcmp(comando, "cd") == 0 || strcmp(comando, "jobs") == 0 || strcmp(comando, "fg") == 0 || strcmp(comando, "bg") == 0)
        {
            printf("%s: No se puede ejecutar en segundo plano\n", comando);
            return 1;
        }
    }

    // Ejecutar el comando
    if (strcmp(comando, "cd") == 0)
    {
        cd(argv[1]);
        return 0;
    }
    else if (strcmp(comando, "jobs") == 0)
    {
        jobs();
        return 0;
    }
    else if (strcmp(comando, "fg") == 0)
    {
       
    }
    else if (strcmp(comando, "bg") == 0)
    {
        
    }
    else if (strcmp(comando, "exit") == 0)
    {
        exitshell();
    }
}

// Implementación de los comandos internos
int cd(char *directorio)
{
    if (directorio == NULL)
    {
        chdir(getenv("HOME"));
    }
    else
    {
        if (chdir(directorio) == -1)
        {
            printf("cd: No se ha podido cambiar de directorio\n");
        }
    }
    return 0;
}

// Implementación de jobs
void jobs()
{
    int i, j; // Contador
    int pid; // PID del proceso hijo
    int status; // Estado del proceso hijo
    int argc; // Número de argumentos

    //Hacer que jobs muera con ctrl c
    signal(SIGINT, SIG_DFL); // Restaurar la señal por defecto

    // Recorrer la lista de procesos
    for (i = 0; i < numprocesos; i++)
    {
        // Hay que sacar el PID del proceso hijo
        pid = waitpid(procesos[i]->commands->argv[0], &status, WNOHANG);
        if (pid == 0) //Está en ejecución
        {
            printf("[%d]+ Running %s", i, procesos[i]->commands->argv[0]);
            for (j = 1; j < procesos[i]->commands->argc; j++)
            {
                printf(" %s", procesos[i]->commands->argv[j]);
            }
            printf("\n");
        } else if (pid == -1) //Ha terminado
        {
            printf("[%d]+ Done %s", i, procesos[i]->commands->argv[0]);
            for (j = 1; j < procesos[i]->commands->argc; j++)
            {
                printf(" %s", procesos[i]->commands->argv[j]);
            }
            printf("\n");
        }
    }
}

// Implementación de fg
void fg()
{
    int i; // Contador
    int pid; // PID del proceso hijo
    int status; // Estado del proceso hijo

    // Recorrer la lista de procesos
    for (i = 0; i < numprocesos; i++)
    {
        // Hay que sacar el PID del proceso hijo
        pid = waitpid(procesos[i]->commands->argv[0], &status, WNOHANG); // WNOHANG para que no se quede esperando
        if (pid == 0) //Está en ejecución
        {
            // Esperar a que acabe
            waitpid(procesos[i]->commands->argv[0], &status, 0);
            return;
        }
    }
}

// Implementación de bg
void bg()
{
    int i; // Contador
    int pid; // PID del proceso hijo
    int status; // Estado del proceso hijo

    // Recorrer la lista de procesos
    for (i = 0; i < numprocesos; i++)
    {
        // Hay que sacar el PID del proceso hijo
        pid = waitpid(procesos[i]->commands->argv[0], &status, WNOHANG); // WNOHANG para que no se quede esperando
        if (pid == 0) //Está en ejecución
        {
            // Enviar señal SIGCONT para reanudar el proceso en segundo plano
            kill(procesos[i]->commands->argv[0], SIGCONT);
            return;
        }
    }
}

//implementación de umask
int chumask(int mask)
{
    // Implementación de umask
    // Comprobar que la máscara es un valor octal
    if (mask < 0 || mask > 0777)
    {
        printf("chumask: La máscara debe ser un valor octal\n");
        return 1;
    }

    // Cambiar la máscara
    umask(mask);
    // Comprobar que se ha cambiado
    if (umask(mask) != mask)
    {
        printf("chumask: No se ha podido cambiar la máscara\n");
        return 1;
    }
    printf("chumask: La nueva máscara es %o\n", mask);
    return 0;
}

// Ayuda
void help()
{
    printf("------ MINISHELL - Santiago Arias -------\n");
    printf("------ Sección de ayuda ------\n");
    printf("Comandos internos:\n");
    printf("cd [directorio] - Cambia el directorio actual\n");
    printf("jobs - Muestra los procesos en ejecución\n");
    printf("fg - Pone en primer plano el último proceso en ejecución\n");
    printf("bg - Pone en segundo plano el último proceso en ejecución\n");
    printf("exit - Cierra la minishell\n");
    printf("help - Muestra esta ayuda\n");
    printf("clear - Limpia la pantalla\n");
    printf("umask [mascara] - Cambia la máscara de permisos\n");
    printf("Comandos externos:\n");
    printf("Todos los comandos que se encuentren en el PATH\n");
    return;
}

// Limpia la pantalla
void clear()
{
    printf("\033[H\033[J");
    return;
}

// Función para ejecutar comandos externos
int ejecutar_externo(char *comando, char *argv[], int argc, int background)
{
    int pid; // PID del proceso hijo
    int status; // Estado del proceso hijo

    // Comprobar si el comando se ejecuta en segundo plano
    if (background == 1)
    {
        // Crear un proceso hijo
        pid = fork();
        if (pid == -1)
        {
            printf("%s: No se ha podido crear el proceso hijo\n", argv[0]);
            return 1;
        }
        else if (pid == 0) // Proceso hijo
        {
            // Ejecutar el comando
            execvp(comando, argv);
            printf("%s: No se ha podido ejecutar el comando\n", argv[0]);
            return 1;
        }
        else // Proceso padre
        {
            // Añadir el proceso a la lista de procesos
            procesos[numprocesos]->commands->argv = argv;
            procesos[numprocesos]->commands->argc = argc;
            numprocesos++;
            return 0;
        }
    }
    else // Se ejecuta en primer plano
    {
        // Crear un proceso hijo
        pid = fork();
        if (pid == -1)
        {
            printf("%s: No se ha podido crear el proceso hijo\n", argv[0]);
            return 1;
        }
        else if (pid == 0) // Proceso hijo
        {
            // Ejecutar el comando
            execvp(comando, argv);
            printf("%s: No se ha podido ejecutar el comando\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        else // Proceso padre
        {
            // Esperar a que acabe el proceso hijo
            waitpid(pid, &status, 0);
            return 0;
        }
    }
}

// Función para ejecutar comandos con redirección
int ejecutar_redireccion(char *comando, char *argv[], int argc, int background, int tiporedir, char *fichero)
{
    int pid; // PID del proceso hijo
    int status; // Estado del proceso hijo
    int fd; // Descriptor de fichero

    // Comprobar si el comando se ejecuta en segundo plano
    if (background == 1)
    {
        // Crear un proceso hijo
        pid = fork();
        if (pid == -1)
        {
            printf("%s: No se ha podido crear el proceso hijo\n", argv[0]);
            return 1;
        }
        else if (pid == 0) // Proceso hijo
        {
            // Comprobar el tipo de redirección
            if (tiporedir == 1) // Redirección de entrada
            {
                // Abrir el fichero
                fd = open(fichero, O_RDONLY);
                if (fd == -1)
                {
                    printf("%s: No se ha podido abrir el fichero\n", argv[0]);
                    return 1;
                }
                // Redirigir la entrada estándar
                dup2(fd, STDIN_FILENO);
                // Cerrar el fichero
                close(fd);
            }
            else if (tiporedir == 2) // Redirección de salida
            {
                // Abrir el fichero
                fd = open(fichero, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1)
                {
                    printf("%s: No se ha podido abrir el fichero\n", argv[0]);
                    return 1;
                }
                // Redirigir la salida estándar
                dup2(fd, STDOUT_FILENO);
                // Cerrar el fichero
                close(fd);
            }
            else if (tiporedir == 3) // Redirección de error
            {
                // Abrir el fichero
                fd = open(fichero, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1)
                {
                    printf("%s: No se ha podido abrir el fichero\n", argv[0]);
                    return 1;
                }
                // Redirigir la salida de error
                dup2(fd, STDERR_FILENO);
                // Cerrar el fichero
                close(fd);
            }
            // Ejecutar el comando
            execvp(comando, argv);
            printf("%s: No se ha podido ejecutar el comando\n", argv[0]);
            return 1;
        }
        else // Proceso padre
        {
            // Añadir el proceso a la lista
            procesos[numprocesos]->commands->argv = argv;
            procesos[numprocesos]->commands->argc = argc;
            numprocesos++;
            return 0;
        }
    }
    else
    {
        // Crear un proceso hijo
        pid = fork();
        if (pid == -1)
        {
            printf("%s: No se ha podido crear el proceso hijo\n", argv[0]);
            return 1;
        }
        else if (pid == 0) // Proceso hijo
        {
            // Comprobar el tipo de redirección
            if (tiporedir == 1) // Redirección de entrada
            {
                // Abrir el fichero
                fd = open(fichero, O_RDONLY);
                if (fd == -1)
                {
                    printf("%s: No se ha podido abrir el fichero\n", argv[0]);
                    return 1;
                }
                // Redirigir la entrada estándar
                dup2(fd, STDIN_FILENO);
                // Cerrar el fichero
                close(fd);
            }
            else if (tiporedir == 2) // Redirección de salida
            {
                // Abrir el fichero
                fd = open(fichero, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1)
                {
                    printf("%s: No se ha podido abrir el fichero\n", argv[0]);
                    return 1;
                }
                // Redirigir la salida estándar
                dup2(fd, STDOUT_FILENO);
                // Cerrar el fichero
                close(fd);
            }
            else if (tiporedir == 3) // Redirección de error
            {
                // Abrir el fichero
                fd = open(fichero, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1)
                {
                    printf("%s: No se ha podido abrir el fichero\n", argv[0]);
                    return 1;
                }
                // Redirigir la salida de error
                dup2(fd, STDERR_FILENO);
                // Cerrar el fichero
                close(fd);
            }
            // Ejecutar el comando
            execvp(comando, argv);
            printf("%s: No se ha podido ejecutar el comando\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        else // Proceso padre
        {
            // Esperar a que acabe el proceso hijo
            waitpid(pid, &status, 0);
            return 0;
        }
    }
}

//Función de salida
void exitshell()
{
    int i, j;
    // Bucle para liberar la memoria
    for (i = 0; i < numprocesos; i++)
    {
        for (j = 0; j < procesos[i]->ncommands; j++)
        {
            free(procesos[i]->commands[j].argv);
        }
        free(procesos[i]->commands);
        free(procesos[i]);
    }
    free(procesos);
    exit(EXIT_SUCCESS);
}