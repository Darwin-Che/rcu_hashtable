#!/bin/bash

sudo insmod rcuht_test.ko \
        input_strategy=big\
        input_limit=19000 \
        input_workers=2 \
        input_hllen=16 \
        input_rdlen=500

sudo rmmod rcuht_test

