#include "xthreadx.hh"

extern thread_local HashMap <void *, DetailLockData, PrivateHeap> lockUsage;

int xthreadx::thread_create(pthread_t * tid, const pthread_attr_t * attr, threadFunction * fn, void * arg) {
    ThreadLocalStatus::addARunningThread();
    if(ThreadLocalStatus::fromSerialToParallel()) {
        Predictor::outsideCyclesStop();
        Predictor::outsideCountingEventsStop();
        Predictor::stopSerial();
        Predictor::outsideCountingEventsStart();
        Predictor::outsideCycleStart();
    }
//    thread_t * children = (thread_t *) MyMalloc::xthreadMalloc(sizeof(thread_t));
    thread_t * children = (thread_t *) MyMalloc::xthreadMalloc();
//    thread_t * children = (thread_t *) MyMalloc::malloc(sizeof(thread_t));
    children->thread = tid;
    children->startArg = arg;
    children->startRoutine = fn;


    int result = RealX::pthread_create(tid, attr, xthreadx::startThread, (void *)children);

//    if(result) {
//        fprintf(stderr, "error: pthread_create failed: %s\n", strerror(errno));
//    }

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

    ThreadLocalStatus::setStackStartAddress(&arg);
    ThreadLocalStatus::getARunningThreadIndex();

#ifdef OPEN_SAMPLING_FOR_ALLOCS
    ThreadLocalStatus::setRandomPeriodForAllocations(RANDOM_PERIOD_FOR_ALLOCS);
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

//    MyMalloc::initializeForThreadLocalXthreadMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::initializeForThreadLocalHashMemory(ThreadLocalStatus::runningThreadIndex);
#ifdef ENABLE_PRECISE_BLOWUP
    MyMalloc::initializeForThreadLocalShadowMemory(ThreadLocalStatus::runningThreadIndex);
#endif
//    MyMalloc::initializeForThreadLocalMemory();
    lockUsage.initialize(HashFuncs::hashAddr, HashFuncs::compareAddr, MAX_LOCK_NUM);

    Predictor::threadInit();

#ifdef OPEN_SAMPLING_EVENT
    initPMU();
//    setupSampling();
#endif

    Predictor::outsideCountingEventsStart();
    Predictor::outsideCycleStart();
    ProgramStatus::setProfilerInitializedTrue();

    result = current->startRoutine(current->startArg);
    threadExit();

    return result;
}

void xthreadx::threadExit() {

    Predictor::outsideCyclesStop();
    Predictor::outsideCountingEventsStop();
    Predictor::threadEnd();
#ifdef OPEN_SAMPLING_EVENT
    stopSampling();
#endif
#ifdef OPEN_COUNTING_EVENT
    stopCounting();
#endif

    GlobalStatus::globalize();

//    MyMalloc::finalizeForThreadLocalXthreadMemory(ThreadLocalStatus::runningThreadIndex);
    MyMalloc::finalizeForThreadLocalHashMemory(ThreadLocalStatus::runningThreadIndex);
#ifdef ENABLE_PRECISE_BLOWUP
    MyMalloc::finalizeForThreadLocalShadowMemory(ThreadLocalStatus::runningThreadIndex);
#endif
//    MyMalloc::finalizeForThreadLocalMemory();
}

int xthreadx::thread_join(pthread_t thread, void ** retval) {
    int result = RealX::pthread_join (thread, retval);
    ThreadLocalStatus::subARunningThread();
    if(ThreadLocalStatus::fromParallelToSerial()) {
        Predictor::outsideCyclesStop();
        Predictor::outsideCountingEventsStop();
        Predictor::threadEnd();
        Predictor::stopParallel();
        Predictor::outsideCountingEventsStart();
        Predictor::outsideCycleStart();
    }
    return result;
}