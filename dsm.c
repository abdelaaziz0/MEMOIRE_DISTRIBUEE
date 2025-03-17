#include "dsm_impl.h"

int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 

static dsm_proc_conn_t *procs = NULL;
static dsm_page_info_t table_page[PAGE_NUMBER];
static pthread_t comm_daemon;
static void *segv_addr;
static sem_t sem_sync;   


/* indique l'adresse de debut de la page de numero numpage */
static char *num2address( int numpage )
{ 
   char *pointer = (char *)(BASE_ADDR+(numpage*(PAGE_SIZE)));
   
   if( pointer >= (char *)TOP_ADDR ){
      fprintf(stderr,"[%i] Invalid address !\n", DSM_NODE_ID);
      return NULL;
   }
   else return pointer;
}

/* cette fonction permet de recuperer un numero de page */
/* a partir  d'une adresse  quelconque */
static int address2num( char *addr )
{
  return (((intptr_t)(addr - BASE_ADDR))/(PAGE_SIZE));
}

/* cette fonction permet de recuperer l'adresse d'une page */
/* a partir d'une adresse quelconque (dans la page)        */
static char *address2pgaddr( char *addr )
{
  return  (char *)(((intptr_t) addr) & ~(PAGE_SIZE-1)); 
}

/* fonctions pouvant etre utiles */
static void dsm_change_info( int numpage, dsm_page_state_t state, dsm_page_owner_t owner)
{
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)) {	
	if (state != NO_CHANGE )
	table_page[numpage].status = state;
      if (owner >= 0 )
	table_page[numpage].owner = owner;
      return;
   }
   else {
	fprintf(stderr,"[%i] Invalid page number !\n", DSM_NODE_ID);
      return;
   }
}

static dsm_page_owner_t get_owner( int numpage)
{
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status( int numpage)
{
   return table_page[numpage].status;
}

/* Allocation d'une nouvelle page */
static void dsm_alloc_page( int numpage )
{
   char *page_addr = num2address( numpage );
   mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   return ;
}

/* Changement de la protection d'une page */
static void dsm_protect_page( int numpage , int prot)
{
   char *page_addr = num2address( numpage );
   mprotect(page_addr, PAGE_SIZE, prot);
   return;
}

static void dsm_free_page( int numpage )
{
   char *page_addr = num2address( numpage );
   munmap(page_addr, PAGE_SIZE);
   return;
}


static int dsm_send(int dest, void *buf, size_t size) {
   ssize_t envoye = 0;
   ssize_t restant = size;
   char *ptr = buf;
   while (restant > 0) {
      envoye = write(procs[dest].fd, ptr, restant);
      if  (envoye <= 0) {
         perror("dsm_send: write failed");
         return -1;
      }
      ptr += envoye;
      restant -= envoye;
   }
   return size - restant;
}

static int dsm_recv(int from, void *buf, size_t size) {
   ssize_t recu = 0;
   ssize_t restant = size;
   char *ptr = buf;
   while (restant > 0) {
      recu = read(procs[from].fd, ptr, restant);
      if (recu < 0) {
         perror("dsm_recv: read failed");
         return -1;
      }
      if (recu == 0) {
         return 0;
      }
      ptr += recu;
      restant -= recu;
   }
   return size - restant;
}

static void *dsm_comm_daemon(void *arg)
{
    dsm_req_t request;
    char page_buffer[PAGE_SIZE];

    while(1) {
        for(int i = 0; i < DSM_NODE_NUM; i++) {
            if (i == DSM_NODE_ID) continue;
            struct pollfd fds;
            fds.fd = procs[i].fd;
            fds.events = POLLIN;
            
            if (poll(&fds, 1, 0) > 0) {
                if (dsm_recv(i, &request, sizeof(dsm_req_t)) > 0) {
                    fprintf(stderr, "[%d] Requête reçue du processus %d pour la page %d\n",
                            DSM_NODE_ID, i, request.page_num);

                    if (request.page_num == -1) {
                        fprintf(stderr, "[%d] Message de finalisation reçu\n", DSM_NODE_ID);
                        continue;
                    }

                    if (get_owner(request.page_num) == DSM_NODE_ID) {
                        char *page_addr = num2address(request.page_num);
                        fprintf(stderr, "[%d] Envoi page %d au processus %d\n",
                                DSM_NODE_ID, request.page_num, i);

                        // Copier la page avant de perdre les droits
                        memcpy(page_buffer, page_addr, PAGE_SIZE);
                        
                        // Envoyer la page
                        if (dsm_send(request.source, page_buffer, PAGE_SIZE) < 0) {
                            fprintf(stderr, "[%d] Erreur envoi page %d\n", 
                                DSM_NODE_ID, request.page_num);
                            continue;
                        }
                        
                        // Attendre la confirmation de réception
                        sem_wait(&sem_sync);
                        
                        // Protéger la page et mettre à jour les infos
                        dsm_protect_page(request.page_num, PROT_NONE);
                        dsm_change_info(request.page_num, NO_ACCESS, request.source);
                        
                        fprintf(stderr, "[%d] Page %d transférée au processus %d\n", 
                                DSM_NODE_ID, request.page_num, request.source);
                    }
                }
            }
        }
        usleep(1000);
    }
    return NULL;
}
static void dsm_handler(void)
{
    int num_page_erreur = address2num(segv_addr);
    dsm_page_owner_t owner_page_erreur = get_owner(num_page_erreur);
    
    fprintf(stderr, "[%d] Traitement SIGSEGV - Page %d, propriétaire=%d\n", 
            DSM_NODE_ID, num_page_erreur, owner_page_erreur);
    // Envoyer la requête
    dsm_req_t request;
    request.source = DSM_NODE_ID;
    request.page_num = num_page_erreur;
    
    fprintf(stderr, "[%d] Envoi requête au processus %d pour la page %d\n",
            DSM_NODE_ID, owner_page_erreur, num_page_erreur);
            
    if (dsm_send(owner_page_erreur, &request, sizeof(dsm_req_t)) < 0) {
        fprintf(stderr, "[%d] Erreur envoi requête\n", DSM_NODE_ID);
        exit(EXIT_FAILURE);
    }

    // Réception de la page
    char page_buffer[PAGE_SIZE];
    fprintf(stderr, "[%d] Attente réception de la page %d\n", 
            DSM_NODE_ID, num_page_erreur);
    if (dsm_recv(owner_page_erreur, page_buffer, PAGE_SIZE) < 0) {
        fprintf(stderr, "[%d] Erreur réception page\n", DSM_NODE_ID);
        exit(EXIT_FAILURE);
    }

    // Protéger et copier la page
    dsm_protect_page(num_page_erreur, PROT_READ | PROT_WRITE);
    char *page_addr = num2address(num_page_erreur);
    memcpy(page_addr, page_buffer, PAGE_SIZE);
    // Mettre à jour les infos
    dsm_change_info(num_page_erreur, WRITE, DSM_NODE_ID);
    fprintf(stderr, "[%d] Page %d reçue et installée\n", 
            DSM_NODE_ID, num_page_erreur);
    // Signaler que la page est bien reçue
    sem_post(&sem_sync);
}

/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context)
{
   fprintf(stderr, "[%d] Entrée dans segv_handler, signal=%d\n", DSM_NODE_ID, sig);
   /* A completer */
   /* adresse qui a provoque une erreur */
   void  *addr = info->si_addr;   
   segv_addr = info->si_addr;  
   fprintf(stderr, "[%d] Adresse fautive: %p\n", DSM_NODE_ID, addr);
  /* Si ceci ne fonctionne pas, utiliser a la place :*/
  /*
   #ifdef __x86_64__
   void *addr = (void *)(context->uc_mcontext.gregs[REG_CR2]);
   #elif __i386__
   void *addr = (void *)(context->uc_mcontext.cr2);
   #else
   void  addr = info->si_addr;
   #endif
   */
   /*
   pour plus tard (question ++):
   dsm_access_t access  = (((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & 2) ? WRITE_ACCESS : READ_ACCESS;   
  */   
   /* adresse de la page dont fait partie l'adresse qui a provoque la faute */
   void  *page_addr = (void *)(((unsigned long) addr) & ~(PAGE_SIZE-1));
   fprintf(stderr, "[%d] Adresse de la page: %p\n", DSM_NODE_ID, page_addr);

   if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR))
   {
       fprintf(stderr, "[%d] Adresse dans la plage DSM, appel de dsm_handler\n", DSM_NODE_ID);
       dsm_handler();
   }
   else
   {
       fprintf(stderr, "[%d] SIGSEGV hors plage DSM\n", DSM_NODE_ID);
       /* SIGSEGV normal : ne rien faire*/
       signal(SIGSEGV, SIG_DFL);
       raise(SIGSEGV);
   }
}

/* Seules ces deux dernieres fonctions sont visibles et utilisables */
/* dans les programmes utilisateurs de la DSM                       */
char *dsm_init(int argc, char* argv[])
{  
   //(lors du debug on a remarqué une SIGRTMIN qui persiste alors on a décidé de l'ignorer)
   struct sigaction act_rt;
   memset(&act_rt, 0, sizeof(act_rt));
   act_rt.sa_handler = SIG_IGN;
   sigaction(SIGRTMIN, &act_rt, NULL);

   fprintf(stderr, "[Proc] Démarrage de dsm_init\n"); 
   struct sigaction act;
   memset(&act, 0, sizeof(act));
   int index;   

   /* Récupération de la valeur des variables d'environnement */
   /* DSMEXEC_FD et MASTER_FD                                 */
   char* dsmexec_fd_s = getenv("DSMEXEC_FD");
   char* master_fd_s = getenv("MASTER_FD");

   if(dsmexec_fd_s == NULL || master_fd_s == NULL){
      fprintf(stderr, "Variables d'environnement manquantes\n");
      exit(EXIT_FAILURE);
   }
   int dsmexec_fd = atoi(dsmexec_fd_s);
   int master_fd = atoi(master_fd_s);

   fprintf(stderr, "[Proc] DSMEXEC_FD=%s, MASTER_FD=%s\n", dsmexec_fd_s, master_fd_s);
   
   /* reception du nombre de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_NUM)      */
   if (read(dsmexec_fd, &DSM_NODE_NUM, sizeof(int)) < 0) {
      perror("Erreur lecture DSM_NODE_NUM");
      exit(EXIT_FAILURE);
   }
   
   /* Reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */
   if (read(dsmexec_fd, &DSM_NODE_ID, sizeof(int)) < 0) {
      perror("Erreur lecture DSM_NODE_ID");
      exit(EXIT_FAILURE);
   }
   fprintf(stderr, "[Proc] DSM_NODE_NUM=%d, DSM_NODE_ID=%d\n", DSM_NODE_NUM, DSM_NODE_ID);
   
   /* Reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */
   procs = malloc(sizeof(dsm_proc_conn_t) * DSM_NODE_NUM);
   if (!procs) {
      perror("malloc procs");
      exit(EXIT_FAILURE);
   }

   for (int i = 0; i < DSM_NODE_NUM; i++) {
      if (read(dsmexec_fd, &procs[i], sizeof(dsm_proc_conn_t)) < 0) {
         perror("Erreur lecture procs");
         exit(EXIT_FAILURE);
      }
   }

   /* Initialisation du sémaphore */
   if (sem_init(&sem_sync, 0, 0) != 0) {
      perror("sem_init");
      exit(EXIT_FAILURE);
   }

   /* Affichage des informations de connexion */
   fprintf(stderr, "[%d] Contenu de procs avant connexions:\n", DSM_NODE_ID);
   for(int i = 0; i < DSM_NODE_NUM; i++) {
      fprintf(stderr, "  procs[%d]: machine=%s, port=%d\n", i, procs[i].machine, procs[i].port_num);
   }

   /* Configuration du master_fd pour l'écoute */
   int on = 1;
   if (setsockopt(master_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
      perror("setsockopt(SO_REUSEADDR)");
      exit(EXIT_FAILURE);
   }

   struct sockaddr_in addr;
   socklen_t len = sizeof(addr);
   if (getsockname(master_fd, (struct sockaddr*)&addr, &len) < 0) {

      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(procs[DSM_NODE_ID].port_num);
      addr.sin_addr.s_addr = INADDR_ANY;
      
      if (bind(master_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
         perror("bind");
         exit(EXIT_FAILURE);
      }
   }

   if (listen(master_fd, DSM_NODE_NUM) < 0) {
      perror("listen");
      exit(EXIT_FAILURE);
   }

   fprintf(stderr, "[%d] Socket maître en écoute sur le port %d\n", DSM_NODE_ID, ntohs(addr.sin_port));

   sleep(2);
   /* initialisation des connexions              */ 
   /* avec les autres processus : connect/accept */
  for (int i = 0; i < DSM_NODE_NUM; i++) {
        if (i == DSM_NODE_ID) continue;

        if (DSM_NODE_ID < i) {
            // Le processus courant accepte les connexions des processus de rang plus grand
            int sock = accept(master_fd, (struct sockaddr *)&addr, &len);
            if (sock < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            procs[i].fd = sock;
            fprintf(stderr, "[%d] Connexion acceptée de la part du processus %d\n", DSM_NODE_ID, i);
        } else {
            int nb_essais = 0;
            int connected = 0;

            while (nb_essais < 5 && !connected) {
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0) {
                    perror("socket creation");
                    exit(EXIT_FAILURE);
                }

                struct sockaddr_in sock_addr;
                memset(&sock_addr, 0, sizeof(sock_addr));
                sock_addr.sin_family = AF_INET;
                sock_addr.sin_port = htons(procs[i].port_num);

                struct hostent *host = gethostbyname(procs[i].machine);
                if (!host) {
                    fprintf(stderr, "[%d] Impossible de résoudre %s\n", 
                            DSM_NODE_ID, procs[i].machine);
                    exit(EXIT_FAILURE);
                }
                memcpy(&sock_addr.sin_addr, host->h_addr, host->h_length);

                fprintf(stderr, "[%d] Tentative connexion processus %d (essai %d)\n", 
                        DSM_NODE_ID, i, nb_essais + 1);

                if (connect(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
                    perror("connect");
                    close(sock);
                    nb_essais++;
                    sleep(1);
                } else {
                    procs[i].fd = sock;
                    connected = 1;
                    fprintf(stderr, "[%d] Connexion établie avec le processus %d\n", DSM_NODE_ID, i);
                }
            }

            if (!connected) {
                fprintf(stderr, "[%d] Échec de la connexion avec le processus %d\n", DSM_NODE_ID, i);
                exit(EXIT_FAILURE);
            }
        }
    }

   /* Allocation des pages en tourniquet */

   for(index = 0; index < PAGE_NUMBER; index++) {    
      if ((index % DSM_NODE_NUM) == DSM_NODE_ID) {
         dsm_alloc_page(index);
         dsm_protect_page(index, PROT_READ | PROT_WRITE);
         dsm_change_info(index, WRITE, DSM_NODE_ID);
      } else {
         dsm_alloc_page(index);
         dsm_protect_page(index, PROT_NONE);
         dsm_change_info(index, NO_ACCESS, index % DSM_NODE_NUM);
      }
   }
   
   /* Mise en place du traitant de SIGSEGV */
   memset(&act, 0, sizeof(act));
   act.sa_flags = SA_SIGINFO;
   act.sa_sigaction = segv_handler;
   sigemptyset(&act.sa_mask);
   if (sigaction(SIGSEGV, &act, NULL) < 0) {
      perror("sigaction");
      exit(EXIT_FAILURE);
   }
   fprintf(stderr, "[%d] Handler SIGSEGV installé\n", DSM_NODE_ID);
      
   /* Création du thread de communication */
   /* ce thread va attendre et traiter les requetes */
   /* des autres processus                          */
   pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL);
   /* Adresse de début de la zone de mémoire partagée */
   return ((char *)BASE_ADDR);
}


void dsm_finalize(void) {
    fprintf(stderr, "[%d] Début finalisation DSM\n", DSM_NODE_ID);

   /* fermer proprement les connexions avec les autres processus */
   /* et */
   /* terminer correctement le thread de communication */
    dsm_req_t req_fin;
    req_fin.source = DSM_NODE_ID;
    req_fin.page_num = -1;
    
    for (int i = 0; i < DSM_NODE_NUM; i++) {
        if (i == DSM_NODE_ID) continue;
        dsm_send(i, &req_fin, sizeof(dsm_req_t));
    }
    for (int i = 0; i < PAGE_NUMBER; i++) {
        if (get_owner(i) == DSM_NODE_ID) {
            dsm_free_page(i);
        }
    }

 
    pthread_cancel(comm_daemon);
    pthread_join(comm_daemon, NULL);

    for (int i = 0; i < DSM_NODE_NUM; i++) {
        if (i != DSM_NODE_ID) {
            close(procs[i].fd);
        }
    }
    free(procs);
    sem_destroy(&sem_sync);

    fprintf(stderr, "[%d] Fin finalisation DSM\n", DSM_NODE_ID);
}
