co: co.c clean
	gcc co.c -fPIC -shared -o libco.so
.PHONY: clean
clean:
	rm -f libco.so test/test