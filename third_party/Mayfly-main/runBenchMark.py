#!/usr/bin/python

#- * - coding : utf - 8 - * -

import os
import sys
import time
import subprocess
import signal
from time import gmtime, strftime
from time import monotonic as timer
from itertools import product
from subprocess import Popen, PIPE, TimeoutExpired


if not os.path.exists('result'):
    os.makedirs('result')

def getResName(s):
    postfix = ""
    if len(sys.argv) > 1:
        postfix = sys.argv[1]
    return 'result/' + s + "-" + postfix + strftime("-%m-%d-%H-%M", gmtime())  + ".res"

# async
def execCommand(command):
    subprocess.Popen(command, shell=True)

def Sed(s, n):
    command = "sed  -i 's/" + s + ".*$/" + s + " " + str(n)  + "/g'  test/rowan_client_rpc.cpp"

    os.system(command)

def getMaxTP():
    f = open('./build/iops', 'r')
    l = [0]
    for line in f:
        try:
            n = float(line)
            l.append(n)
        except:
            continue

    f.close()
    return sorted(l, reverse=True)

# s_th_arr = [24]
# c_th_arr = [1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 35]
# c_wnd_arr = [4]
# w_ratio_arr = [50]


s_th_arr = [24]
c_th_arr = [35]
c_wnd_arr = [4]
w_ratio_arr = [5]

#start benchmark
os.system("bash ./stop_all.sh")
file_name = getResName('bench')
fp = open(file_name, 'w')
for s_th, write_ratio, c_th, c_wdn in product(s_th_arr, w_ratio_arr, c_th_arr, c_wnd_arr):

    fp.writelines("\nserver_thread: " + str(s_th) + "\n")
    fp.writelines("client_thread: " + str(c_th) + "\n")
    fp.writelines("client_window: " + str(c_wdn) + "\n")
    fp.writelines("write_ratio: " + str(write_ratio) + "\n")

    Sed("const int write_ratio =", str(write_ratio) + ";")
    Sed("const int kWindowSize =", str(c_wdn) + ";")
   
    execCommand("bash ./start_server.sh " + str(s_th))
    time.sleep(30) 
 
    start = timer()
    with Popen("bash ./start_client.sh " + str(s_th) + " " + str(c_th), shell=True, stdout=PIPE, preexec_fn=os.setsid) as process:
        try:
            output = process.communicate(timeout=60 * 8)[0]
        except TimeoutExpired:
            os.killpg(process.pid, signal.SIGINT) # send signal to the process group
            output = process.communicate()[0]
        print('Elapsed seconds: {:.2f}'.format(timer() - start))
    
    os.system("bash ./stop_all.sh")

    res = getMaxTP()

    if len(res) < 3:
        fp.writelines('NO OUTPUT\n------------------------------------------------------\n\n')
        fp.flush()
        continue

    fp.writelines(str(res[1:5]) + "\n")
    latency = open('./build/latency', 'r')
    for line in latency:
        fp.writelines(line)
    latency.close()

    fp.writelines('------------------------------------------------------\n\n')
    fp.flush()
    
    os.system("cat ./build/iops")
    print(str(res[1:5]))
    os.system("cat ./build/latency")

    
fp.close()