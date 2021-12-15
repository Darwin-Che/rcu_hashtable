#!/bin/bash

sudo insmod rcuht_test.ko \
        input_strategy=newrcu\
        input_limit=10000 \
        input_workers=10 \
        input_hllen=15 \
        input_rdlen=500

sudo rmmod rcuht_test

