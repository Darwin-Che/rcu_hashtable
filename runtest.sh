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

	limit=10000

	if [[ ${strat} =  'rcu' ]] && [[ ${workers} -eq 2 ]]; then
		let limit/=4
	fi

	if [[ ${strat} != 'big' ]] && [[ ${workers} -eq 5 ]]; then
		let limit*=2
	fi
	if [[ ${strat} != 'big' ]] && [[ ${workers} -eq 8 ]]; then
		let limit*=4
	fi

	if [[ ${hllen} -eq 15 ]]; then
		let limit/=2
	fi
	if [[ ${hllen} -eq 16 ]]; then
		let limit/=4
	fi

	if [[ ${rdlen} -eq 200 ]]; then
		let limit/=2
	fi
	if [[ ${rdlen} -eq 500 ]]; then
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
	echo "${strat}  ${workers}  ${hllen}  ${rdlen} ${limit}" >> "result${testrun}.txt"
	awk "/TEST No./ { print \$NF \" \" ${limit}/\$NF }" "log${strat}.txt"  >> "result${testrun}.txt"

}

for testrun in 1 2 3; do
echo "start testrun${testrun}"
for strat in big rcu callrcu; do
	for workers in 2 5 8; do
		for hllen in 14 15 16; do
			for rdlen in 0 200 500; do
				mytest
			done
		done
	done
done
echo "finish testrun${testrun}"
done


