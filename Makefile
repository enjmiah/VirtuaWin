# MINGW Makefile for project VirtuaWin
OSTYPE   = $(shell uname -msr)

ifeq ($(findstring CYGWIN,$(OSTYPE)),CYGWIN)
CC      = gcc
CFLAGS	= -mno-cygwin -Wall -O2
LDFLAGS	= -mno-cygwin -O2
RC      = windres 
endif

ifeq ($(findstring MINGW32,$(OSTYPE)),MINGW32)
CC      = gcc
CFLAGS	= -Wall -O2
LDFLAGS	= -O2
RC      = windres 
endif

ifeq ($(findstring Linux,$(OSTYPE)),Linux)
CC      = i586-mingw32msvc-gcc
CFLAGS  = -Wall -O2
LDFLAGS = -O2
RC	= i586-mingw32msvc-windres
endif

TARGET	= VirtuaWin.exe
OBJS	= VirtuaWin.o DiskRoutines.o SetupDialog.o ModuleRoutines.o VirtuaWin.coff
LIBS	= -lshell32 -lcomctl32 -lgdi32 -lmsvcrt

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

VirtuaWin.coff: VirtuaWin.rc
	$(RC) --input-format rc --output-format coff -o $@ -i $<

clean: 
	rm -f $(OBJS)

all:    clean $(TARGET)
