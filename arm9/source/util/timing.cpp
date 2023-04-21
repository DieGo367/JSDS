#include <map>
#include <nds/timers.h>



std::map<int, int> timers;
std::map<int, int> counters;
int timerUsage = 0;
int timerIds = 0;
int counterIds = 0;

void timingTick() {
	for (const auto &[id, timeout] : timers) {
		timers[id] = timers[id] - 1;
	}
	for (const auto &[id, tick] : counters) {
		counters[id] = counters[id] + 1;
	}
}
void timingUse() {
	if (timerUsage++ == 0) {
		timerStart(0, ClockDivider_1024, TIMER_FREQ_1024(1000), timingTick);
	}
}
void timingDone() {
	if (--timerUsage <= 0) {
		timerUsage = 0;
		timerStop(0);
	}
}
bool timingOn() {
	return timerUsage > 0;
}

int timerAdd(int ticks) {
	timers[++timerIds] = ticks;
	timingUse();
	return timerIds;
}
void timerSet(int id, int ticks) {
	timers[id] = ticks;
}
int timerGet(int id) {
	return timers.at(id);
}
void timerRemove(int id) {
	timers.erase(id);
	timingDone();
}


int counterAdd() {
	counters[++counterIds] = 0;
	timingUse();
	return counterIds;
}
int counterGet(int id) {
	return counters.at(id);
}
void counterRemove(int id) {
	counters.erase(id);
	timingDone();
}
