
from os import system
from typing import List
import concurrent.futures
import subprocess
import time


master = '10.0.2.130'

clients = [
    '10.0.2.110',
    '10.0.2.111',
    '10.0.2.112',
    '10.0.2.113',
    '10.0.2.114',
    '10.0.2.115',
    '10.0.2.116',
    '10.0.2.117',
]

servers = [
    # '10.0.2.130',
    '10.0.2.135',
    '10.0.2.136',
    '10.0.2.137',
]

all_machines_aside_master = clients + servers

all_machines = clients + servers + [master]


# client install docker
SSHPASS = 'sshpass -p 1234'
SSH = "ssh -o StrictHostKeyChecking=no"


def ParallelSSH(hosts, command):
    print(hosts, command)

    def command_fn(host):
        subprocess.run(
            f'''{SSH} {host} "{command}"''', shell=True, check=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
        for each in hosts:
            executor.submit(command_fn, each)


def install_apt(hosts, apts):
    ParallelSSH(hosts, f"sudo apt update -y")
    ParallelSSH(hosts, f"sudo apt install -y {apts}")


def install_docker(hosts):
    ParallelSSH(hosts, "curl -fsSL https://get.docker.com -o get-docker.sh")
    ParallelSSH(hosts, "sh ./get-docker.sh")


def install_sshkey(hosts):
    for each in hosts:
        subprocess.run(f"{SSHPASS} ssh-copy-id {each}", shell=True)


def install_eachserver_key_to_master(hosts):
    ParallelSSH(
        hosts, f'ls .ssh/id_rsa >/dev/null || ssh-keygen -t rsa -q -f "$HOME/.ssh/id_rsa" -N ""')
    ParallelSSH(hosts, f"{SSHPASS} ssh-copy-id {master}")


def mount_master(hosts):
    ParallelSSH(
        hosts, f"cd /home/xieminhui; mkdir -p petps;")
    ParallelSSH(
        hosts, f"cd /home/xieminhui; ls petps;")

    ParallelSSH(
        hosts, f"fusermount -u petps;")
    ParallelSSH(
        hosts, f"sudo mount {master}:/home/xieminhui/petps petps")


def installFolly(hosts):
    install_apt(hosts, "g++ cmake libboost-all-dev libevent-dev libdouble-conversion-dev libgoogle-glog-dev libgflags-dev libiberty-dev liblz4-dev liblzma-dev libsnappy-dev make zlib1g-dev binutils-dev libjemalloc-dev libssl-dev pkg-config \
                libunwind-dev")
    ParallelSSH(hosts, "sudo apt remove -y libgoogle-glog-dev")
    ParallelSSH(hosts, "git clone https://github.com/google/glog")
    ParallelSSH(hosts, "cd glog && git checkout v0.5.0 && git checkout v0.5.0 && mkdir build && cd build && cmake .. && make -j && sudo make install")
    ParallelSSH(hosts, "git clone https://github.com/facebook/folly")

    ParallelSSH(hosts, "git clone https://github.com/fmtlib/fmt.git")
    ParallelSSH(
        hosts, "cd fmt && mkdir _build && cd _build && cmake .. && make -j && sudo make install")

    ParallelSSH(hosts, "cd folly && git checkout v2022.01.17.00 && mkdir _build")
    ParallelSSH(hosts, "rm -rf folly/_build && mkdir folly/_build")
    ParallelSSH(hosts, "cd folly/_build && CFLAGS='-fPIC' CXXFLAGS='-fPIC -Wl,-lrt' CC=/usr/bin/gcc CXX=/usr/bin/g++ cmake .. && make -j && make DESTDIR=/home/xieminhui/folly-install install")


def installGCC9(hosts):
    ParallelSSH(hosts, "sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y")
    ParallelSSH(hosts, "sudo apt update -y && sudo apt install -y gcc-9 g++-9")


if __name__ == "__main__":
    # Do not change order
    # install_sshkey(all_machines_aside_master)
    # install_apt(all_machines_aside_master, "sshfs sshpass")
    # install_apt(all_machines_aside_master, "nfs-common")
    # install_eachserver_key_to_master(all_machines_aside_master)
    mount_master(all_machines_aside_master)
    # Do not change order done
    # installFolly(all_machines_aside_master)
    # install_apt(all_machines_aside_master, "libmemcached-dev")

    # ParallelSSH(all_machines_aside_master,
    #             "sudo echo 'export LC_ALL=C' >>/etc/profile")
    # ParallelSSH(all_machines_aside_master,
    #             "sudo echo 'unset LANGUAGE' >>/etc/profile")

    # installGCC9(clients)

    install_apt(
        all_machines, "linux-tools-$(uname -r) linux-cloud-tools-$(uname -r)")
    ParallelSSH(all_machines,
                "sudo cpupower frequency-set -g performance >/dev/null")

    # install_docker(clients)
    pass
