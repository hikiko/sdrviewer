PREFIX = /usr/local

csrc = $(wildcard src/*.c)
src = $(wildcard src/*.cc)
obj = $(src:.cc=.o) $(csrc:.c=.o)

bin = sdrviewer

CXXFLAGS = -pedantic -Wall -g
CFLAGS = -pedantic -Wall -g
LDFLAGS = -lGL -lglut -lGLEW -lresman

$(bin): $(obj)
	$(CXX) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: install
install: $(bin)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(bin) $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(bin)
