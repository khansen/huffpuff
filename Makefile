INSTALL = install
CFLAGS = -Wall -g
LFLAGS =
OBJS = charmap.o huffpuff.o

prefix = /usr/local
datarootdir = $(prefix)/share
datadir = $(datarootdir)
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
infodir = $(datarootdir)/info
mandir = $(datarootdir)/man

huffpuff: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -o huffpuff

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: huffpuff
	$(INSTALL) -m 0755 huffpuff $(bindir)
	$(INSTALL) -m 0444 huffpuff.1 $(mandir)/man1

clean:
	rm -f $(OBJS) huffpuff huffpuff.exe

.PHONY: clean install
