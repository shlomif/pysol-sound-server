#
# written by Markus F. X. J. Oberhumer <markus@oberhumer.com>
#
# to use a specific python version call
#   `make PYTHON=python2.3'
#

SHELL = /bin/sh

PYTHON = python2.3
PYTHON = python

all build:
	$(PYTHON) setup.py build

semi-static:
	$(PYTHON) setup.py build --semi-static

test: build
	$(PYTHON) tests/playmus.py tests/*.mp3 tests/*.mod

# (needs GNU binutils)
strip: build
	strip -p --strip-unneeded build/lib*/*.so
	chmod -x build/lib*/*.so

install install_lib:
	$(PYTHON) setup.py $@

clean:
	-rm -rf build dist
	-rm -f *.pyc *.pyo */*.pyc */*.pyo
	-rm -f MANIFEST

distclean: clean
	-rm -f setup.py

maintainer-clean: distclean

dist sdist: distclean
	$(PYTHON) setup.py sdist

.PHONY: all build semi-static test strip install install_lib clean distclean maintainer-clean dist sdist

.NOEXPORT:
