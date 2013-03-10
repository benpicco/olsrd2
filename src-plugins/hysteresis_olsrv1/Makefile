LIBNAME=olsr-$(shell basename $(CURDIR))
CC=arm-elf-gcc
RIOTBASE=/home/benpicco/µkleos/RIOT-new
OONFBASE=/home/benpicco/µkleos/oonf_api
OLSRBASE=/home/benpicco/µkleos/olsrd2

all: $(LIBNAME).a

$(LIBNAME).a:
	$(CC) -std=c99 -DRIOT -c -I .. -I $(RIOTBASE) -I $(RIOTBASE)/sys/include -I $(RIOTBASE)/core/include -I $(OONFBASE)/src-api -I $(OLSRBASE)/src *.c
	ar -r $(LIBNAME).a *.o

clean:
	rm *.o
	rm *.a
