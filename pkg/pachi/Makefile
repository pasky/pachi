# To package a new version of Pachi:
#   pick new package name and branch (this Makefile)
#   add an entry to debian/changelog for target distribution
#   fix debian/patches if needed
#   cross fingers & make
#
# See ppa:lemonsqueeze/pachi if official caffe package is missing.


# Debian package name
BASE=pachi-go_12.82.20230708
NAME=pachi-go_12.82.20230708-jammy1

# Which branch to package ?
GIT_BRANCH=master

DATAFILES = detlef54.prototxt detlef54.trained josekifix.gtp joseki19.gtp opening.dat patterns_mm.gamma patterns_mm.spat 

################################################################

# Binary package
ARCH=$(shell dpkg --print-architecture)
DEB=$(NAME)_$(ARCH).deb


all: deb-src deb

# don't build binary package
fast: deb-src


################################################################

# Debian source files
deb-src: $(NAME)_source.changes

$(NAME)_source.changes: $(BASE).orig.tar.gz
	cd src && debuild -S

$(BASE).orig.tar.gz:
	make checkout
	make copy_data
	cd tmp && tar -cvzf ../$@ *
	rm -rf tmp
	cd src && tar -xzf ../$@       # populate src, makes debugging a lot easier

checkout:
	mkdir tmp
	cd tmp &&          \
	   git init &&     \
           git remote add -f origin ../../../.git  && \
           git checkout $(GIT_BRANCH)              && \
	   QUILT_PATCHES=../src/debian/patches quilt push -a  && \
	   make  build.h   && \
           quilt pop -a

copy_data:
	@echo ""
	@echo "copying datafiles:"
	@for file in $(DATAFILES); do         \
           echo "cp ../../$$file tmp/";       \
           cp ../../$$file tmp/  ||  exit 1;  \
        done


################################################################

# Debian binary package
deb: $(DEB)

$(DEB):
	cd src && dpkg-buildpackage -b


################################################################


clean:
	-@rm -rf tmp   src/.pc                   2>/dev/null
	-@rm *~ pachi-*                          2>/dev/null
	-@cd src && rm -rf `ls | grep -v debian` 2>/dev/null
