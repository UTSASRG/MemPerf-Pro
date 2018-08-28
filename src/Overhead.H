#define relaxed std::memory_order_relaxed

class Overhead {

	std::atomic_size_t metadata;
	std::atomic_size_t blowup;
	std::atomic_size_t alignment;

	public:
	
	Overhead (): metadata(0), blowup(0), alignment(0) {};

	size_t getMetadata() {
		size_t m = metadata.load();
		return m;
	}
	size_t getBlowup() {
		size_t b = blowup.load();
		return b;
	}
	size_t getAlignment() {
		size_t a = alignment.load();
		return a;
	}
	void setMetadata(size_t size) {metadata = size;}
	void setBlowup(size_t size) {blowup = size;}
	void setAlignment(size_t size) {alignment = size;}
	void addMetadata(size_t size) {metadata.fetch_add(size, relaxed);}
	void addBlowup(size_t size) {blowup.fetch_add(size, relaxed);}
	void addAlignment(size_t size) {alignment.fetch_add(size, relaxed);}
};
