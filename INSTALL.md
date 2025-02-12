Building from source
====================


## Dependencies

- Install caffe dependencies.
  On Debian / Ubuntu this is:

	sudo apt install git make gcc g++ libboost-all-dev libgflags-dev libgoogle-glog-dev libprotobuf-dev libopenblas-dev

- Caffe
  Install caffe package if your distribution has one:

	sudo apt install libcaffe-cpu-dev

  For older Ubuntu there might be one in my ppa:

	sudo add-apt-repository ppa:lemonsqueeze/pachi
	sudo apt update
	sudo apt install libcaffe-cpu-dev

  Otherwise build [caffe](http://caffe.berkeleyvision.org) from source:

  CPU-only build is fine, no need for GPU, cuda, python or the other optional dependencies.  
  You need OpenBlas for good performance.

	sudo apt install libhdf5-dev protobuf-compiler
	git clone https://github.com/BVLC/caffe
	cd caffe
	cp Makefile.config.example Makefile.config

  Edit Makefile.config:

	CPU_ONLY := 1
	...
	USE_OPENCV := 0
	USE_LEVELDB := 0
	USE_LMDB := 0
	...
	BLAS := open

  Build and install caffe:

	make all
	make test
	make distribute
	make install		# default: /usr/local

  If `make distribute` insists on building python edit Makefile:

	-$(DISTRIBUTE_DIR): all py | $(DISTRIBUTE_SUBDIRS)
	+$(DISTRIBUTE_DIR): all | $(DISTRIBUTE_SUBDIRS)

  See caffe [installation instructions](http://caffe.berkeleyvision.org/installation.html) if you run into issues.

- KataGo dependencies (optional)
  Install cmake and Eigen3 if you want to do the KataGo CPU build (Pachi uses it for joseki purposes).

	sudo apt install cmake libeigen3-dev

  Compiler should support at least C++14.


## Build

Download pachi repository:

	git clone https://github.com/pasky/pachi
	cd pachi

Edit Makefile, top section has configuration options.  
Currently Unix and Windows are supported (MAC build currently untested).

> If caffe is installed in an unusual location set CAFFE_PREFIX.

To build Pachi type:

	make clean
	make


## Download datafiles

Get dcnn data files from github (not kept in git repo):

	make datafiles

Or if that fails you can get them manually from
[here](https://github.com/pasky/pachi/releases/download/pachi_networks/detlef54.zip).


## Test

Run basic tests:

	make test

You can play around with pachi in current directory without installing (run `./pachi`).


## Install

	make install

Install pachi in BINDIR (default /usr/bin) and datafiles in DATADIR (default /usr/share/pachi-go).

Edit Makefile or pass variables to change install directories (if you change PREFIX / DATADIR you need to rebuild).

For example, build and install to /usr/local:

	make clean
	make          PREFIX=/usr/local 
	make install  PREFIX=/usr/local
