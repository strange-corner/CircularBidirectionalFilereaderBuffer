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

		TEST_METHOD(Method1) {

			const unsigned int N_ELEMENTS_IN_TESTFILE{8192};
			const unsigned int CACHE_LEN{ 1024 };
			FILE *f;
			errno_t err = fopen_s(&f, "testfile.bin", "rb");
			Assert::AreEqual(NULL, err);
			TestListener testListener;
			typedef CircularBidirectionalFilereaderBuffer<int, CACHE_LEN> Testee_t;
			Testee_t testee{ f, testListener };
			testListener.p_testee_ = &testee;
			Assert::AreEqual<size_t>(512u, testee.top(), L"ungleich");
			int value;
			Testee_t::CacheState_t state = testee.getNext(value);
			Assert::AreEqual(Testee_t::CacheState_t::OK, state);
			Assert::AreEqual<int>(0, value);
			int newValue;
			while (state == Testee_t::CacheState_t::OK) {
				printf("%d", value);
				state = testee.getNext(newValue);
				Assert::AreEqual<int>(value + 1, newValue);
				value = newValue;
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, testee.top(), L"End of file");
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, testee.bottom(), L"End of file");
			state = testee.getPrev(value);
			Assert::AreEqual(Testee_t::CacheState_t::OK, state);
			Assert::AreEqual<unsigned int>(N_ELEMENTS_IN_TESTFILE - 1, value);
			
			// Rückwärts eine halbe Cache-Länge lesen:
			for (unsigned int i = 0; i < CACHE_LEN / 2 - 1; i++) {
				state = testee.getPrev(newValue);
				Assert::AreEqual<int>(newValue + 1, value);
				value = newValue;
			}
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE, testee.top());
			Assert::AreEqual<size_t>(N_ELEMENTS_IN_TESTFILE - CACHE_LEN, testee.bottom());

			// Rückwärts ein weiteres Cache-Viertel lesen:
			for (unsigned int i = 0; i < CACHE_LEN / 4; i++) {
				state = testee.getPrev(newValue);
				Assert::AreEqual<int>(newValue + 1, value);
				value = newValue;
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
				printf("%d", value);
				state = testee.getPrev(newValue);
				Assert::AreEqual<int>(value - 1, newValue);
				value = newValue;
			}
			Assert::AreEqual<size_t>(CACHE_LEN, testee.top(), L"");
			Assert::AreEqual<size_t>(0, testee.bottom(), L"");

			fclose(f);

		}

	private:

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

			CircularBidirectionalFilereaderBuffer<int, 1024>* p_testee_;
		};

	};


}
