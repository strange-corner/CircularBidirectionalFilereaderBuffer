#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "CircularBidirectionalFilereaderBuffer.hpp"

static const size_t CACHE_LEN{ 1024u };
/** testfile.bin muss Werte dieses Typs in aufsteigender Reihenfolge enthalten */
typedef int TYPE_OF_DATA;

namespace Microsoft {
	namespace VisualStudio {
		namespace CppUnitTestFramework {

			/** @see https://stackoverflow.com/questions/60117597/compare-enum-types */
			std::wstring ToString(const enum CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::CacheState_t &value)
			{
				switch (value) {
				case CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::CacheState_t::OK: return L"OK";
				case CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::CacheState_t::ALMOST_EMPTY: return L"ALMOST_EMPTY";
				case CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::CacheState_t::END_OF_FILE: return L"END_OF_FILE";
				case CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::CacheState_t::CACHE_OVERFLOW: return L"CACHE_OVERFLOW";
				}

				return std::to_wstring(static_cast<TYPE_OF_DATA>(value));
			}

		} // namespace CppUnitTestFramework 
	} // namespace VisualStudio
} // namespace Microsoft

namespace UnitTest1
{
	TEST_CLASS(UnitTest) {

		TEST_METHOD(Simple) {
			setup();
			p_testee_ = new Testee_t(f);
			auto *p_testListener_ = new TestListener(*p_testee_);
			MyTest();
			delete p_testListener_;
			tearDown();
		}
		
		TEST_METHOD(Threaded) {
			printf("hier1");
			setup();
			auto *p_testListener_ = new CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::DefaultListener(*p_testee_);
			MyTest();
			p_testListener_->tearDown();
			printf("hier");
			delete p_testListener_;
			tearDown();
		}

	private:

		static const size_t N_ELEMENTS_IN_TESTFILE{ 8192u };
		typedef CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN> Testee_t;

		void setup() {
			errno_t err = fopen_s(&f, "testfile.bin", "rb");
			Assert::IsNotNull(f);
			Assert::AreEqual(0, err);
			p_testee_ = new Testee_t(f);
		}

		void tearDown() {
			delete p_testee_;
			fclose(f);
		}

		void MyTest() {
			using namespace std::chrono_literals;	
			Assert::AreEqual<size_t>(512u, p_testee_->top_, L"ungleich");
			TYPE_OF_DATA value;
			p_testee_->getCurrent(value);
			Assert::AreEqual<int>(0, value);
			TYPE_OF_DATA newValue;
			Testee_t::CacheState_t state = Testee_t::CacheState_t::OK;
			while (state == Testee_t::CacheState_t::OK) {
				state = p_testee_->getNext(newValue);
				Assert::AreEqual<TYPE_OF_DATA>(value + 1, newValue);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelUp() <= CACHE_LEN);
			}
			Assert::AreEqual(Testee_t::CacheState_t::END_OF_FILE, state);
			Assert::AreEqual<unsigned int>(N_ELEMENTS_IN_TESTFILE - 1, value);
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, p_testee_->top_, L"End of file");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, p_testee_->bottom_, L"End of file");
			state = p_testee_->getPrev(newValue);
			Assert::AreEqual(Testee_t::CacheState_t::OK, state);
			Assert::AreEqual<TYPE_OF_DATA>(newValue + 1, value);
			value = newValue;

			// Rückwärts eine halbe Cache-Länge lesen (-1 weil schon eins gelesen):
			for (unsigned int i = 0; i < CACHE_LEN / 2 - 2; i++) {
				state = p_testee_->getPrev(newValue);
				Assert::AreEqual<TYPE_OF_DATA>(newValue + 1, value);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, p_testee_->top_);
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, p_testee_->bottom_);

			// Rückwärts ein weiteres Cache-Viertel lesen:
			for (unsigned int i = 0; i < CACHE_LEN / 4; i++) {
				state = p_testee_->getPrev(newValue);
				Assert::AreEqual<TYPE_OF_DATA>(newValue + 1, value);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, p_testee_->top_, L"Inzwischen wurde nicht gefüllt");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, p_testee_->bottom_, L"Inzwischen wurde nicht gefüllt");
			// Das data_-Array enthält die <CACHE_LEN> höchsten Werte:
			// Beim nächsten sollte unten aufgefüllt werden
			state = p_testee_->getPrev(newValue);
			std::this_thread::sleep_for(10ms);  // TODO saubere Synchronisation
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN - CACHE_LEN / 4, p_testee_->bottom_, L"bottom_ um einen Viertel runtergewandert");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN / 4, p_testee_->top_);
			Assert::AreEqual<TYPE_OF_DATA>(newValue + 1, value);
			value = newValue;
			// Jetzt sollte das oberste Viertel des Caches mit den nächsten Daten gefüllt worden sein:
			// Rest abfragen
			while (state == Testee_t::CacheState_t::OK) {
				state = p_testee_->getPrev(newValue);
				Assert::AreEqual<TYPE_OF_DATA>(value - 1, newValue);
				value = newValue;
				Assert::IsTrue(p_testee_->fillLevelDown() <= CACHE_LEN);
			}
			Assert::AreEqual<size_t>(CACHE_LEN, p_testee_->top_, L"");
			Assert::AreEqual<size_t>(0, p_testee_->bottom_, L"");
		}

		/**
		 * Einfacher Testlistener, der die nötigen Aufrufe direkt aus dem Listener macht. In echt müsste
		 * der die Aufrufe aus einem anderen Thread machen
		 */
		class TestListener : public CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN>::IBackgroundTaskListener {
		public:

			TestListener(CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN> &testee) : testee_(testee) {
				testee_.setListener(this);
			}

			virtual ~TestListener() {}

			virtual void requestFill(bool up) override {
				if (up) {
					testee_.fillUpwards();
				}
				else {
					testee_.fillDownwards();
				}
			}

		private:
			
			CircularBidirectionalFilereaderBuffer<TYPE_OF_DATA, CACHE_LEN> &testee_;
		};

		FILE* f{ nullptr };
		Testee_t *p_testee_{ nullptr };
	};


}
