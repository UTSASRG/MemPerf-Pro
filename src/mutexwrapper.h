#ifndef SYNCPERFSOURCE_MUTEX_H
#define SYNCPERFSOURCE_MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

    int _my_pthread_mutex_lock (pthread_mutex_t *mutex);
    int my_pthread_mutex_trylock (pthread_mutex_t *mutex);
    int my_pthread_mutex_unlock ( pthread_mutex_t *mutex );

    void allocatingStatusRecordALockContention();

#ifdef __cplusplus
}
#endif

#endif //SYNCPERFSOURCE_MUTEX_H
