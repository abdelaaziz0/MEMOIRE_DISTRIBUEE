#include "dsm.h"

int main(int argc, char **argv)
{
   char *pointer; 
   char *current;
   int value;

   pointer = dsm_init(argc,argv);
   current = pointer;

   printf("[%i] Coucou, mon adresse de base est : %p\n", DSM_NODE_ID, pointer);
   
   if (DSM_NODE_ID == 0)
     {
       current += 4*sizeof(int);
       value = *((int *)current);
       printf("[%i] valeur de l'entier : %i\n", DSM_NODE_ID, value);
       sleep(2);
     } 
   else if (DSM_NODE_ID == 1)
     {
       //current += PAGE_SIZE;
       sleep(1);
       current += 16*sizeof(int);

       value = *((int *)current);
       printf("[%i] valeur de l'entier : %i\n", DSM_NODE_ID, value);
     }
     sleep(1);
   dsm_finalize();
   return 1;
}
