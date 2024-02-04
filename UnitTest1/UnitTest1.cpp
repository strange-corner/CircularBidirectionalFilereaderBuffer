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
			MyTest(testListenerSimple);
		}

	private:

		void MyTest(CircularBidirectionalFilereaderBuffer<int, 1024>::IBackgroundTaskListener &testListener) {

			const  size_t N_ELEMENTS_IN_TESTFILE{ 8192u };
			const  size_t CACHE_LEN{ 1024u };
			FILE* f{ nullptr };
			errno_t err = fopen_s(&f, "testfile.bin", "rb");
			Assert::IsNotNull(f);
			Assert::AreEqual(NULL, err);
			typedef CircularBidirectionalFilereaderBuffer<int, CACHE_LEN> Testee_t;
			Testee_t testee{ f, testListener };
			testListener.initialize(&testee);
			Assert::AreEqual<size_t>(512u, testee.top(), L"ungleich");
			int value;
			testee.getCurrent(value);
			Assert::AreEqual<int>(0, value);
			int newValue;
			Testee_t::CacheState_t state = Testee_t::CacheState_t::OK;
			while (state == Testee_t::CacheState_t::OK) {
				state = testee.getNext(newValue);
				Assert::AreEqual<int>(value + 1, newValue);
				value = newValue;
				Assert::IsTrue(testee.fillLevelUp() <= CACHE_LEN);
			}
			Assert::AreEqual(Testee_t::CacheState_t::END_OF_FILE, state);
			Assert::AreEqual<unsigned int>(N_ELEMENTS_IN_TESTFILE - 1, value);
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, testee.top(), L"End of file");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, testee.bottom(), L"End of file");
			state = testee.getPrev(newValue);
			Assert::AreEqual(Testee_t::CacheState_t::OK, state);
			Assert::AreEqual<int>(newValue + 1, value);
			value = newValue;

			// Rückwärts eine halbe Cache-Länge lesen (-1 weil schon eins gelesen):
			for (unsigned int i = 0; i < CACHE_LEN / 2 - 2; i++) {
				state = testee.getPrev(newValue);
				Assert::AreEqual<int>(newValue + 1, value);
				value = newValue;
				Assert::IsTrue(testee.fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, testee.top());
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, testee.bottom());

			// Rückwärts ein weiteres Cache-Viertel lesen:
			for (unsigned int i = 0; i < CACHE_LEN / 4; i++) {
				state = testee.getPrev(newValue);
				Assert::AreEqual<int>(newValue + 1, value);
				value = newValue;
				Assert::IsTrue(testee.fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, testee.top(), L"Inzwischen wurde nicht gefüllt");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, testee.bottom(), L"Inzwischen wurde nicht gefüllt");
			// Das data_-Array enthält die <CACHE_LEN> höchsten Werte:
			// Beim nächsten sollte unten aufgefüllt werden
			state = testee.getPrev(newValue);
			Assert::AreEqual<size_t>(8192 - CACHE_LEN - CACHE_LEN / 4, testee.bottom(), L"bottom_ um einen Viertel runtergewandert");
			Assert::AreEqual<size_t>(8192 - CACHE_LEN / 4, testee.top());
			Assert::AreEqual<int>(newValue + 1, value);
			value = newValue;
			// Jetzt sollte das oberste Viertel des Caches mit den nächsten Daten gefüllt worden sein:
			// Rest abfragen
			while (state == Testee_t::CacheState_t::OK) {
				state = testee.getPrev(newValue);
				Assert::AreEqual<int>(value - 1, newValue);
				value = newValue;
				Assert::IsTrue(testee.fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(CACHE_LEN, testee.top(), L"");
			Assert::AreEqual<size_t>(0, testee.bottom(), L"");

			fclose(f);

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
				keepRunning = false;
				cv.notify_one();
				thread_.join();
			}

			virtual void requestFill(bool up) override {
				fillRequestDirectionUp_ = up;
				cv.notify_one();
			}

			bool initialize(CircularBidirectionalFilereaderBuffer<int, 1024>* testee) {
				p_testee_ = testee;
				std::this_thread::yield();
				return true;
			}

		private:

			void run() {
				while (keepRunning) {
					{
						//std::unique_lock lock{ p_testee_->getLock() };
						//cv.wait(lock);
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
			std::thread thread_{ &ThreadedTestListener::run, this };
			std::condition_variable cv;
			bool keepRunning{ true };
			bool fillRequestDirectionUp_{ false };

		};

	};


}
