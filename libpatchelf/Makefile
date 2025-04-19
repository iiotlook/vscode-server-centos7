CROSS_PREFIX ?=
CXX = $(CROSS_PREFIX)g++
AR = $(CROSS_PREFIX)gcc-ar

CFLAGS += -std=c++17 -Wall -Wextra

LIBDIR ?= lib

HEADERS = $(wildcard *.h)
SRC = libpatchelf.cc
OBJ = libpatchelf.o
LIB = $(LIBDIR)/libpatchelf.a

all: $(LIB)

$(OBJ): $(SRC) $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $(OBJ) $(SRC)

$(LIB): $(OBJ)
	mkdir -p $(LIBDIR)
	$(AR) rcs $(LIB) $(OBJ)

clean:
	$(RM) -r $(OBJ) $(LIBDIR)

.PHONY: all clean
