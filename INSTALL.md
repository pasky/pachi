## Compiling

Currently Unix, Mac and Windows are supported.  
To build Pachi, simply type:

	make


In case you hit compilation issues (e.g. when building on MacOS/X)
or want to change the build configuration, check the user configurable
section at the top of the Makefile.

Here is an example for installing all dependencies and compiling Pachi
from source under Ubuntu 18.04:

	sudo apt install git make gcc g++ libcaffe-cpu-dev libboost-all-dev libgflags-dev libgoogle-glog-dev libprotobuf-dev libopenblas-dev
	git clone https://github.com/pasky/pachi
	cd pachi
	make

Install libcaffe-cuda-dev instead for nvidia gpu acceleration.  
Non-dcnn build just needs git make and gcc.

Otherwise:
- Install [Caffe](http://caffe.berkeleyvision.org)  
  CPU-only build is fine, no need for GPU, cuda or the other optional dependencies.  
  You need openblas for good performance.
- Edit Makefile, point it to where caffe is installed and build.

After compiling and setting up data files you can install pachi with:

    make install
    make install-data

Pachi will look for extra data files (such as dcnn, pattern, joseki or
fuseki database) in pachi's system directory (`/usr/local/share/pachi`
by default) as well as current directory. System data directory can be
overridden at runtime by setting `DATA_DIR` environment variable.
