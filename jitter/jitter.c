/**
 * Copyright (c) 2017 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *   Original Creation Date : Sept 1, 2012
 *   Last Modified : Dec 17, 2017
 *   Author: Shrikant M. Shah
 *     Contributor: Patrick Lu
 *
 *   Program for measuring software jitter
 *
 *   This program is meant for measuring the "jitter" in the execution time caused by OS and/or the underlying architecture.
 *   The program does the following:
 *		- Calls a dummy function which consists of a small piece of code block being executed in a loop. The loop count is user defined (default 80,000).
 *		- Measures the Execution time for this function in Core cycles
 *		- Compares the Execution time with Instantaneous Minimum and Maximum execution time values tracked during the display update interval. These values are adjusted based on the comparison.
 *		- Compares the execution time with Absolute Minimum and maximum execution values since the program started. These values are adjusted based on the comparison.
 *		- Repeats above operation.
 *      - Updates the display with the statistics once after the dummy function is executed certain number of times (default 20,000).
 *			The display update interval is approximately set to 1 second. At this time, Instantaneous Minimum and Maximum execution time are initialized.
 *		- The statistics include Instantaneous Minimum and Maximum execution times measured during the display interval and jitter.
 *   The most important statistic to watch is Inst_jitter, the difference between instantaneous Minimum and Maximum execution times
 *   Display update interval is also user programmable.
 *   Note that Absolute Minimum and Maximum execution times keep track of variations in the execution time over a long period of time.
 *   The Absolute Min and Max values can be reset using User signal.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>

union timestampclock
{
	uint64_t timestamp64;
	struct
	{
		uint32_t low;
		uint32_t high;
	};
}a;

#define MINVAL 0xfffffffL

int resetStats=0;
unsigned int loopcount = 80000;
unsigned int displayUpdate = 20000;
#ifdef PROCESSOR_TRACE
unsigned int jitter_threshold = 15000;
#endif
unsigned int iterations = 200;

char *title = "   Inst_Min   Inst_Max   Inst_jitter last_Exec  Abs_min    Abs_max      tmp       Interval     Sample No\n";

int dummyop(unsigned int loops, unsigned startval)
{
	register unsigned int i,a , k;
	a = startval ;
	k = loops;
	for(i=0; i < k; i++)
	{
		a += (3*i + k);
		a &=0x7f0f0000;
	}

	return a;
}

inline uint64_t TimeStampCounter()
{
	union timestampclock tscount;
	uint32_t eax,edx;

	__asm__ volatile("mfence");

	__asm__ __volatile__("rdtsc" : "=a" (eax), "=d" (edx));
	tscount.low = eax;
	tscount.high = edx;
	return tscount.timestamp64;
}

inline uint64_t TimeStampCounterEnd()
{
	union timestampclock tscount;
	uint32_t eax,edx;

	__asm__ __volatile__("rdtscp" : "=a" (eax), "=d" (edx)::"%rcx");
	tscount.low = eax;
	tscount.high = edx;
	__asm__ __volatile__("rdtscp" : "=a" (eax), "=d" (edx)::"%rcx");
	return tscount.timestamp64;
}

uint64_t clockdiff(uint64_t starttime,uint64_t  endtime)
{

	uint64_t a ;
	if (starttime > endtime)
		a = starttime - endtime;
	else
		a = endtime - starttime;

	return a ;
}

void signalHandler(int signalno)
{
	if (signalno == SIGUSR1)
	{
		printf("Resettings Absolute Min and Max counts\n");
		resetStats = 1;
	}
}

void showhelp()
{
	printf("usage:\n");
	printf("taskset -c ./jitter [-l] [r] [-h] [-p $(pgrep perf)] [-t]\n");

	printf("	-r : Display update rate. Default is %d\n", displayUpdate);
	printf("	-l : Loop count for code block. Default is %d\n", loopcount);
	printf("	-p : perf_pid [run: perf record -S -C$CORENO -e intel_pt// -v on another window]\n");
#ifdef PROCESSOR_TRACE
	printf("	-t : jitter threshold for perf trigger. Default is > %d cycles\n", jitter_threshold);
#endif
	printf("	-i : Sample counts after which program terminates. Default count is %u\n", iterations);
	printf("For resetting statistics use:  pkill -USR1 jitter\n");
	printf("For elevating the priority of this program try:  chrt -r -p 99 processId\n");

}

void displayInfo()
{
	printf("Timings are in CPU Core cycles\n");
	printf("Inst_Min:    Minimum Excution time during the display update interval(default is ~1 second)\n");
	printf("Inst_Max:    Maximum Excution time during the display update interval(default is ~1 second)\n");
	printf("Inst_jitter: Jitter in the Excution time during rhe display update interval. This is the value of interest\n");
	printf("last_Exec:   The Excution time of last iteration just before the display update\n");
	printf("Abs_Min:     Absolute Minimum Excution time since the program started or statistics were reset\n");
	printf("Abs_Max:     Absolute Maximum Excution time since the program started or statistics were reset\n");
	printf("tmp:         Cumulative value calcualted by the dummy function\n");
	printf("Interval:    Time interval between the display updates in Core Cycles\n");
	printf("Sample No:   Sample number\n\n");
}

int main(int argc, char* argv[])
{

	register unsigned int tref=0;
	register unsigned int SampleNo=0;
	register long currentExectime;
	uint64_t startTime,endTime,seedtime, lasttime;
	uint64_t absoluteMin=MINVAL, absoluteMax=0, TransientMax=0, TransientMin=MINVAL;
	unsigned int tmp=0;
	int userinput;
#ifdef PROCESSOR_TRACE
	pid_t perf_pid = -1;
	int skip = 5, signaled = 0;
#endif

	printf("Linux Jitter testing program version 1.8\n");

	while ((userinput = getopt(argc, argv, "l:r:hp:t:i:")) != EOF)
	{
		switch(userinput)
		{
			case 'h' : showhelp();
				   exit(0);
			case 'l' : loopcount = strtoul(optarg, (char**)NULL, 0);
				   break;
			case 'r' : displayUpdate = strtoul(optarg, (char**)NULL, 0);
				   break;
			case 'p' :
#ifdef PROCESSOR_TRACE
				   perf_pid = strtol(optarg, (char**)NULL, 0);
				   printf("Perf PID=%d\n", perf_pid);
#else
				   printf("jitter program did not compile with -DPROCESSOR_TRACE, cannot use Linux perf with Intel Processor Trace to record jitter source\n");
#endif
				   break;

			case 'i' :
				   iterations = strtoul(optarg, (char**)NULL, 0);
				   printf("Iterations=%u\n", iterations);
				   break;

			case 't' :
#ifdef PROCESSOR_TRACE
				   jitter_threshold = strtoul(optarg, (char**)NULL, 0);
				   printf("Update jitter threshold to %u\n", jitter_threshold);
#else
				   printf("jitter program did not compile with -DPROCESSOR_TRACE, cannot use Linux perf with Intel Processor Trace to record jitter source\n");
#endif
				   break;
		}
	}

	printf("The pragram will execute a dummy function %u times\n",loopcount);
	printf("Display is updated every %u displayUpdate intervals\n",displayUpdate);

	displayInfo();

	// register signal handler for resetting Absoulte statistics
	if (signal(SIGUSR1, signalHandler) == SIG_ERR)
	{
		printf("**** Error: Signal Handler is somehow not registered. Sorry, you cannot reset statistics using USR1 signal\n");

	}

	printf("%s",title);

	seedtime = TimeStampCounter();	// get a random value from timestamp

	// prime the cache by executing the code a few times
	for(SampleNo =0;SampleNo < 1000; SampleNo++)
	{
		tmp += dummyop(loopcount, (uint32_t) seedtime);
	}

	SampleNo = 0;
	tref =0;

	do	// run the loop for a long time
	{

		startTime = TimeStampCounter();	// timestamp start
		tmp += dummyop(loopcount, (uint32_t) seedtime);	// run the compute function 'loopcount' times
		endTime = TimeStampCounterEnd();	// timestamp end

		currentExectime = clockdiff(startTime, endTime); // calculate differnce in clocks

		// calcualte absolute min and max
		if (currentExectime < absoluteMin)		// calculate absolute min
			absoluteMin = currentExectime;
		if (currentExectime>= absoluteMax)        // calculate absolute max
			absoluteMax = currentExectime;

		// calcualte Max execution time within the Display displayUpdate interval
		if (currentExectime < TransientMin)
			TransientMin = currentExectime;          // temporary  min
		if (currentExectime >= TransientMax)
			TransientMax = currentExectime;          // temporary max

#ifdef PROCESSOR_TRACE
		if ((perf_pid != -1) && (SampleNo > skip) && !(signaled) && (currentExectime - TransientMin) > jitter_threshold) {
			kill(perf_pid, SIGUSR2);
			signaled = 1;
		}
#endif

		if(tref++ >= displayUpdate) // update the stats on screen/memory at displayUpdate interval
		{
			tref=1;
			if(SampleNo > 0)
				printf("%10lu %10lu %10lu %10lu %10lu %10lu %13u %10lu %10u\n",
						TransientMin,
						TransientMax,
						TransientMax-TransientMin,
						currentExectime, absoluteMin,
						absoluteMax,tmp,
						(endTime-lasttime),
						SampleNo);

			SampleNo++;
			lasttime = endTime;

			TransientMax = 0;		// reset TransientMax to zero
			TransientMin = MINVAL;

			if (resetStats !=0)		// the varibale is set if user issues 'pkill USR1 jitter' signal
			{
				absoluteMax = 0;
				absoluteMin = MINVAL;
				resetStats = 0;
				SampleNo = 1 ;
#ifdef PROCESSOR_TRACE
				signaled = 0;
#endif
			}


			if (!(SampleNo % 40))	// print the title every 40 lines
					printf("%s",title);

		}

	} while(SampleNo <= iterations);

}
