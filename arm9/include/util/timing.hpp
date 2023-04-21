#ifndef JSDS_TIMING_HPP
#define JSDS_TIMING_HPP

bool timingOn();

int timerAdd(int ticks);
void timerSet(int id, int ticks);
int timerGet(int id);
void timerRemove(int id);

int counterAdd();
int counterGet(int id);
void counterRemove(int id);

#endif /* JSDS_TIMING_HPP */