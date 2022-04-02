co: co.c clean
	gcc co.c -g -fPIC -shared -o libco.so
.PHONY: clean
clean:
	rm -f libco.so