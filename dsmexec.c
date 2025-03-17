#include "common_impl.h"

/* variables globales */
dsm_proc_t *proc_array = NULL;
volatile int num_procs_creat = 0;

void usage(void) {
    fprintf(stdout, "Usage : dsmexec machine_file executable arg1 arg2 ...\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) {
    /* on traite les fils qui se terminent */
    /* pour eviter les zombies */
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage();
    } else {      
        pid_t pid;
        int num_procs = 0;
        int i = 0;
        char ligne[MAX_STR];
        int sock_fd;
        struct sockaddr_in sock_addr;
        socklen_t len = sizeof(struct sockaddr_in);
        int *stdout_pipes = NULL;
        int *stderr_pipes = NULL;

        /* Mise en place du handler */
        struct sigaction sa;
        sa.sa_handler = sigchld_handler;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCHLD, &sa, NULL);

        /* Première lecture pour compter les processus */
        FILE *machines = fopen(argv[1], "r");
        if (!machines) {
            ERROR_EXIT("fopen");
        }

        while (fgets(ligne, MAX_STR, machines) != NULL) {
            if (strlen(ligne) > 1) {
                num_procs++;
            }
        }

        /* Allocation après avoir compté */
        proc_array = malloc(num_procs * sizeof(dsm_proc_t));
        if (!proc_array) {
            fclose(machines);
            ERROR_EXIT("malloc proc_array");
        }

        /* Retour au début du fichier */
        rewind(machines);

        /* Deuxième lecture pour remplir le tableau */
        i = 0;
        while (fgets(ligne, MAX_STR, machines) != NULL && i < num_procs) {
            if (strlen(ligne) > 1) {
                ligne[strlen(ligne)-1] = '\0';
                strncpy(proc_array[i].connect_info.machine, ligne, MAX_STR);
                i++;
            }
        }
        fclose(machines);

        /* Creation socket */
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            ERROR_EXIT("socket");
        }
      
        /* Obtenir l'adresse IP locale */
        char ip_addr[INET_ADDRSTRLEN];
        char hostname[256];

        if (gethostname(hostname, sizeof(hostname)) == -1) {
            ERROR_EXIT("gethostname");
        }

        struct hostent *host_info = gethostbyname(hostname);
        if (host_info == NULL) {
            ERROR_EXIT("gethostbyname");
        }

        struct in_addr **addr_list = (struct in_addr **)host_info->h_addr_list;
        if (inet_ntop(AF_INET, addr_list[0], ip_addr, INET_ADDRSTRLEN) == NULL) {
            ERROR_EXIT("inet_ntop");
        }

        memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port = 0;
        sock_addr.sin_addr.s_addr = INADDR_ANY;        

        if (bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1) {
            ERROR_EXIT("bind");
        }
      
        if (listen(sock_fd, num_procs) == -1) {
            ERROR_EXIT("listen");
        }

        /* Allocation des pipes */
        stdout_pipes = malloc(2 * num_procs * sizeof(int));
        stderr_pipes = malloc(2 * num_procs * sizeof(int));
        if (!stdout_pipes || !stderr_pipes) {
            ERROR_EXIT("malloc pipes");
        }

        /* Creation des processus */
        for(i = 0; i < num_procs ; i++) {
            if (pipe(stdout_pipes + 2*i) == -1) {
                ERROR_EXIT("pipe");
            }
            if (pipe(stderr_pipes + 2*i) == -1) {
                ERROR_EXIT("pipe");
            }
    
            pid = fork();
            if(pid == -1) ERROR_EXIT("fork");
    
            if (pid == 0) { /* fils */    
                close(stdout_pipes[2*i]);
                if (dup2(stdout_pipes[2*i + 1], STDOUT_FILENO) == -1)
                    ERROR_EXIT("dup2");
                close(stdout_pipes[2*i + 1]);
      
                close(stderr_pipes[2*i]);
                if (dup2(stderr_pipes[2*i + 1], STDERR_FILENO) == -1)
                    ERROR_EXIT("dup2");
                close(stderr_pipes[2*i + 1]);

                char **newargv = malloc((argc + 5) * sizeof(char *));
                if (!newargv) ERROR_EXIT("malloc");
            
                char port_str[20];
                if (getsockname(sock_fd, (struct sockaddr*)&sock_addr, &len) == -1) {
                    ERROR_EXIT("getsockname");
                }
                sprintf(port_str, "%d", ntohs(sock_addr.sin_port));


                newargv[0] = "ssh";
                newargv[1] = proc_array[i].connect_info.machine;
                newargv[2] = "dsmwrap";
                newargv[3] = ip_addr;
                newargv[4] = port_str;
                
                for (int j = 2; j < argc; j++) {
                    newargv[j + 3] = argv[j];
                }
                newargv[argc+3] = NULL;

                fprintf(stderr, "Lancement SSH vers %s:\n", proc_array[i].connect_info.machine);
                fprintf(stderr, "  IP lanceur: %s\n", ip_addr);
                fprintf(stderr, "  Port lanceur: %s\n", port_str);
                fprintf(stderr, "Commande complète:");
                for (int j = 0; newargv[j] != NULL; j++) {
                    fprintf(stderr, " %s", newargv[j]);
                }
                fprintf(stderr, "\n");

                execvp("ssh", newargv);
                fprintf(stderr, "Erreur execvp: %s\n", strerror(errno));
                free(newargv);
                ERROR_EXIT("execvp");

            } else if(pid > 0) {        
                fprintf(stderr, "Created process %d for machine %s\n",
                        pid, proc_array[i].connect_info.machine);
                close(stdout_pipes[2*i + 1]);
                close(stderr_pipes[2*i + 1]);
                proc_array[i].pid = pid;
                num_procs_creat++;      
            }
        }

        /*/
        /********** ATTENTION : LE PROTOCOLE D'ECHANGE *************/
        /********** DECRIT CI-DESSOUS NE DOIT PAS ETRE *************/
        /********** MODIFIE, NI DEPLACE DANS LE CODE   *************/
        /*/
        
        /* 1- envoi du nombre de processus aux processus dsm*/
        /* On envoie cette information sous la forme d'un ENTIER */
        /* (IE PAS UNE CHAINE DE CARACTERES */
        
        /* 2- envoi des rangs aux processus dsm */
        /* chaque processus distant ne reçoit QUE SON numéro de rang */
        /* On envoie cette information sous la forme d'un ENTIER */
        /* (IE PAS UNE CHAINE DE CARACTERES */
        
        /* 3- envoi des infos de connexion aux processus */
        /* Chaque processus distant doit recevoir un nombre de */
        /* structures de type dsm_proc_conn_t égal au nombre TOTAL de */
        /* processus distants, ce qui signifie qu'un processus */
        /* distant recevra ses propres infos de connexion */
        /* (qu'il n'utilisera pas, nous sommes bien d'accords). */

        /*/
        /********** FIN DU PROTOCOLE D'ECHANGE DES DONNEES *********/
        /********** ENTRE DSMEXEC ET LES PROCESSUS DISTANTS ********/

        for(i = 0; i < num_procs; i++) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            
            // Accepter la connexion du processus distant
            int client_sock = accept(sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_sock < 0) {
                ERROR_EXIT("accept");
            }
            
            // 1. Envoyer le nombre total de processus
            if (write(client_sock, &num_procs, sizeof(int)) < 0) {
                ERROR_EXIT("write num_procs");
            }
            
            // 2. Envoyer le rang du processus
            if (write(client_sock, &i, sizeof(int)) < 0) {
                ERROR_EXIT("write rank");
            }
            
            proc_array[i].connect_info.fd = client_sock;
            proc_array[i].connect_info.rank = i;

            // Recevoir les informations de connexion
            char hostname[MAX_STR];
            int pid, port_dsm;

            if (read(client_sock, hostname, MAX_STR) < 0 ||
                read(client_sock, &pid, sizeof(int)) < 0 ||
                read(client_sock, &port_dsm, sizeof(int)) < 0) {
                ERROR_EXIT("read connection info");
            }
            
            proc_array[i].connect_info.port_num = port_dsm;
        }

        // 3. Envoyer les infos de connexion à tous les processus
        for(i = 0; i < num_procs; i++) {
            for(int j = 0; j < num_procs; j++) {
                if (write(proc_array[i].connect_info.fd, &proc_array[j].connect_info,
                        sizeof(struct dsm_proc_conn)) < 0) {
                    ERROR_EXIT("write connection info");
                }
            }
        }
        /* gestion des E/S : on recupere les caracteres */
        /* sur les tubes de redirection de stdout/stderr */    
        /* while(1)
            {
               je recupere les infos sur les tubes de redirection
               jusqu'à ce qu'ils soient inactifs (ie fermes par les
               processus dsm ecrivains de l'autre cote ...)
          
            };
         */

        struct pollfd *fds = malloc(2 * num_procs * sizeof(struct pollfd));
        if (!fds) {
            ERROR_EXIT("malloc pollfd");
        }

        /* Initialisation des structures poll */
        for(i = 0; i < num_procs; i++) {
            /* stdout pipes */
            fds[2*i].fd = stdout_pipes[2*i];
            fds[2*i].events = POLLIN;
    
            /* stderr pipes */
            fds[2*i + 1].fd = stderr_pipes[2*i];
            fds[2*i + 1].events = POLLIN;
        }

        /*lecture des sorties */
        char buffer[MAX_STR];
        int nb_pipes_ouverts = 2 * num_procs;

        while(nb_pipes_ouverts > 0) {
            int ret = poll(fds, 2 * num_procs, -1);
    
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("poll");
                break;
            }

            /* Vérification de chaque pipe */
            for(i = 0; i < 2 * num_procs; i++) {
                if (fds[i].revents & POLLIN) {
                    int nb_read = read(fds[i].fd, buffer, MAX_STR-1);
                    if (nb_read > 0) {
                        buffer[nb_read] = '\0';
                        if (i % 2 == 0) {
                            printf("[Processus %d - stdout] : %s", i/2, buffer);
                            fflush(stdout);
                        } else {
                            fprintf(stderr, "[Processus %d - stderr] : %s", i/2, buffer);
                            fflush(stderr);
                        }
                    } else if (nb_read == 0) {
                        /* Pipe fermé */
                        fds[i].fd = -1;
                        nb_pipes_ouverts--;
                        close(fds[i].fd);
                    }
                }
        
                if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    if (fds[i].fd != -1) {
                        close(fds[i].fd);
                        fds[i].fd = -1;
                        nb_pipes_ouverts--;
                    }
                }
            }
        }

        /* Attente des fils */
        for(i = 0; i < num_procs; i++) {
            wait(NULL);
        }

        /* Nettoyage */
        free(proc_array);
        free(stdout_pipes);
        free(stderr_pipes);
        free(fds);
        close(sock_fd);
    }  
    exit(EXIT_SUCCESS);  
}