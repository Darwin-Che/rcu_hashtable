#!/bin/bash

sudo insmod rcuht_test.ko \
        input_strategy=big\
        input_limit=50000 \
        input_workers=2 \
        input_hllen=15 \
        input_rdlen=200

sudo rmmod rcuht_test

