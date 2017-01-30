NAME=$(notdir $(PWD))

ARCH?=x86

#CFLAGS=-g -Wall -Wa,-ahdlcs=$@.lst $(EXTRA_CFLAGS)
CFLAGS=-g -Wall $(EXTRA_CFLAGS)
LDFLAGS=$(EXTRA_LDFLAGS)

ifeq ($(ARCH),mips)
CC             = 
LD             = 
EXTRA_CPPFLAGS =
EXTRA_CFLAGS   = -mabi=32
EXTRA_LDFLAGS  = -mabi=32
LDLIBS         = 
endif

ifeq ($(ARCH),arm)
CC             = 
LD             = 
EXTRA_CPPFLAGS =
EXTRA_CFLAGS   =
EXTRA_LDFLAGS  =
LDLIBS         = 
endif

all: $(NAME)

$(NAME): $(NAME).o

$(NAME).o: Makefile

clean:
	-rm -f *.o *.lst *~ $(NAME)

.PHONEY: all clean
