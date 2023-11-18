// Includes:
#include <mutex>
#include <condition_variable>

class semaphore
{
	public:
		// Constructor
		semaphore(int _count = 0) : count(_count)
		{
		}

		void wait()
		{
			// Set up a lock on a mutex (start locked)
			std::unique_lock<std::mutex> lck(mtx);

			// While the count is zero
			while (count == 0)
			{
				// If the count is zero, then we need to wait.
				// Release the mutex, and wait to be notified
				condvar.wait(lck);
			}

			// Once we've acquired the lock (and thus we must be
			// greater than zero), decrement the count
			count--;

			// Function exit unlocks the mutex
			return;
		}

		void notify()
		{
			// Set up a lock on the mutex (start locked)
			std::unique_lock<std::mutex> lock(mtx);

			// Increment the count (we're the only thread who can
			// modify this currently)
			count++;

			// (Possibly) Notify another thread that's running wait()
			condvar.notify_one();

			// Function exit unlocks the mutex
			return;
		}

	private:
		// Mutex used to lock the count
		std::mutex mtx;

		// Condition variable
		std::condition_variable condvar;

		// The current mutex counter
		int count;
};
