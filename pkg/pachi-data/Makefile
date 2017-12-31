# data files are packaged in 'pachi-go-data' package,
# which shouldn't need to be updated unless the data files
# actually change.
#
# To generate new version:
#   pick new package name (this Makefile)
#   add an entry to debian/changelog for target distribution
#   make    (will take a while)
#
# Since it's data-only no need to generete it for each ubuntu
# series, binary package can just be copied over in launchpad.


# Debian package name
BASE=pachi-go-data_12.00
NAME=pachi-go-data_12.00-1


################################################################


DEB=$(NAME)_all.deb


all: deb-src deb

# don't build binary package
fast: deb-src


################################################################

# Debian source files
deb-src: $(NAME)_source.changes

$(NAME)_source.changes: $(BASE).orig.tar.xz
	cd src && debuild -S

$(BASE).orig.tar.xz:
	make copy-data-files
	cd src && tar -cvJf ../$@  `ls | grep -v debian`

copy-data-files:
	cp -p ../../golast.trained     src/
	cp -p ../../golast19.prototxt  src/
	cp -p ../../joseki19.pdict     src/
	cp -p ../../opening.dat        src/
	cp -p ../../patterns.prob      src/
	cp -p ../../patterns.spat      src/


################################################################

# Debian binary package
deb: $(DEB)

$(DEB):
	cd src && dpkg-buildpackage -b


################################################################


clean:
	-@rm *~ pachi-* src/*.*     2>/dev/null
