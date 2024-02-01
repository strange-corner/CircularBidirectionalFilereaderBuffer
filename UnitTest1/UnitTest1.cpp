#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "CircularBidirectionalFilereaderBuffer.hpp"

namespace UnitTest1
{
	TEST_CLASS(UnitTest) {

		TEST_METHOD(Method1) {

			const unsigned int CACHE_LEN{ 1024 };
			FILE *f;
			errno_t err = fopen_s(&f, "testfile.bin", "rb");
			Assert::AreEqual(NULL, err);
			CircularBidirectionalFilereaderBuffer<int, CACHE_LEN> testee{ f };
			Assert::AreEqual<unsigned int>(512u, testee.top_, L"ungleich");
			int value;
			bool ok = testee.getNext(value);
			Assert::IsTrue(ok);
			Assert::AreEqual<int>(0, value);
			while (ok) {
				printf("%d", value);
				int newValue;
				ok = testee.getNext(newValue);
				Assert::AreEqual<int>(value + 1, newValue);
				value = newValue;
			}
			Assert::AreEqual<unsigned int>(2048, testee.top_, L"End of file");
			Assert::AreEqual<unsigned int>(2048 - CACHE_LEN, testee.bottom_, L"End of file");
			ok = testee.getPrev(value);
			Assert::IsTrue(ok);
			Assert::AreEqual<unsigned int>(2047, value);
			while (ok) {
				printf("%d", value);
				int newValue;
				ok = testee.getPrev(newValue);
				Assert::AreEqual<int>(value - 1, newValue);
				value = newValue;
			}
			Assert::AreEqual<unsigned int>(CACHE_LEN, testee.top_, L"");
			Assert::AreEqual<unsigned int>(0, testee.bottom_, L"");

			fclose(f);

		}

	};

}
