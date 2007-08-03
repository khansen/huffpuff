huffpuff: huffpuff.o charmap.o
	$(CC) huffpuff.o charmap.o -o huffpuff

%.o: %.c
	$(CC) -Wall -g -c $< -o $@

.PHONY: clean

clean:
	rm -f *.o huffpuff huffpuff.exe
