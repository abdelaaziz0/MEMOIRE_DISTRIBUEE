#include "common_impl.h"

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <ip_lanceur> <port_lanceur> <programme> <args...>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "dsmwrap: Démarrage sur %s\n", argv[1]);
    fprintf(stderr, "  IP lanceur: %s\n", argv[1]);
    fprintf(stderr, "  Port lanceur: %s\n", argv[2]);
    fprintf(stderr, "  Programme à exécuter: %s\n", argv[3]);
    fprintf(stderr, "  Nombre total d'arguments: %d\n", argc);

    /* Connexion au lanceur */
    const char *ip_lanceur = argv[1];
    int port_lanceur = atoi(argv[2]);
    
    int socket_conn = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_conn == -1) ERROR_EXIT("socket");

    struct sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port_lanceur);
    
    if (inet_pton(AF_INET, ip_lanceur, &sock_addr.sin_addr) <= 0) {
        ERROR_EXIT("inet_pton");
    }

    fprintf(stderr, "dsmwrap: Tentative de connexion à %s:%d\n", ip_lanceur, port_lanceur);
    if (connect(socket_conn, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) == -1) {
        ERROR_EXIT("connect");
    }
    fprintf(stderr, "dsmwrap: Connecté au lanceur\n");

    /* Envoi des informations au lanceur */
    char hostname[MAX_STR];
    if (gethostname(hostname, MAX_STR) == -1) {
        ERROR_EXIT("gethostname");
    }
    fprintf(stderr, "dsmwrap: Envoi du hostname: %s\n", hostname);
    if (write(socket_conn, hostname, MAX_STR) == -1) {
        ERROR_EXIT("write hostname");
    }

    pid_t pid = getpid();
    fprintf(stderr, "dsmwrap: Envoi du PID: %d\n", pid);
    if (write(socket_conn, &pid, sizeof(pid_t)) == -1) {
        ERROR_EXIT("write pid");
    }

    /* Socket pour DSM */
    int sock_dsm = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_dsm == -1) ERROR_EXIT("socket dsm");

    struct sockaddr_in dsm_addr;
    memset(&dsm_addr, 0, sizeof(dsm_addr));
    dsm_addr.sin_family = AF_INET;
    dsm_addr.sin_port = 0;
    dsm_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_dsm, (struct sockaddr*)&dsm_addr, sizeof(dsm_addr)) == -1) {
        ERROR_EXIT("bind");
    }

    if (listen(sock_dsm, MAX_CONNECT) == -1) {
        ERROR_EXIT("listen");
    }

    socklen_t len = sizeof(dsm_addr);
    if (getsockname(sock_dsm, (struct sockaddr*)&dsm_addr, &len) == -1) {
        ERROR_EXIT("getsockname");
    }

    int port_dsm = ntohs(dsm_addr.sin_port);
    fprintf(stderr, "dsmwrap: Envoi du port DSM: %d\n", port_dsm);
    if (write(socket_conn, &port_dsm, sizeof(int)) == -1) {
        ERROR_EXIT("write port");
    }

    /* Préparation des arguments*/
    char **newargv = malloc((argc - 3) * sizeof(char*));
    if (!newargv) ERROR_EXIT("malloc");

    /* Copie des arguments*/
    for (int i = 3; i < argc; i++) {
        newargv[i-3] = argv[i];
    }
    newargv[argc-3] = NULL;

    fprintf(stderr, "dsmwrap: Tentative d'exécution de %s\n", argv[3]);
    execvp(argv[3], newargv);


    fprintf(stderr, "Erreur: impossible d'exécuter %s: %s\n", argv[3], strerror(errno));
    free(newargv);
    close(socket_conn);
    close(sock_dsm);
    
    return EXIT_FAILURE;
}