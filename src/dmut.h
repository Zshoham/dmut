#ifndef DMUT_H
#define DMUT_H

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#ifdef WIN32
	#define ann_lock(expr)		_Acquires_lock_(expr)
	#define ann_unlock(expr)	_Releases_lock_(expr)
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
 *			this mutex behaves more like a std::shared_mutex since
 *			a readers lock (shared lock) can be acquired as well as
 *			a writers lock (exclusive lock).
 *
 *			*	to acquire a writers lock use the lock() method,
 *				use try_lock() if you wish for the method to return
 *				upon failure.
 *
 *			*	to acquire a readers lock use the peek() method,
 *				use try_peek() if you wish for the method to return
 *				upon failure.
 *
 *			both methods return a dlock objects which acts as a unique_ptr
 *			to the data, once the dlock is destroyed
 *			(by going out of scope or using the unlock() method)
 *			the lock is released and can be acquired by others.
 *			
 * \tparam T The type of data that the mutex guards.
 */
template <typename T>
class dmut
{
	template <typename V>
	struct base_mut_data
	{
		V *ptr_data;
		std::atomic_int reader_count;

		explicit base_mut_data(V* ptr) : ptr_data(ptr), reader_count(0) {}
		base_mut_data() : ptr_data(nullptr), reader_count(0) {}
		base_mut_data(const base_mut_data& other) = delete;
		base_mut_data(base_mut_data&& other) = delete;
		virtual ~base_mut_data() = default;

		base_mut_data& operator=(const base_mut_data& other) = delete;
		base_mut_data& operator=(base_mut_data&& other) = delete;

		virtual void clean() noexcept { delete this->ptr_data; }
	};

	template <typename V>
	struct mut_val_data : base_mut_data<V>
	{
		V data;

		explicit mut_val_data(V&& data) : base_mut_data<V>(&this->data), data(data) {}
		mut_val_data(const mut_val_data& other) = delete;
		mut_val_data(mut_val_data&& other) = delete;
		virtual ~mut_val_data() = default;

		mut_val_data& operator=(const mut_val_data& other) = delete;
		mut_val_data& operator=(mut_val_data&& other) = delete;
		
		void clean() noexcept override {}
	};

	base_mut_data<T> *data;
	
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
	 * \param	type the type of the expiring lock.
	 */
	void ann_unlock(this->mut_w) on_release(const LOCK_TYPE type) 
	{
		if (type == WRITER_LOCK) this->mut_w.unlock();
		

		if (type == READER_LOCK)
		{
			//will unlock at the end of the if statement.
			std::lock_guard<std::mutex>(this->mut_r);
			
			--data->reader_count;
			if (data->reader_count == 0) this->mut_w.unlock();
		}
	}

public:

	explicit dmut(T&& value) : data(new mut_val_data<T>(std::move(value))) {}
    explicit dmut(T& value) : data(new mut_val_data<T>(std::move(value))) {}
	explicit dmut(T *value_ptr) : data(new base_mut_data<T>(value_ptr)) {}
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
		//both mutexes will unlock when the methods returns.
		std::lock_guard<std::mutex>(this->mut_w);
		std::lock_guard<std::mutex>(other.mut_w);

		this->data = other.data;
		other.data = nullptr;
    }

    ~dmut() 
    {
    	// this will ensure that the mutex cannot be destroyed while
    	// someone holds a lock on its data.
		std::lock_guard<std::mutex>(this->mut_w);
		data->clean();
		delete data;
    }

	dmut& operator=(const dmut& other) = delete;

    /**
	 * \brief	moves some dmute into this one.
	 *			note: this operation requires a writers lock on both dmuts,
	 *			thus the method may block for a long time if both dmuts are active.
	 * \param	other the dmut being moved into this one.
	 * \return	a reference to this dmut.
	 */
	dmut& operator=(dmut&& other) noexcept
	{
		//both mutexes will unlock when the methods returns.
		std::lock_guard<std::mutex>(this->mut_w);
		std::lock_guard<std::mutex>(other.mut_w);
		
		this->data = other.data;
		other.data = nullptr;
		
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
		return dlock<T>(data->ptr_data, this, WRITER_LOCK);
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
			return std::make_pair(true, dlock<T>(data->ptr_data, this, WRITER_LOCK));

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
		//will unlock the readers mutex whenever the method returns.
		std::lock_guard<std::mutex>(this->mut_r);
		
		++data->reader_count;
		if (data->reader_count == 1) this->mut_w.lock();
		return dlock<const T>(data->ptr_data, this, READER_LOCK);
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
		
		++data->reader_count;
		if (data->reader_count == 1)
		{
			if (this->mut_w.try_lock())
				return std::make_pair(true, dlock<const T>(data->ptr_data, this, READER_LOCK));
			

			--data->reader_count;
			return std::make_pair(false, dlock<const T>());
		}
		
		return std::make_pair(true, dlock<const T>(data->ptr_data, this, READER_LOCK));
	}
};

/**
 * \brief Creates a dmut, allocating the data it guards on the stack.
 * \tparam T the type of data the mutex should guard.
 * \tparam U the parameter types required to construct the object.
 * \param args the parameters required to construct the object.
 * \return a dmut containing the newly constructed object of type T.
 */
template <typename T, typename ...U>
dmut<T> make_dmut(U&& ...args)
{
	return dmut<T>(T(std::forward<U>(args)...));
}

/**
 * \brief Creates a dmut, allocating the data it guards on the heap.
 * \tparam T the type of data the mutex should guard.
 * \tparam U the parameter types required to construct the object.
 * \param args the parameters required to construct the object.
 * \return a dmut containing the newly constructed object of type T.
 */
template<typename T, typename ...U>
dmut<T> new_dmut(U&& ...args)
{
	return dmut<T>(new T(std::forward<U>(args)...));
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
class dlock : std::unique_ptr<T>
{
	// when making a readers lock the type of the dlock should
	// be const T, but the type of the owner remains T,
	// remove_const to make mut_type always T.
	typedef typename std::remove_const<T>::type mut_type;
	
	LOCK_TYPE type;
	dmut<mut_type> *owner;

public:
	dlock(T *ptr, dmut<mut_type> *owner, LOCK_TYPE type) : std::unique_ptr<T>(ptr), type(type), owner(owner) {}
	dlock(dlock&& other) noexcept : std::unique_ptr<T>(std::move(other)), type(other.type), owner(other.owner) {}
	dlock(const dlock& other) = delete;

	dlock& operator=(const dlock& other) = delete;
	dlock& operator=(dlock&& other) noexcept
	{
		std::unique_ptr<T>::operator=(std::move(other));
		this->owner = other.owner;
		other.owner = nullptr;
		this->type = type;
		other.type = NO_LOCK;

		return *this;
	}

	~dlock() { unlock(); }
	
	T& operator*() const { return std::unique_ptr<T>::operator*(); }
	T* operator->() const noexcept { return std::unique_ptr<T>::operator->(); }

	/**
	 * \brief	releases the lock to the data rendering this object useless,
	 *			has a similar effect to std::unique_ptr::reset() or setting a
	 *			pointer to nullptr.
	 */
	void unlock() noexcept
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