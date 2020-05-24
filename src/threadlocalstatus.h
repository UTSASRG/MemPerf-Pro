//
// Created by 86152 on 2020/5/23.
//

#ifndef MMPROF_THREADLOCALSTATUS_H
#define MMPROF_THREADLOCALSTATUS_H

class ThreadLocalStatus {
private:
public:
    static thread_local unsigned int runningThreadIndex;
};

#endif //MMPROF_THREADLOCALSTATUS_H
