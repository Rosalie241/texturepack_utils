CC ?= gcc

all: htc2uhts

htc2uhts:
	g++ -g htc2uhts.cpp -o htc2uhts -lz

clean:
	rm -f htc2uhts
