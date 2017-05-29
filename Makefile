CXX := clang -std=c++11

LDFLAGS := -lstdc++ -ljack -llo
CXXFLAGS := -Wall

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
	$(CXX) -o $@ $< $(LDFLAGS)

%.o: %.cpp
	$(CXX) -o $@ -c $< $(CXXFLAGS)

