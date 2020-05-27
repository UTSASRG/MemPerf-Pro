//
// Created by 86152 on 2020/5/23.
//

#include "threadlocalstatus.h"

void ThreadLocalStatus::getRunningThreadIndex() {
    lock.lock();
    runningThreadIndex = totalNumOfRunningThread++;
    lock.unlock();
}