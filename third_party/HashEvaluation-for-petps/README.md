# Build All
```
$ make
```

# Before Run
[You should mount your PMEM* to a NVM-aware filesystem with DAX.](https://docs.pmem.io/persistent-memory/getting-started-guide/creating-development-environments/linux-environments/linux-memmap)
```
$ sudo mkfs.xfs /dev/pmem0
$ sudo mkdir /mnt/pmem
$ sudo mount -o dax /dev/pmem0 /mnt/pmem
$ sudo modprobe msr
```

# Run with PiBench
```
$ make
$ cd bin
$ sudo ./PiBench [lib.so] [args...]

eg:
$ sudo ./PiBench ./Dash.so -r 0 -i 1 -t 32 -n 1024 -p 1024
```
## PiBench 
The `PiBench` executable is generated and supports the following arguments:
```
$ ./PiBench --help
Benchmark framework for persistent indexes.
Usage:
  PiBench [OPTION...] INPUT

      --input arg           Absolute path to library file
  -n, --records arg         Number of records to load (default: 0)
  -N, --negative_ratio arg  Negative search ratio (default: 0.000000)
  -S, --hash_size arg       hashtable size (default: 4096)
  -M, --test_mode arg       Test mode (default: THROUGHPUT)
  -p, --operations arg      Number of operations to execute (default: 1024)
  -t, --threads arg         Number of threads to use (default: 1)
  -r, --read_ratio arg      Ratio of read operations (default: 0.000000)
  -i, --insert_ratio arg    Ratio of insert operations (default: 1.000000)
  -d, --remove_ratio arg    Ratio of remove operations (default: 0.000000)
      --skip_load           Skip the load phase (default: true)
      --distribution arg    Key distribution to use (default: UNIFORM)
      --help                Print help
```
### Test mode

#### Throughput
eg: 
```
$ sudo ./PiBench ./Dash.so -S 16777216 -p 200000000 -M THROUGHPUT
```
#### LOAD_FACTOR

the load factor of hash table during execution.
eg:
```
$ sudo ./PiBench ./CCEH.so -S 16777216 -p 200000000 -M LOAD_FACTOR
```

For Dash, you need to recompile with `DA_FLAGS=-DCOUNTING`
```
$ make clean -C hash/Dash
$ make DA_FLAGS=-DCOUNTING -C hash/Dash
$ make
```
#### RESIZE

```
$ sudo ./PiBench ./Dash.so -S 16777216 -p 200000000 -M RESIZE
```
#### PM info

```
$ sudo ./PiBench ./Dash.so -S 16777216 -p 200000000 -M PM
```

#### LATENCY

```
$ sudo ./PiBench ./Dash.so -S 16777216 -p 200000000 -M LATENCY
```
