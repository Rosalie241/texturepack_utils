CXX := g++
CC 	:= gcc

all: htc2uhts hts2png

%: %.cpp
	$(CXX) $< -o $@ -lz $(EXTRACFLAGS)

%: %.c
	$(CC) $< -o $@ -lpng -lz $(EXTRACFLAGS)

clean:
	rm -f htc2uhts hts2png
