Building from source
====================


## Install dependencies

**Ubuntu 18.04 / 20.04**

Caffe package is present so you can install all dependencies with one command:

	sudo apt install git make gcc g++ libcaffe-cpu-dev libboost-all-dev libgflags-dev libgoogle-glog-dev libprotobuf-dev libopenblas-dev


**Ubuntu (other versions)**

Caffe package no longer present in Ubuntu, use this ppa or build from source:

	sudo add-apt-repository ppa:lemonsqueeze/pachi
	sudo apt update
	sudo apt install git make gcc g++ libcaffe-cpu-dev libboost-all-dev libgflags-dev libgoogle-glog-dev libprotobuf-dev libopenblas-dev


**Other distributions**

  Install [Caffe](http://caffe.berkeleyvision.org) package, or build from source if your distribution doesn't have one.
  
  CPU-only build is fine, no need for GPU, cuda or the other optional dependencies.  
  You need OpenBlas for good performance.
  
  > If caffe is installed in an unusual location set CAFFE_PREFIX in Makefile.
  

## Build

Download pachi repository:

	git clone https://github.com/pasky/pachi
	cd pachi

Edit Makefile, top section has configuration options.  
Currently Unix and Windows are supported (MAC build currently untested).

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
