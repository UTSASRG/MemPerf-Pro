#include "xthreadx.hh"

extern thread_local HashMap <void *, DetailLockData, nolock, PrivateHeap> lockUsage;

int xthreadx::thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
    thread_t * children = (thread_t *) MyMalloc::malloc(sizeof(thread_t));
    children->thread = tid;
    children->startArg = arg;
    children->startRoutine = fn;

    int result = RealX::pthread_create(tid, attr, xthreadx::startThread, (void *)children);

    if(result) {
        fprintf(stderr, "error: pthread_create failed: %s\n", strerror(errno));
    }
    return result;
}

void * xthreadx::startThread(void * arg) {
    void * result = nullptr;
    thread_t * current = (thread_t *) arg;
    pthread_attr_t attrs;
    if(pthread_getattr_np(pthread_self(), &attrs) != 0) {
        printf("error: unable to get thread attributes: %s\n", strerror(errno));
        abort();
    }


#ifndef NO_PMU
    initPMU();
#endif
//
    ThreadLocalStatus::addARunningThread();
    ThreadLocalStatus::getARunningThreadIndex();

    ///CPU Binding
//    cpu_set_t mask;
//    CPU_ZERO(&mask);
//    CPU_SET(ThreadLocalStatus::runningThreadIndex%40, &mask);
//    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
//    {
//        fprintf(stderr, "warning: could not set CPU affinity\n");
//        abort();
//    }


    MyMalloc::initializeForMMAPHashMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::initializeForThreadLocalMemory();
    lockUsage.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_LOCK_NUM);
    ProgramStatus::setProfilerInitializedTrue();
    result = current->startRoutine(current->startArg);
    threadExit();

    return result;
}

void xthreadx::threadExit() {

    ThreadLocalStatus::threadIsStopping = true;

#ifndef NO_PMU
    stopSampling();
    stopCounting();
#endif
    GlobalStatus::globalize();

    ThreadLocalStatus::subARunningThread();
    MyMalloc::finalizeForMMAPHashMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::finalizeForThreadLocalMemory();
}