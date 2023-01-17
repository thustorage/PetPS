set -e

#gcc-9
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update -y
sudo apt install -y gcc-9


sudo apt-get install \
    g++ \
    cmake \
    libboost-all-dev \
    libevent-dev \
    libdouble-conversion-dev \
    libgoogle-glog-dev \
    libgflags-dev \
    libiberty-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    make \
    zlib1g-dev \
    binutils-dev \
    libjemalloc-dev \
    libssl-dev \
    pkg-config \
    libunwind-dev

sudo apt remove libgoogle-glog-dev

cd ~
rm -rf fmt
git clone https://github.com/fmtlib/fmt.git && cd fmt
mkdir _build && cd _build
CFLAGS='-fPIC' CXXFLAGS='-fPIC -Wl,-lrt' cmake ..
make -j$(nproc)
sudo make install
cd -


# glog
sudo apt remove -y glog
cd ~
git clone https://github.com/google/glog
cd glog
git checkout v0.5.0
mkdir build
cd build
cmake ..
make -j
sudo make install
cd -


# folly
cd ~
git clone https://github.com/facebook/folly
cd folly
git checkout v2022.01.17.00
rm -rf _build
mkdir _build
cd _build
CFLAGS='-fPIC' CXXFLAGS='-fPIC -Wl,-lrt' CC=/usr/bin/gcc CXX=/usr/bin/g++ cmake ..
make -j
rm -rf /home/${USER}/folly-install
make DESTDIR=/home/${USER}/folly-install install
cd -


# PB
sudo apt install -y libtool
wget https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.10.0.tar.gz
tar zxvf v3.10.0.tar.gz
cd protobuf-3.10.0
./autogen.sh 
./configure
make -j
sudo make install
cd -


wget https://github.com/gperftools/gperftools/releases/download/gperftools-2.7/gperftools-2.7.tar.gz
tar -zxf gperftools-2.7.tar.gz
cd gperftools-2.7/
./configure
make -j
sudo make install
cd -


# pip3
pip3 install numpy pandas zmq paramiko