#include <vector>
#include <thread>
#include <pthread.h>

#define NUM_ITERATIONS 100

pthread_barrier_t barrier;
pthread_mutex_t lock;
pthread_cond_t cond;

std::vector<void*> v;
int t = 1;

void t1 ();
void t2 ();

int main() {

	pthread_cond_init (&cond, NULL);
	pthread_mutex_init (&lock, NULL);

	std::thread thread1 (t1);
	std::thread thread2 (t2);

	thread1.join();
	thread2.join();

	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&lock);

//	malloc_stats ();
	return EXIT_SUCCESS;
}

void t1 () {

	void* object;
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		pthread_mutex_lock (&lock);
		while (t != 1) pthread_cond_wait (&cond, &lock);

		object = malloc (1000);
		v.push_back (object);

		t = 2;
		pthread_cond_broadcast (&cond);
		pthread_mutex_unlock (&lock);
	}
}

void t2 () {

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		pthread_mutex_lock (&lock);

		while (t != 2) pthread_cond_wait (&cond, &lock);

		free (v.back());
		v.pop_back();
		t = 1;
		pthread_cond_broadcast (&cond);
		pthread_mutex_unlock(&lock);
	}
}
