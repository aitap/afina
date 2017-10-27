#include <ctype.h>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <future>
#include <pthread.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


const size_t read_size = 256;

std::string afina_request(sockaddr * addr, const char * buf, size_t size, pthread_barrier_t * barrier) {
	// wait for the barrier first, before we can return because of an error
	int b_res = pthread_barrier_wait(barrier);
	if (b_res != 0 && b_res != PTHREAD_BARRIER_SERIAL_THREAD) {
		return "<barrier failed>";
	}
	std::string ret;

	int s = socket(PF_INET, SOCK_STREAM, 0);
	if (s == -1)
		return "<socket failed>";

	// Flight of the Valkyries starts playing in the background
	// 1. connect to Afina
	if (connect(s, addr, sizeof(*addr))) {
		close(s);
		return "<failed to connect>";
	}

	// 2. write the request
	size_t offset = 0;
	while (offset < size) {
		int res = send(s, buf+offset, size-offset, MSG_NOSIGNAL);
		if (res < 0) {
			close(s);
			return "<write error>";
		}
		offset += res;
	}
	// 3. prevent deadlock - make sure Afina won't try to read any more commands
	shutdown(s, SHUT_WR);

	try {
		std::vector<char> read_buf;
		read_buf.resize(read_size);
		// 4. read the response
		size_t offset = 0;
		for (;;) {
			int res = recv(s, read_buf.data() + offset, read_buf.size() - offset, MSG_NOSIGNAL);
			if (res < 0) {
				close(s);
				return "<read error>";
			} else if (res == 0) { // EOF
				break;
			} else
				offset += res;
			if (offset == read_buf.size()) {
				read_buf.resize(read_buf.size()*2);
			}
		}
		ret.assign(read_buf.data(), offset);
	} catch (...) {
		close(s);
		return "<exception in read loop>";
	}
	close(s);
	return ret;
}

const size_t num_threads = 1000;

int main (int argc, char** argv) {
	pthread_barrier_t barrier;
	if (pthread_barrier_init(&barrier, nullptr, num_threads))
		return 1;
	struct sockaddr_in addr = {AF_INET, htons(8080), htonl(INADDR_LOOPBACK)};

	const std::string request {"set var 0 0 6\r\nfoobar\r\n"};

	std::vector<std::future<std::string>> results;
	for (size_t i = 0; i < num_threads; i++)
		results.push_back(std::move(std::async(std::launch::async, afina_request, (sockaddr*)&addr, request.data(), request.size(), &barrier)));

	std::unordered_map<std::string, size_t> statistics;
	for (auto & res : results)
		statistics[res.get()]++;

	for (auto it = statistics.begin(); it != statistics.end(); ++it) {
		std::cout << it->second << "\t";
		for (const char & c: it->first) {
			if (isprint(c))
				if (c == '\\')
					std::cout << "\\\\";
				else
					std::cout << c;
			else {
				std::cout << "\\x";
				auto w = std::cout.width(2);
				auto f = std::cout.fill('0');
				std::cout.setf(std::ios::hex, std::ios::basefield);
				std::cout << (int)(c);
				std::cout.unsetf(std::ios::hex);
				std::cout.width(w);
				std::cout.fill(f);
			}
		}
		std::cout << std::endl;
	}

	if (pthread_barrier_destroy(&barrier))
		return 1;
}
