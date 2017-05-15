CPP := g++

LDFLAGS := -ljack -llo

.PHONY: all clean dist-clean tag run-nsm
all: njsm

clean:
	rm -f *.o

dist-clean: clean
	rm -f njsm

tag:
	ctags -R .

run-nsm:
	PATH=.:$$PATH non-session-manager

njsm: njsm.o
	$(CPP) -o $@ $< $(LDFLAGS)

%.o: %.cpp
	$(CPP) -o $@ -c $<

