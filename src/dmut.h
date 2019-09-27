#ifndef DMUT_H
#define DMUT_H

#include <atomic>
#include <memory>
#include <mutex>

#ifdef WIN32
	#define ann_lock(expr) _Acquires_lock_(expr)
	#define ann_unlock(expr) _Releases_lock_(expr)
#else
	#define ann_lock(expr)
	#define ann_unlock(expr)
#endif

enum LOCK_TYPE { WRITER_LOCK, READER_LOCK, NO_LOCK };

template <typename T>
class dlock;

/**
 * \brief	Data Oriented Mutex, The mutex holds the data and ensures
 *			mutual exclusion in accessing it as apposed to std::mutex
 *			which ensures mutual exclusion of code segments.
 * \tparam T The type of data that the mutex guards.
 */
template <typename T>
class dmut
{
    T value;
    std::atomic_int reader_count;
	
	// write mutex, ensures that when write access is needed only one thread
	// can hold a write lock and no read lock can be held.
    std::mutex mut_w;
	
	// read mutex, ensures that two read locks are not acquired concurrently
	// in order to keep the reader_count thread safe.
    std::mutex mut_r;

	friend dlock<const T>;
	friend dlock<T>;

	
    /**
	 * \brief	callback for releasing locks on this dmut.
	 *			should be called whenever a lock expires.
	 * \param type the type of the expiring lock.
	 */
	void ann_unlock(this->mut_w) on_release(const LOCK_TYPE type)
	{
		if (type == WRITER_LOCK) this->mut_w.unlock();

		if (type == READER_LOCK)
		{
			this->mut_r.lock();
			--reader_count;
			if (reader_count == 0) this->mut_w.unlock();
			this->mut_r.unlock();
		}
	}

public:

    explicit dmut(T&& value) : reader_count(0) { this->value = std::move(value); }
    explicit dmut(T& value) : reader_count(0) { this->value = std::move(value); }
    dmut() = default;
    dmut(const dmut& other) = delete;

	/**
	 * \brief	constructs a dmut by moving some dmute into a new one.
	 *			note: this constructor requires a writers lock on the
	 *			dmut being moved.
	 * \param	other the dmut being moved.
	 */
    dmut(dmut&& other) noexcept
    {
        this->mut_w.lock();
        other.mut_w.lock();
        this->value = std::move(other.value);
        this->reader_count = 0;
        other.mut_w.unlock();
        this->mut_w.lock();
    }

    ~dmut() 
    {
        this->mut_w.lock();
        this->mut_w.unlock();
    }

	dmut& operator=(const dmut& other) = delete;

    /**
	 * \brief	moves some dmute into this one.
	 *			note: this operation requires a writers lock on both dmuts,
	 *			thus the method may block for a long time if both dmuts are active.
	 * \param	other the dmut being moved into this one.
	 * \return a reference to this dmut.
	 */
	dmut& operator=(dmut&& other) noexcept
	{
		this->mut_w.lock();
		other.mut_w.lock();
		this->value = std::move(other.value);
		this->reader_count = 0;
		other.mut_w.unlock();
		this->mut_w.unlock();

		return *this;
	}

	
    /**
     * \brief	requests a writers lock on the data.
     *			if someone else is already holding some lock on this data
     *			this method will wait until the lock is available.
     *			for non blocking requests see 'try_lock'.
     * \return the lock on the data with ability to read and write to the underlying memory.
     * 
     */
    dlock<T> ann_lock(this->mut_w) lock()
    {
		this->mut_w.lock();
		return dlock<T>(&value, this, WRITER_LOCK);
	}
	
    /**
	 * \brief	requests a writers lock on the data.
	 *			if someone else is already holding some lock on this data
	 *			the lock will not be acquired and the method will return.
	 * \return a pair of bool and dlock, the bool represents whether or not
	 *			the lock was acquired and the dlock is the lock itself.
	 *			in case the lock is not acquired a dlock pointing to null will be returned.
	 */
	std::pair<bool, dlock<T>> ann_lock(this->mut_w) try_lock()
	{
		if (this->mut_w.try_lock)
			return std::make_pair(true, dlock<T>(&value, this, WRITER_LOCK));

		return std::make_pair(false, dlock<T>());
	}


    /**
	 * \brief	requests a readers lock on the data.
	 *			if someone else is holding a writers lock on the data
	 *			this method will wait until the lock is released.
	 *			for non blocking requests see 'try_peek'.
	 * \return the lock on the data as const, meaning the data can only be read.
	 */
	dlock<const T> ann_lock(this->mut_w) peek()
	{
		this->mut_r.lock();
		++reader_count;
		if (reader_count == 1) this->mut_w.lock();
		this->mut_r.unlock();
		return dlock<const T>(&value, this, READER_LOCK);
	}

    /**
	 * \brief	requests a readers lock on the data.
	 *			if someone else is holding a writers lock on the data
	 *			the lock will not be acquired and the method will return.
	 *			note: if someone else is attempting to acquire a readers lock
	 *			at the same time, the method might block for a small amount of time
	 *			while other threads are acquiring the lock.
	 * \return a pair of bool and dlock, the bool represents whether or not the lock
	 *			was acquired and the dlock is the lock itself.
	 *			in case the lock cannot be acquired a dlock pointing to null will re returned.
	 */
	std::pair<bool, dlock<T>> ann_lock(this->mut_w) try_peek()
	{
		//will unlock the readers mutex whenever the method returns.
		std::lock_guard<std::mutex>(this->mut_r);
		
		++reader_count;
		if (reader_count == 1)
		{
			if (this->mut_w.try_lock())
				return std::make_pair(true, dlock<const T>(&value, this, READER_LOCK));
			

			--reader_count;
			return std::make_pair(false, dlock<const T>());
		}
		
		return std::make_pair(true, dlock<const T>(&value, this, READER_LOCK));
	}
};

template <typename T, typename ...U>
dmut<T> make_dmut(U&& ...args)
{
	return dmut<T>(T(std::forward<U>(args)...));
}

/**
 * \brief	a lock for data of type T.
 *			this lock operates very similarly to a unique_ptr
 *			which is why it inherits it, when the lock is acquired
 *			it is capable of accessing the data just as a unique_ptr
 *			would, the only difference being when the lock is destroyed
 *			it notifies the mutex and the lock is released.
 *			in addition the methods release and reset cannot be called.
 *			
 * \tparam T The type of data the lock refers to.
 */
template <typename T>
class dlock : public std::unique_ptr<T>
{
	// when making a readers lock the type of the dlock should
	// be const T, but the type of the owner remains T,
	// remove_const to make mut_type always T .
	typedef typename std::remove_const<T>::type mut_type;
	
	LOCK_TYPE type;
	dmut<mut_type>* owner;

	void release() { std::unique_ptr<T>::release(); }
	void reset() { std::unique_ptr<T>::reset(); }

public:
	dlock(T* ptr, dmut<mut_type>* owner, LOCK_TYPE type) : std::unique_ptr<T>(ptr), type(type), owner(owner) {}
	dlock(dlock&& other) noexcept : std::unique_ptr<T>(other), type(other.type), owner(other.owner) {}
	dlock(const dlock& other) = delete;

	dlock& operator=(const dlock& other) = delete;
	dlock& operator=(dlock&& other) noexcept
	{
		*this = std::unique_ptr<T>(other);
		this->owner = other.owner;
		other.owner = nullptr;
		this->type = type;

		return *this;
	}

	~dlock()
	{
		// the unique_ptr is being released without being deleted because
		// the data is still held by the owner, and it is stack allocated
		// and as such will cause an exception if the unique_ptr destructor
		// will try to delete it.
		this->release();
		owner->on_release(type);
		this->owner = nullptr;
		this->type = NO_LOCK;
	}
};

#endif