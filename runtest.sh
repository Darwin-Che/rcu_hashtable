#!/bin/bash

make

cores="10"
limit="1000"
workers="10"


strat="rcu"

sudo insmod rcuht_test.ko \
	input_strategy=${strat} \
	input_limit=${limit} \
	input_workers=${workers}

tail /var/log/syslog -n50 > 'log.txt'

mkdir -p "result/${cores}"
resultfile="result/${cores}/${strat}_${workers}.txt"

awk "/TEST No./ { print ${limit} / \$NF * 1000 }" log.txt > ${resultfile}

sudo rmmod rcuht_test

strat="big"

sudo insmod rcuht_test.ko \
	input_strategy=${strat} \
	input_limit=${limit} \
	input_workers=${workers}

tail /var/log/syslog -n50 > 'log.txt'

mkdir -p "result/${cores}"
resultfile="result/${cores}/${strat}_${workers}.txt"

awk "/TEST No./ { print ${limit} / \$NF * 1000 }" log.txt > ${resultfile}

sudo rmmod rcuht_test
