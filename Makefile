# Make the buffer program

# You might need to add the following to CGFLAGS:
#
# Add -DSYS5 for A System 5 (USG) version of Unix
#   You should also add -DSYS5 for Ultrix, AIX, and Solaris.
# Add -DDEF_SHMEM=n if you can only have n bytes of shared memory
#   (eg: -DDEF_SHMEM=524288 if you can only have half a meg.)
# Add -DAMPEX to change the default settings suitable for the high capacity
#   Ampex drives, such as the DST 310.

CC=gcc
CFLAGS=-Wall

# Where to install buffer and its manual pages
INSTBIN=/usr/local/bin
INSTMAN=/usr/man/manl
# The manual page section (normally l or 1)
S=l

RM=/bin/rm
ALL=README buffer.man Makefile buffer.c sem.c sem.h COPYING

all: buffer

buffer: buffer.o sem.o
	$(CC) -o buffer $(CFLAGS) buffer.o sem.o

clean:
	$(RM) -f *.o core buffer .merrs

install: buffer
	rm -f $(INSTBIN)/buffer
	cp buffer $(INSTBIN)/buffer
	chmod 111 $(INSTBIN)/buffer
	rm -f $(INSTMAN)/buffer.$S
	cp buffer.man $(INSTMAN)/buffer.$S
	chmod 444 $(INSTMAN)/buffer.$S

buffer.tar: $(ALL)
	$(RM) -f buffer.tar
	tar cvf buffer.tar $(ALL)

buffer.shar: $(ALL)
	$(RM) -f buffer.shar
	shar $(ALL) > buffer.shar
