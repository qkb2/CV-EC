zad1:
	gcc zad1.c -o zad1 -lm

zad5:
	gcc zad5.c -o zad5 -lm

zad5qemu:
	aarch64-linux-gnu-gcc zad5.c -o zad5 -O3

zad5qemu-static:
	aarch64-linux-gnu-gcc zad5.c -o zad5 -static -O3

zad5-run:
	./zad5 sample.ppm test.pgm

qemu-zad5-run:
	qemu-aarch64 ./zad5 sample.ppm test.pgm

clean:
	rm zad1