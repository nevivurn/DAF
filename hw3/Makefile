CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wvla -Og -g

all: hw3

test: yeast.dag

yeast.dag: hw3
	./hw3 ../yeast ../yeast_400n 100 > yeast.dag

clean:
	rm -rf hw3 yeast.dag
