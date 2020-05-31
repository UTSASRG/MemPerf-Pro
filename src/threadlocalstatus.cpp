//
// Created by 86152 on 2020/5/23.
//

#include <stdio.h>
#include "threadlocalstatus.h"

void ThreadLocalStatus::getARunningThreadIndex() {
    lock.lock();
    runningThreadIndex = totalNumOfRunningThread++;
    lock.unlock();
}