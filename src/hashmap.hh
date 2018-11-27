#if !defined(DOUBLETAKE_HASHMAP_H)
#define DOUBLETAKE_HASHMAP_H

/*
 * @file   hashtable.h
 * @brief  Management about hash table.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 *         The original design is from kulesh [squiggly] isis.poly.edu
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <atomic>

#include "list.hh"
#include "spinlock.hh"

void* myMalloc (size_t size);
void myFree();
bool debug_hashmap = false;
bool d_initialize = false;

template <class KeyType,	// What is the key? A long or string
			class ValueType,	// What is the value there?
			class LockType>	// Where to call malloc
			class HashMap {

	struct Bucket {
		list_t list;
		// Each bucket has a separate lock
		LockType lock;
		size_t count; // How many entries in this bucket

		void initialize() {
			count = 0;
			listInit(&list);
			LockInit();
		}

		void Lock() { lock.lock(); }
		void Unlock() { lock.unlock(); }
		void LockInit() { lock.init(); }
		void* getFirstEntry() { return (void*)list.next; }
	};

	struct Entry {
		list_t list;
		KeyType key;
		size_t keylen;
		ValueType value;

		void initialize(KeyType ikey = 0, int ikeylen = 0, ValueType ivalue = 0) {
			listInit(&list);
			key = ikey;
			keylen = ikeylen;
			value = ivalue;
		}

		void erase() { listRemoveNode(&list); }
		struct Entry* nextEntry() { return (struct Entry*)list.next; }
		ValueType getData() { return value; }
		KeyType getKey() { return key; }
	};

	bool _initialized;
	struct Bucket* _buckets;
	size_t _numBuckets;     // How many buckets in total
	size_t _bucketsUsed; // How many buckets in total

	typedef bool (*keycmpFuncPtr)(const KeyType, const KeyType, size_t);
	typedef size_t (*hashFuncPtr)(const KeyType, size_t);
	keycmpFuncPtr _keycmp;
	hashFuncPtr _hashfunc;

	public:
	HashMap() : _initialized(false) {}

	void initialize(hashFuncPtr hfunc, keycmpFuncPtr kcmp, const size_t size = 4096) {
		_buckets = NULL;
		_bucketsUsed = 0;
		_numBuckets = size;

		if(hfunc == NULL || kcmp == NULL) {abort();}

		// Initialize those functions.
		_hashfunc = hfunc;
		_keycmp = kcmp;

		// Allocated predefined size.
		_buckets = (struct Bucket*) myMalloc (size * sizeof(struct Bucket));

		// Initialize all of these _buckets.
		struct Bucket* entry;
		for(size_t i = 0; i < size; i++) {
			entry = getBucket(i);
			entry->initialize();
		}
		_initialized = true;
	}

	inline struct Bucket* getBucket(size_t index) {
		if(index < _numBuckets) {
			return &_buckets[index];
		} else {
			return NULL;
		}
	}

	inline size_t hashIndex(const KeyType& key, size_t keylen) {
		size_t hkey = _hashfunc(key, keylen);
		return hkey % _numBuckets;
	}

	// Look up whether an entry is existing or not.
	// If existing, return true. *value should be carried specific value for this key.
	// Otherwise, return false.
	bool find(const KeyType& key, ValueType* value) {
		size_t keylen = sizeof(key);
		assert(_initialized == true);
		size_t hindex = hashIndex(key, keylen);
		struct Bucket* bucket = getBucket(hindex);

		bool isFound = false;
		bucket->Lock();
		struct Entry* entry = getEntry(bucket, key, keylen);

		if(entry) {
			*value = entry->value;
			isFound = true;
		}

		bucket->Unlock();
		return isFound;
	}

	void insert(const KeyType& key, ValueType value) {
		assert(_initialized == true);
		size_t keylen = sizeof(key);
		size_t hindex = hashIndex(key, keylen);
		struct Bucket* bucket = getBucket(hindex);
		bucket->Lock();
		insertEntry(bucket, key, keylen, value);
		bucket->Unlock();
	}

	// Insert a hash table entry if it is not existing.
	// If the entry is already existing, return true
	bool insertIfAbsent(const KeyType& key, ValueType value) {
		assert(_initialized == true);
		size_t keylen = sizeof(key);
		size_t hindex = hashIndex(key, keylen);
		struct Bucket* bucket = getBucket(hindex);
		struct Entry* entry;
		bool isFound = true;

		bucket->Lock();

		// Check all _buckets with the same hindex.
		entry = getEntry(bucket, key, keylen);
		if(!entry) {
			isFound = false;
			insertEntry(bucket, key, keylen, value);
		}

		bucket->Unlock();
		return isFound;
	}

	// Free an entry with specified
	bool erase(const KeyType& key) {
		assert(_initialized == true);
		size_t keylen = sizeof(key);
		size_t hindex = hashIndex(key, keylen);
		struct Bucket* bucket = getBucket(hindex);
		struct Entry* entry;
		bool isFound = false;

		bucket->Lock();

		entry = getEntry(bucket, key, keylen);

		if(entry) {
			isFound = true;

			// Check whether this entry is the bucket entry.
			// Remove this entry if existing.
			entry->erase();
			bucket->count--;
			myFree (entry);
		}

		bucket->Unlock();
		return isFound;
	}

	// Clear all _buckets
	void clear() {}

	private:
	// Create a new Entry with specified key and value.
	struct Entry* createNewEntry(const KeyType& key, size_t keylen, ValueType value) {
		struct Entry* entry = (struct Entry*)	myMalloc (sizeof(struct Entry));
//		fprintf (stderr, "sizeof(struct Entry) is %zu\n", sizeof(struct Entry));

		// Initialize this new entry.
		entry->initialize(key, keylen, value);
		return entry;
	}

	void insertEntry(struct Bucket* bucket, const KeyType& key, size_t keylen, ValueType value) {
		// Check whether the bucket entry is empty or not.
		// Create an entry
		struct Entry* entry = createNewEntry(key, keylen, value);
		listInsertTail(&entry->list, &bucket->list);
		bucket->count++;
		//printUtilization();
	}

	// Search the entry in the corresponding list.
	struct Entry* getEntry(struct Bucket* bucket, const KeyType& key, size_t keylen) {
		struct Entry* entry = (struct Entry*)bucket->getFirstEntry();
		struct Entry* result = NULL;

		// Check all entries with the same hindex.
		int count = bucket->count;
		while(count > 0) {

			if(entry->keylen == keylen && _keycmp(entry->key, key, keylen)) {
				result = entry;
				break;
			}

			entry = entry->nextEntry();
			count--;
		}

		return result;
	}

	public:
	class iterator {
		friend class HashMap<KeyType, ValueType, LockType>;
		struct Entry* _entry; // Which entry in the current hash entry?
		size_t _pos;          // which bucket at the moment? [0, nbucket-1]
		HashMap* _hashmap;

		public:
		iterator(struct Entry* ientry = NULL, int ipos = 0, HashMap* imap = NULL) {
//			cout << "In default constructor of iterator\n";
			_pos = ipos;
			_entry = ientry;
			_hashmap = imap;
		}

		~iterator() {}

		struct Entry operator*() { return *(this->_entry); }
		iterator& operator++() {
			(*this)++;
			return *this;
		}

		iterator& operator++(int) { // in postfix ++  /* parameter? */
			struct Bucket* hashentry = _hashmap->getBucket(_pos);

			// Check whether this entry is the last entry in current hash entry.
			if(!isListTail(&_entry->list, &hashentry->list)) {
				// If not, then we simply get next entry. No need to change pos.
				_entry = _entry->nextEntry();
			} else {
				// Since current list is empty, we must search next hash entry.
				_pos++;
				while((hashentry = _hashmap->getBucket(_pos)) != NULL) {
					if(hashentry->count != 0) {
						// Now we can return it.
						_entry = (struct Entry*)hashentry->getFirstEntry();
						return *this;
					}
					_pos++;
				}
				_entry = NULL;
			}

			return *this;
		}

		// iterator& operator -- ();
		// Iterpreted as a = b is treated as a.operator=(b)
		iterator& operator=(const iterator& that) {
			_entry = that._entry;
			_pos = that._pos;
			_hashmap = that._hashmap;
			return *this;
		}

		bool operator==(const iterator& that) const { return _entry == that._entry; }

		bool operator!=(const iterator& that) const { return _entry != that._entry; }

		ValueType getData() { return _entry->getData(); }
		KeyType getKey() { return _entry->getKey(); }
	};

	// Acquire the bucket entry of the hash table
	iterator begin() {
		size_t pos = 0;
		struct Bucket* head = NULL;
		struct Entry* entry;

		// Get the bucket non-null entry
		while(pos < _numBuckets) {
			head = getBucket(pos);
			if(head->count != 0) {
				// Now we can return it.
				entry = (struct Entry*)head->getFirstEntry();
				//entry->list.next);
				return iterator(entry, pos, this);
			}
			pos++;
		}
		return end();
	}

	iterator end() { return iterator(NULL, 0, this); }

	void printUtilization() {
		size_t pos = 0;
		struct Bucket* head = NULL;
		size_t numEntries = 0;
		size_t bucketsUsed = 0;
		size_t emptyBuckets = 0;
		size_t firstBucket = 0;

		while(pos < _numBuckets) {
			head = getBucket(pos);
			if(head->count == 0) {
				emptyBuckets++;
			} else {
				if(firstBucket == 0) {
					firstBucket = pos;
				}
				bucketsUsed++;
				numEntries += head->count;
			}
			//fprintf(stderr, "map %p -> bucket %04zu -> count = %zu\n", this, pos, head->count);
			//entry = (struct Entry*)head->getFirstEntry();
			//return iterator(entry, pos, this);
			pos++;
		}

		fprintf(stderr, "map %p -> bucketsUsed = %zu , firstBucket = %zu, emptyBuckets = %zu, total entries = %zu\n",
				this, bucketsUsed, firstBucket, emptyBuckets, numEntries);

	}
};
#endif
