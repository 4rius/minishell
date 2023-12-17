//Autor: Santiago Arias Paniagua
//Compilación: gcc -Wall -Wextra myshell.c libparser.a -o myshell -static

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

// Variables globales
pid_t *pids;                    // Pids de los procesos hijos (background)
char **nombre_procesos;         // Nombres de los procesos hijos (background)
pid_t pidfg;                    // Gaurdar el pid que se ejecuta en foreground, para que funcione el manejador
int num_procesos = 0;           // Numero de procesos hijos (background)
int umask_val;                  // El umask con el que la minishell crea los ficheros

// Mandatos internos
void cd(char *dir);
void jobs();
void fg(char *identificador);
void chumask(char *mask);
void help();
void salir();

// Manejador de señales
void manejador_sigint();

// Funciones
void prompt();
void loop();
tline *leer_linea();
void ejecutar_interno(tline *linea);
void ejecutar_externo(tline *linea);
void ejecutar_pipe(tline *linea, int restantes, int entrada);
int ficheroredireccion(tline *linea, int tipo);
void comprobar_procesos_terminados();

//Función principal, inicializar la variable de entorno, los arrays de memoria dinámica y el umask de la minishell
int main() {
    system("clear");

    // Inicializar variables globales de memoria dinámica
    pids = (pid_t *)malloc(sizeof(pid_t));
    nombre_procesos = (char **)malloc(sizeof(char *));

    // Inicializar el umask de la minishell, por defecto: archivos: 644 (rw-r--r--) directorios: 755 (rwxr-xr-x)
    chumask("022");

    printf("\x1b[35m------- Minishell - Santiago Arias ------\n");
    // Loop principal
    loop();
 
    return 0;
}

void prompt() {
    char *buf;  // Para leer el directorio actual
    buf = (char *)malloc(1024 * sizeof(char));

    printf("\033[0;32m %s\x1b[0m:\033[0;31mm$h>\x1b[0m ", getcwd(buf, 1024));  // Imprimir el prompt y el directorio actual con colores
    free(buf);
}

void loop() {
    tline *line;
    
    while (1) {
        prompt();                   // Imprimir el prompt
        signal(SIGINT, SIG_IGN);    // Ignorar CTRL + C

        line = leer_linea();
        if (line != NULL) {
            comprobar_procesos_terminados();
            waitpid(-1, NULL, WNOHANG); // Limpiar los procesos zombies que queden, normalmente después de pipes
            ejecutar_interno(line);
        }
    }
}

tline *leer_linea() {
    char buffer[1024];

    fgets(buffer, 1024, stdin);
    if (feof(stdin)) salir();                   // Si se manda CTRL + D, se sale de la minishell
    if (buffer[0] == '\n') return NULL;         // Si no se ha introducido nada, volver a pedir entrada
    else if (buffer[0] == ' ') return NULL;     // Si se ha introducido un espacio, volver a pedir entrada
    else if (buffer[0] == '\t') return NULL;    // Si se ha introducido un tab, volver a pedir entrada
    return tokenize(buffer);                    // Devolver la línea tokenizada
}

// Comprobar si hay algun proceso hijo terminado, para ir limpiando el array de background según terminan, un "cosechador" por así decirlo
void comprobar_procesos_terminados() {
    int i, j;
    for (i = 0; i < num_procesos; i++) {
        if (waitpid(pids[i], NULL, WNOHANG) != 0) { // WNOHANG testea si el hijo pid[i] ha terminado, si hubiera terminado, el status no me interesa
            // Mantener el orden de los procesos por cómo se ha implementado fg
            for (j = i; j < num_procesos; j++) {
                pids[j] = pids[j + 1];
                nombre_procesos[j] = nombre_procesos[j + 1];
            }
            num_procesos--;
            pids = (int *)realloc(pids, (num_procesos + 1) * sizeof(int)); // +1 es por cómo he implementado el añadir a esta "lista"
            nombre_procesos = (char **)realloc(nombre_procesos, (num_procesos + 1) * sizeof(char *));
        }
    }
}

// Manejador CTRL + C
void manejador_sigint() {
    // Salir solo del proceso en foreground, no de la minishell
    kill(pidfg, SIGKILL);
    printf("\n");
}

void ejecutar_interno(tline *linea) {
    // Comprobar si el mandato es interno
    if (strcmp(linea->commands[0].argv[0], "cd") == 0) cd(linea->commands[0].argv[1]);
    else if (strcmp(linea->commands[0].argv[0], "jobs") == 0) jobs();
    else if (strcmp(linea->commands[0].argv[0], "fg") == 0) fg(linea->commands[0].argv[1]);
    else if (strcmp(linea->commands[0].argv[0], "umask") == 0) chumask(linea->commands[0].argv[1]);
    else if (strcmp(linea->commands[0].argv[0], "exit") == 0) salir();
    else if (strcmp(linea->commands[0].argv[0], "clear") == 0) system("clear");
    else if(strcmp(linea->commands[0].argv[0], "help") == 0) help();
    else ejecutar_externo(linea); // Ejecutar mandato externo
}

// Implementación cd
void cd(char *dir) {
    if (dir == NULL) {
        if (chdir(getenv("HOME")) != 0) {
            perror("cd");
            return;
        }
    }
    else if (chdir(dir) != 0) {
        fprintf(stderr, "cd: Error: no se ha podido cambiar al directorio %s\n", dir);
        return;
    }
}

// Implementación jobs
void jobs() {
    int i;
    for (i = 0; i < num_procesos; i++) {
        // Comprobar si el proceso está activo
        // WNOHANG testea si el hijo pid[i] ha terminado, si no ha terminado, devuelve 0, no nos interesa el status así que ni lo compruebo
        if (waitpid(pids[i], NULL, WNOHANG) == 0) printf("[%d] Running        %s&\n", i + 1, nombre_procesos[i]);
    }
}

// Implementación fg
void fg(char *identificador) {
    pid_t pidv;
    if (identificador != NULL) pidv = pids[atoi(identificador) - 1]; // Por cómo está implementada la lista, siempre será uno menos
    else pidv = pids[num_procesos - 1];

    if (waitpid(pidv, NULL, WNOHANG) == 0) { // Para evitar error si la lista está vacía o el proceso que le mandamos no está activo
        pidfg = pidv;
        signal(SIGINT, manejador_sigint); // En caso de que se haga ctrl + c, se matará a este pid en concreto
        printf("%s\n", nombre_procesos[num_procesos - 1]);
        waitpid(pidv, NULL, 0);
        return;
    }

    fprintf(stderr, "fg: No hay ningún proceso en segundo plano o no se ha podido ejecutar el pid proporcionado en primer plano.\n");
}

// Implementación umask
void chumask(char *mask) {
    int mascara;
    // Comprobar si se ha pasado un argumento y si es válido
    if (mask != NULL) {
        // Comprobar si es un número
        if (sscanf(mask, "%o", &mascara) != 1) {  // %o para que se lea como octal
            fprintf(stderr, "umask: Error: el argumento proporcionado no es un octal.\n");
            return;
        }
        // Comprobar si es un número válido
        if (mascara < 0000 || mascara > 0777) {
            fprintf(stderr, "umask: Error: el argumento proporcionado no es una máscara válida.\n");
            return;
        }
        // Cambiar la máscara
        umask_val = mascara;
        return;
    }
    // Si no se ha pasado un argumento, se muestra la máscara actual
    printf("%04o\n", umask_val);
}

// Implementación exit
void salir() {
    int i;
    // Liberar memoria y matar los procesos en background
    for (i = 0; i < num_procesos; i++) {
        kill(pids[i], SIGKILL);
        free(nombre_procesos[i]);
    }
    free(pids);
    free(nombre_procesos);
    system("clear");
    printf("\033[0;31m------- Asesinando la minishell ---------\x1b[0m\n");
    printf("\033[0;32m----------- Hasta la próxima ------------\x1b[0m\n");
    // Salir
    exit(EXIT_SUCCESS);
}

// Implementación help
void help() {
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
    printf("El resto de comandos se ejecutan como en cualquier shell UNIX.\n");
}

// Ejecutar mandatos externos
void ejecutar_externo(tline *linea) {
    int j;
    int k;
    pid_t pid;
    int fd[2];
    int redirent, redirsal, redirerr;
    int status;
    char *lineaenviada;
    redirent = ficheroredireccion(linea, 1);
    if (redirent == 1)  return;  // Si el fichero de redirección proporcionado es inválido, podemos salir directamente
    redirsal = ficheroredireccion(linea, 2);
    if (redirsal == 1) return;
    redirerr = ficheroredireccion(linea, 3);
    if (redirerr == 1) return;
    lineaenviada = (char *)calloc(1024, sizeof(char)); // Uso calloc porque sino la línea en jobs aparecía con caracteres raros que venían de que malloc da la memoria sin ponerla a 0, calloc nos da el bloque bien limpio
    // Comprobar si necesitamos pipes
    if (linea->ncommands > 1) {
        if (pipe(fd) != 0) {
            fprintf(stderr, "%s. Error al crear el pipe, %s", linea->commands[0].argv[0], strerror(errno));
            free(lineaenviada);
            return;
        }
    }

    // Ejecutar en foreground o background lo haremos desde el padre
    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "%s: Error al crear el proceso hijo, %s\n", linea->commands[0].argv[0], strerror(errno));
        return;
    }
    if (pid == 0) {
        umask(umask_val);  // Cambiar la máscara de permisos
        if (linea->redirect_input != NULL) dup2(redirent, STDIN_FILENO);
        if (linea->ncommands > 1) {
            if (dup2(fd[1], STDOUT_FILENO) < 0) {
                fprintf(stderr, "%s: Error al escribir en el pipe, %s\n", linea->commands[0].argv[0], strerror(errno));
                free(lineaenviada);
                exit(EXIT_FAILURE);
            }
// Cerrar los extremos del pipe, dup2 duplica el descriptor de fichero, se puede cerrar el extremo de escritura, en este punto tenemos dos descriptores de fichero "iguales"
            close(fd[0]);
            close(fd[1]);
        }
        if (linea->redirect_output != NULL && linea->ncommands == 1) dup2(redirsal, STDOUT_FILENO);
        else if (linea->redirect_error != NULL && linea->ncommands == 1) dup2(redirerr, STDERR_FILENO);
        execv(linea->commands[0].filename, linea->commands[0].argv);
        if (strcmp(strerror(errno), "Bad address \0")) fprintf(stderr, "%s: No se encuentra el mandatao.\n", linea->commands[0].argv[0]); // En caso en que el mandato no exista
        free(lineaenviada);
        exit(EXIT_FAILURE);
    } else {  // PEsto se produce antes del execv
        if (linea->background) {
            // Añadir el proceso a la lista de procesos, ejecución en background
            pids = realloc(pids, (num_procesos + 1) * sizeof(pid_t));  // Añadir espacio para el nuevo proceso
            pids[num_procesos] = pid;  // Guardar el pid del *primer* proceso
            nombre_procesos = realloc(nombre_procesos, (num_procesos + 1) * sizeof(char *));  // Añadir espacio para el nuevo proceso
            for (k = 0; k < linea->ncommands; k++) {
                for (j = 0; j < linea->commands[k].argc; j++) {  // Guardar el nombre completo del proceso, para simular la salida de jobs
                    strcat(lineaenviada, linea->commands[k].argv[j]);
                    strcat(lineaenviada, " ");
                }
                if (k != linea->ncommands - 1) strcat(lineaenviada, "| ");
            }
            nombre_procesos[num_procesos] = (char*)malloc(1024 * sizeof(char));  // Añadir espacio para el nombre del proceso, para poder actualizarlos luego, se utiliza el valor 1024
            strcpy(nombre_procesos[num_procesos], lineaenviada);
            num_procesos++;
            printf("[%d] %d\n", num_procesos, pid);  // Lo mismo que hace una shell de UNIX cuando se manda un mandato a background
        } else {
            // Ejecución en foreground
            pidfg = pid; // Meter el pid en el activo para que lo mate el manejador si se le llama
            signal(SIGINT, manejador_sigint); // Cambiar el manejador para poder matarlo con ctrl + c
            if (linea->ncommands > 1 ) waitpid(pid, &status, WNOHANG);  // Si hay pipes, no esperamos a que se ejecute aquí, tendríamos problemas con mandatos del tipo "find / | grep santi", el grep no se ejecutaría hasta que se hiciera CTRL + C o terminase find
            else waitpid(pid, &status, 0);  // Si es un solo mandato, esperamos a que se ejecute aquí
            // Comprobar si el proceso hijo ha terminado correctamente
            if (WIFEXITED(status) != 0)
                if (WEXITSTATUS(status) != 0) fprintf(stderr, "%s: Error al ejecutar el mandato.\n", linea->commands[0].argv[0]);
        }
    }

    // Proceso principal, en caso que haya pipes, mandamos la entrada de lectura del pipe, se produce después del execv
    if (linea->ncommands > 1) {
        // Todos los procesos tienen acceso independiente a los descriptores de fichero, luego también es necesario cerrar
        // el extremo de escritura aquí para que el siguiente mandato sepa dónde termina la entrada estándar
        close(fd[1]); // Cerramos este bicho aquí para que el hijo que creamos sepa dónde termina la entrada estándar
        ejecutar_pipe(linea, linea->ncommands - 1, fd[0]); // Solo nos hace falta enviar el extremo de lectura del pipe
        close(fd[0]);
    }
    // Liberar la memoria reservada
    free(lineaenviada);
}

void ejecutar_pipe(tline *linea, int restantes, int entrada) { // Una forma recursiva, relativamente elegante, de gestionar las líneas que tengan pipes
    // int entrada representa lo que es nuestra entrada estándar para X mandato, luego es el equivalente a tener "fd[0]", tendremos que cerrarlo igualmente
    int fd[2];
    pid_t pid;
    int redirsal, redirerr;
    if (restantes != 1) 
        if (pipe(fd) != 0) {
            fprintf(stderr, "%s: Error al crear el pipe: %s\n", linea->commands[linea->ncommands - restantes].argv[0], strerror(errno));
            return;
        }
    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "%s: Error al crear el proceso hijo: %s\n", linea->commands[linea->ncommands - restantes].argv[0], strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        umask(umask_val); // Aplicar la máscara de permisos de la minishell
        if (dup2(entrada, STDIN_FILENO) < 0) {  // Redirigir la entrada estándar
            fprintf(stderr, "%s: Error al leer del pipe, %s\n", linea->commands[linea->ncommands - restantes].filename, strerror(errno)); // Duplicar el descriptor de fichero de entrada estándar
            exit(EXIT_FAILURE);
        }
        close(entrada);
        if (restantes == 1) { // Si tenemos alguna redirección, hacemos dup2 aquí que es el último mandato de los enviados
            redirsal = ficheroredireccion(linea, 2); // Ya comprobamos en ejecutar_interno si era un fichero válido
            redirerr = ficheroredireccion(linea, 3);
            if (linea->redirect_output != NULL) dup2(redirsal, STDOUT_FILENO);
            else if (linea->redirect_error != NULL) dup2(redirerr, STDERR_FILENO);
        } else { // Si no es el último mandato, seguiremos la recursión volviendo a redirigir la salida
            if (dup2(fd[1], STDOUT_FILENO) < 0) {  // Redirigir la salida estándar de un posible mandato intermedio
                fprintf(stderr, "%s: Error al escribir en el pipe, %s\n", linea->commands[linea->ncommands - restantes].filename, strerror(errno)); // Duplicar el descriptor de fichero de salida estándar
                exit(EXIT_FAILURE);
            }
            close(fd[0]);
            close(fd[1]);
        }
        execv(linea->commands[linea->ncommands - restantes].filename, linea->commands[linea->ncommands - restantes].argv);
        exit(EXIT_FAILURE);
    }
    // Si hubiera varios pipes, cargaríamos los mandatos de forma recursiva, enviando la salida de lectura de la tubería donde cargamos antes la salida estándar
    if (restantes != 1) {
        close(fd[1]);  // Cerramos la entrada de escritura aquí para que el siguiente hijo sepa dónde termina la entrada estándar, de forma análoga a la primera ejecución
        ejecutar_pipe(linea, restantes - 1, fd[0]);  // Llamada recursiva, el último mandato no tomará esta rama
        close(fd[0]);
    }

    if (!linea->background) { // Ejecución en foreground
        /* Esta espera se pone al final porque si tuviéramos una línea del tipo "find / | grep h | head -5", se quedaría esperando a la finalización de grep, se quedaría colgado como "find / | grep h" si esperasemos en vez de usar WNOHANG en ejecutar_externo el pidfg
        esto se debe a que intentaríamos esperar por el mandato anterior, cuando es el último el que se debe estar ejecutando, sin que el anterior haya terminado necesariamente. El loop principal se encargará de escanear por procesos
        zombies, luego en líneas como "find / | grep h | head -5", en las que find queda zombie porque se termina antes de sacar head que de buscar, lo limpiará el loop principal.*/
        // Esperar por los PIDS empezando por el último y hasta el segundo, se espera por ellos, el primer mandato si queda zombie, lo limpia loop principal.
        waitpid(pid, NULL, 0);
        // No se queda esperando en líneas que tienen mandatos como "grep" entre medias, porque tienen las entradas y salidas redirigidas, y el descriptor de fichero de entrada cerrado cuando llega aquí, así que muere directamente al esperar por él
    }
}

int ficheroredireccion(tline *linea, int tipo) { // Separado del proceso de ejecución para poder redirigir la salida más fácilmente en el caso de las pipes
    int redir = 0;
    
    if (linea->redirect_input != NULL && tipo == 1) redir = open(linea->redirect_input, O_RDONLY);
    if (linea->redirect_output != NULL && tipo == 2) redir = creat(linea->redirect_output, 0666 - umask_val); // Usamos el valor de la máscara para creación de archivos de la minishell
    if (linea->redirect_error != NULL && tipo == 3) redir = creat(linea->redirect_error, 0666 - umask_val);
    if (redir == -1 && (linea->redirect_input != NULL || linea->redirect_output != NULL || linea->redirect_error != NULL)) {
        fprintf(stderr, "%s: Error. No se pudo abrir o crear el archivo proporcionado, %s\n", linea->commands[0].argv[0], strerror(errno));
        return 1;
    }
    return redir;
}
