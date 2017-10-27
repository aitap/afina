#include <thread>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

std::string afina_request(struct sockaddr & addr, const void * buf, size_t size, pthread_barrier_t * barrier) {
	int s = socket(PF_INET, SOCK_STREAM, 0);
	if (s == -1) return "";
	int b_res = pthread_barrier_wait(barrier);
	if (b_res != 0 && b_res != PTHREAD_BARRIER_SERIAL_THREAD) {
		close(s);
		return "<barrier failed>";
	}
	if (connect(s, addr, sizeof(addr))) {
		close(s);
		return "<failed to connect>";
	}
	ssize_t offset = 0;
	// TODO: write loop
	// TODO: read loop
}

const size_t num_threads = 1000;

int main (int argc, char** argv) {
	pthread_barrier_t barrier;
	if (pthread_barrier_init(&barrier, nullptr, num_threads))
		return 1;
	// TODO: prepare the sockaddr, buf and spawn `num_threads` threads
}
