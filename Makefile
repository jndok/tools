SRCDIR = src
BUILDDIR = build

getpanic:
	clang $(SRCDIR)/getpanic.c -o $(BUILDDIR)/getreport -L/usr/local/lib/ -lplist -limobiledevice

clean:
	rm -rf build/*

all:
	make getpanic
