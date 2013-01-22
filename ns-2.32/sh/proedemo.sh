#!/bin/bash

#traffic.tcl: src, dst info
#$1 trace file
#$2 interval

interval=$2;
flow=$3

start=0;
end=200;

traffic=0;
current=0;
current_interval=1;
					
while [ "$start" -le "$end" ]; do
  	
  	current=0;
  	sum=0;
  	
  	while [ "$current" -lt "$interval" ]; do
  		
  		now=$(($start+$current))
			  		
			t=$(grep "at $now\." $1 | wc | awk '{print $1}')
			sum=$(($sum+$t))
			current=$(($current+1))
		
		done
		
		#echo "$sum" | awk '{print '$start' " " $1*1420*8.0/1000000.0}' >> $rpath/$1_sh_tp_$traffic
		#<e2e time="1" flow="0" bytes="11111"/>
		#echo "$sum $interval" | awk '{print '$start' " " $1*1420*8.0/$2/1000000.0}' >> $1_e2e
		
		echo "$sum $interval $start $flow" | awk '{printf "<e2e time=\"%d\" flow=\"%d\" bytes=\"%d\" />\n", $3, $4,  $1*1420*8.0/$2/1000.0}' >> $1_e2e
		
		start=$(($start+$interval))
		
done
