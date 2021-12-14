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

	limit=$(sed "${limitline}q;d" limit.txt)

	let limit/=2

#	limit=2000

	echo "starting ${strat}, workers=${workers}, hllen=${hllen}, rdlen=${rdlen}, limit=${limit}"

	sudo insmod rcuht_test.ko \
		input_strategy=${strat} \
		input_limit=${limit} \
		input_workers=${workers} \
		input_hllen=${hllen} \
		input_rdlen=${rdlen}

	sudo rmmod rcuht_test

	tail /var/log/syslog -n40 > "log${strat}.txt"
	echo "${strat}  ${workers}  ${hllen}  ${rdlen} ${limit}" >> "result.txt"
	awk "/TEST No./ { print \$NF \" \" ${limit}/\$NF }" "log${strat}.txt"  >> "result.txt"

}

limitline=1

for strat in big rcu newrcu; do
	for workers in 2 4 6 8; do
		for hllen in 14 16; do
			for rdlen in 0 500; do
				mytest
				let limitline+=1
			done
		done
	done
done


