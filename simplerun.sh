#!/bin/bash

sudo insmod rcuht_test.ko \
        input_strategy=newrcu\
        input_limit=1000 \
        input_workers=10 \
        input_hllen=14 \
        input_rdlen=100000

sudo rmmod rcuht_test

