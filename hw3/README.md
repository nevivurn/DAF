# hw3

## Dependencies
- make
- A C99-compliant compiler (gcc)

## Usage
```
$ gcc --version
8.3.0
$ make
$ ./hw3 yeast yeast_400n 100 > yeast_400n.dag
$ ./daf_2min -d yeast -q yeast_400n -a yeast_400n.dag -n 100 -m 100000 > result_dag
```
