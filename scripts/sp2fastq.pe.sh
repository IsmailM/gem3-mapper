#!/bin/bash

RMAP=$1

awk '{nr=NR-1; printf("%s\t",$1); if(nr%4==3) printf "\n"}' ../data/sample.chr1.l100.pe.fastq > t0;
awk '{$1=substr($1,2,length($1)-3); printf("%s\n",$0);}' t0 > t1
awk '{print $1}' $RMAP > t2

join t1 t2 | awk '{printf("@%s/1\n%s\n+\n%s\n",$1,$2,$4)}' > ${RMAP%.rmap}.fastq

## rm t0 t1 t2