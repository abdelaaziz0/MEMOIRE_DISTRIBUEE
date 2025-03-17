ALL: default 

CC       = gcc
CLINKER  = $(CC)
OPTFLAGS = -O0 


SHELL = /bin/sh

CFLAGS  = -DREENTRANT -g
CCFLAGS = $(CFLAGS)
LIBS    = -lpthread 

EXECS = exemple 

default: $(EXECS) 

$(EXECS): %: %.o dsm.o 

#%.o:%.c
#	$(CC) $(CFLAGS) -c $<

dsm.o: dsm_impl.h

%:%.o dsm.o 
	$(CLINKER) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@-mv $@ ../Phase1/bin/		
clean:
	@-/bin/rm -f *.o *~ PI* $(EXECS) *.out core  *.so 

