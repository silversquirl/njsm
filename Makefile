CPP := g++

LDFLAGS := -ljack

.PHONY: all clean dist-clean tag
all: njsm

tag:
	ctags -R .

clean:
	rm -f *.o

dist-clean: clean
	rm -f njsm

njsm: njsm.o
	$(CPP) -o $@ $< $(LDFLAGS)

%.o: %.cpp
	$(CPP) -o $@ -c $<

