#ifndef SYNCPERFSOURCE_MUTEX_H
#define SYNCPERFSOURCE_MUTEX_H

extern "C" {
    int _my_pthread_mutex_lock (pthread_mutex_t *mutex);
    int my_pthread_mutex_trylock (pthread_mutex_t *mutex);
    int my_pthread_mutex_unlock ( pthread_mutex_t *mutex );
};

#endif //SYNCPERFSOURCE_MUTEX_H
