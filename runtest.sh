#!/bin/bash

:'
sudo insmod rcuht_test.ko \
	input_strategy=big \
	input_limit=100 \
	input_workers=10  \
	input_hllen=14 \
	input_rdlen=0
#'

make
if [[ $? -ne 0 ]]; then
	exit 1
fi

sudo rmmod rcuht_test

rm result*.txt

# called with ${strat}, ${workers}, ${hllen}, ${rdlen}
function mytest() {

	limit=8000

	if [[ ${hllen} -eq 15 ]]; then
		let limit/=2
	fi
	if [[ ${hllen} -eq 16 ]]; then
		let limit/=4
	fi

	if [[ ${rdlen} -eq 2 ]]; then
		let limit/=2
	fi

	if [[ ${rdlen} -eq 5 ]]; then
		let limit/=4
	fi

	echo "testrun ${testrun} : starting ${strat}, workers=${workers}, hllen=${hllen}, rdlen=${rdlen}, limit=${limit}"

	sudo insmod rcuht_test.ko \
		input_strategy=${strat} \
		input_limit=${limit} \
		input_workers=${workers} \
		input_hllen=${hllen} \
		input_rdlen=${rdlen}

	sudo rmmod rcuht_test

	tail /var/log/syslog -n40 > "log${strat}.txt"
	echo "${strat}  ${workers}  ${hllen}  ${rdlen}" >> "result${testrun}.txt"
	awk "/TEST No./ { print ${limit} / \$NF * 1000 }" "log${strat}.txt"  >> "result${testrun}.txt"

}

for testrun in 1 2 3; do

for strat in big rcu callrcu; do
	for workers in 2 6 10; do
		for hllen in 14 15 16; do
			for rdlen in 0 2 5; do
				mytest
			done
		done
	done
done

done


