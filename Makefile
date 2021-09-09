CXX ?= g++

all: htc2uhts

htc2uhts:
	$(CXX) htc2uhts.cpp -o htc2uhts -lz $(CXXFLAGS)

clean:
	rm -f htc2uhts
