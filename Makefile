all: honeysaver

clean:
	rm honeysaver

honeysaver: honeysaver.c
	gcc honeysaver.c -o honeysaver -lX11 -lpthread -lpam
