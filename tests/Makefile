.PHONY: test libco

all: libco-test

test: libco all
	@echo "==== TEST ===="
	@LD_LIBRARY_PATH=.. ./libco-test

libco:
	@cd .. && make -s

libco-test: main.c
	gcc -I.. -L.. -m64 main.c -o libco-test -lco

clean:
	rm -f libco-test*
