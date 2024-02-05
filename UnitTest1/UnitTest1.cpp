#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "CircularBidirectionalFilereaderBuffer.hpp"

namespace Microsoft {
	namespace VisualStudio {
		namespace CppUnitTestFramework {

			/** @see https://stackoverflow.com/questions/60117597/compare-enum-types */
			std::wstring ToString(const enum CircularBidirectionalFilereaderBuffer<int, 1024>::CacheState_t &value)
			{
				switch (value) {
				case CircularBidirectionalFilereaderBuffer<int, 1024>::CacheState_t::OK: return L"OK";
				case CircularBidirectionalFilereaderBuffer<int, 1024>::CacheState_t::ALMOST_EMPTY: return L"ALMOST_EMPTY";
				case CircularBidirectionalFilereaderBuffer<int, 1024>::CacheState_t::END_OF_FILE: return L"END_OF_FILE";
				case CircularBidirectionalFilereaderBuffer<int, 1024>::CacheState_t::CACHE_OVERFLOW: return L"CACHE_OVERFLOW";
				}

				return std::to_wstring(static_cast<int>(value));
			}

		} // namespace CppUnitTestFramework 
	} // namespace VisualStudio
} // namespace Microsoft

namespace UnitTest1
{
	TEST_CLASS(UnitTest) {

		TEST_METHOD(Simple) {
			TestListener testListenerSimple;
			setup(testListenerSimple);
			MyTest();
			tearDown();
		}
		
		TEST_METHOD(Threaded) {
			ThreadedTestListener testListenerTh;
			setup(testListenerTh);
			MyTest();
			testListenerTh.tearDown();  // diesen Beenden bevor der Testee gelöscht wird.
			tearDown();
		}

	private:

		static const size_t N_ELEMENTS_IN_TESTFILE{ 8192u };
		static const size_t CACHE_LEN{ 1024u };
		typedef CircularBidirectionalFilereaderBuffer<int, CACHE_LEN> Testee_t;

		void setup(CircularBidirectionalFilereaderBuffer<int, 1024>::IBackgroundTaskListener& testListener) {
			errno_t err = fopen_s(&f, "testfile.bin", "rb");
			Assert::IsNotNull(f);
			Assert::AreEqual(NULL, err);
			p_testee_ = new Testee_t(f, testListener);
			testListener.initialize(p_testee_);
		}

		void tearDown() {
			fclose(f);
			delete p_testee_;
			p_testee_ = nullptr;
		}

		void MyTest() {
			using namespace std::chrono_literals;	
			Assert::AreEqual<size_t>(512u, p_testee_->top(), L"ungleich");
			int value;
			p_testee_->getCurrent(value);
			Assert::AreEqual<int>(0, value);
			int newValue;
			Testee_t::CacheState_t state = Testee_t::CacheState_t::OK;
			while (state == Testee_t::CacheState_t::OK) {
				state = p_testee_->getNext(newValue);
				Assert::AreEqual<int>(value + 1, newValue);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelUp() <= CACHE_LEN);
			}
			Assert::AreEqual(Testee_t::CacheState_t::END_OF_FILE, state);
			Assert::AreEqual<unsigned int>(N_ELEMENTS_IN_TESTFILE - 1, value);
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, p_testee_->top(), L"End of file");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, p_testee_->bottom(), L"End of file");
			state = p_testee_->getPrev(newValue);
			Assert::AreEqual(Testee_t::CacheState_t::OK, state);
			Assert::AreEqual<int>(newValue + 1, value);
			value = newValue;

			// Rückwärts eine halbe Cache-Länge lesen (-1 weil schon eins gelesen):
			for (unsigned int i = 0; i < CACHE_LEN / 2 - 2; i++) {
				state = p_testee_->getPrev(newValue);
				Assert::AreEqual<int>(newValue + 1, value);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, p_testee_->top());
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, p_testee_->bottom());

			// Rückwärts ein weiteres Cache-Viertel lesen:
			for (unsigned int i = 0; i < CACHE_LEN / 4; i++) {
				state = p_testee_->getPrev(newValue);
				Assert::AreEqual<int>(newValue + 1, value);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, p_testee_->top(), L"Inzwischen wurde nicht gefüllt");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, p_testee_->bottom(), L"Inzwischen wurde nicht gefüllt");
			// Das data_-Array enthält die <CACHE_LEN> höchsten Werte:
			// Beim nächsten sollte unten aufgefüllt werden
			state = p_testee_->getPrev(newValue);
			std::this_thread::sleep_for(10ms);  // TODO saubere Synchronisation
			Assert::AreEqual<size_t>(8192 - CACHE_LEN - CACHE_LEN / 4, p_testee_->bottom(), L"bottom_ um einen Viertel runtergewandert");
			Assert::AreEqual<size_t>(8192 - CACHE_LEN / 4, p_testee_->top());
			Assert::AreEqual<int>(newValue + 1, value);
			value = newValue;
			// Jetzt sollte das oberste Viertel des Caches mit den nächsten Daten gefüllt worden sein:
			// Rest abfragen
			while (state == Testee_t::CacheState_t::OK) {
				state = p_testee_->getPrev(newValue);
				Assert::AreEqual<int>(value - 1, newValue);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(CACHE_LEN, p_testee_->top(), L"");
			Assert::AreEqual<size_t>(0, p_testee_->bottom(), L"");
		}

		/**
		 * Einfacher Testlistener, der die nötigen Aufrufe direkt aus dem Listener macht. In echt müsste
		 * der die Aufrufe aus einem anderen Thread machen
		 */
		class TestListener : public CircularBidirectionalFilereaderBuffer<int, 1024>::IBackgroundTaskListener {
		public:

			TestListener() {}

			virtual ~TestListener() {}

			virtual void requestFill(bool up) override {
				if (up) {
					p_testee_->fillUpwards();
				}
				else {
					p_testee_->fillDownwards();
				}
			}

			bool initialize(CircularBidirectionalFilereaderBuffer<int, 1024>* t) {
				p_testee_ = t;
				return true;
			}

		private:
			
			CircularBidirectionalFilereaderBuffer<int, 1024>* p_testee_{nullptr};
		};

			/**
			 * Threaded TestListener
			 */
			class ThreadedTestListener : public CircularBidirectionalFilereaderBuffer<int, 1024>::IBackgroundTaskListener {
			public:

				ThreadedTestListener() {}

				virtual ~ThreadedTestListener() {
					delete thread_;
				}

				virtual void requestFill(bool up) override {
					fillRequestDirectionUp_ = up;
					cv.notify_one();
				}

				bool initialize(CircularBidirectionalFilereaderBuffer<int, 1024>* testee) {
					p_testee_ = testee;
					thread_ = new std::thread(&ThreadedTestListener::run, this);
					std::this_thread::yield();
					return true;
				}

				void tearDown() {
					keepRunning = false;
					cv.notify_one();
					thread_->join();
				}

			private:

				void run() {
					std::mutex mutex;
					while (keepRunning) {
						{
							std::unique_lock<std::mutex> lock{mutex};
							cv.wait(lock);
							if (fillRequestDirectionUp_) {
								p_testee_->fillUpwards();
							}
							else {
								p_testee_->fillDownwards();
							}
						}
					}
				}

				CircularBidirectionalFilereaderBuffer<int, 1024>* p_testee_{ nullptr };
				std::thread* thread_;
				std::condition_variable cv;
				bool keepRunning{ true };
				bool fillRequestDirectionUp_{ false };

			};

			FILE* f{ nullptr };
			Testee_t *p_testee_{ nullptr };

	};


}
