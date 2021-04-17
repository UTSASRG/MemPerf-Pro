#include "xthreadx.hh"

extern thread_local HashMap <void *, DetailLockData, PrivateHeap> lockUsage;
pthread_t * xthreadx::threads[MAX_THREAD_NUMBER];

int xthreadx::thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
    ThreadLocalStatus::addARunningThread();

#ifdef PREDICTION
    if(ThreadLocalStatus::fromSerialToParallel()) {
        Predictor::outsideCyclesStop();
        Predictor::stopSerial();
        Predictor::outsideCycleStart();
    }
#endif

    thread_t * children = (thread_t *) MyMalloc::xthreadMalloc();
    children->thread = tid;
    children->startArg = arg;
    children->startRoutine = fn;


    int result = RealX::pthread_create(tid, attr, xthreadx::startThread, (void *)children);

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

    ThreadLocalStatus::getARunningThreadIndex();
    threads[ThreadLocalStatus::runningThreadIndex] = current->thread;


#ifdef OPEN_SAMPLING_FOR_ALLOCS
    ThreadLocalStatus::setRandomPeriodForAllocations();
#endif

#ifdef OPEN_CPU_BINDING
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(ThreadLocalStatus::runningThreadIndex%40, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        fprintf(stderr, "warning: could not set CPU affinity\n");
        abort();
    }
#endif

    MyMalloc::initializeForThreadLocalHashMemory(ThreadLocalStatus::runningThreadIndex);
    lockUsage.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_LOCK_NUM);

#ifdef PREDICTION
    Predictor::threadInit();
#endif


#ifdef OPEN_SAMPLING_EVENT
        initPMU2();

#endif

#ifdef PREDICTION
    Predictor::outsideCycleStart();
#endif

//    ProgramStatus::setProfilerInitializedTrue();
    ProgramStatus::profilerInitialized = true;

    pthread_cleanup_push(threadExit, nullptr);

    fprintf(stderr, "tid %d start\n", ThreadLocalStatus::runningThreadIndex);
    result = current->startRoutine(current->startArg);

    pthread_cleanup_pop(1);
//    threadExit(nullptr);
    return result;
}

//bool lastThreadDepended;

void xthreadx::threadExit(void * arg) {

fprintf(stderr, "thread %d clean\n", ThreadLocalStatus::runningThreadIndex);

threads[ThreadLocalStatus::runningThreadIndex] = nullptr;

#ifdef PREDICTION
    Predictor::outsideCyclesStop();
    Predictor::threadEnd();
#endif

#ifdef OPEN_SAMPLING_EVENT
    stopSampling();
#endif

    GlobalStatus::globalize();
    MyMalloc::finalizeForThreadLocalHashMemory(ThreadLocalStatus::runningThreadIndex);
}

int xthreadx::thread_join(pthread_t thread, void ** retval) {
    int result = RealX::pthread_join (thread, retval);
    ThreadLocalStatus::subARunningThread();

#ifdef PREDICTION
        if(ThreadLocalStatus::fromParallelToSerial()) {
            Predictor::outsideCyclesStop();
            Predictor::threadEnd();
            Predictor::stopParallel();
            Predictor::outsideCycleStart();
        }
#endif

    return result;

}
