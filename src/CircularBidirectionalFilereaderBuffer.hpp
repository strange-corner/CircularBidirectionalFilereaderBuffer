
#ifndef CIRCULARBIDIRECTIONALFILEREADERBUFFER_HPP_
#define CIRCULARBIDIRECTIONALFILEREADERBUFFER_HPP_

#include <stdio.h> // FILE
#include <limits>

namespace UnitTest1 {
	class UnitTest;
}

/**
 * Cache für das Lesen aus einer Datei
 * @tparam T Typ der Datenelemente. Länge in Bytes muss eine Zweierpotenz sein (1, 2, 4, ....)
 * @tparam DATA_TUPLES_CHACHE_LENGTH Anzahl Elemente von T. Muss eine Zweierpotenz sein
 */
template <class T, unsigned int DATA_TUPLES_CHACHE_LENGTH>
class CircularBidirectionalFilereaderBuffer {

    public:

        /**
         * @param file zum Lesen geöffnete Datei
         */
        CircularBidirectionalFilereaderBuffer(FILE *file) :  file_(file) {
            static_assert(DATA_TUPLES_CHACHE_LENGTH && !(DATA_TUPLES_CHACHE_LENGTH & (DATA_TUPLES_CHACHE_LENGTH - 1)));  // Power of two
			static_assert(sizeof(T) && !(sizeof(T) & (sizeof(T) - 1)));
            initialize();
        }

        void initialize() {
            filePointer_ = 0;
            base_ = 0;
            bottom_ = 0;
            rewind(file_);
            top_ = fread(data_, sizeof(T), DATA_TUPLES_CHACHE_LENGTH / 2, file_);
            filePointer_ = top_;
        }
        
        bool getNext(T &ele) {
            ele = data_[base_];
            base_ = (base_ + 1) % DATA_TUPLES_CHACHE_LENGTH;
            if (fillLevelUp() < DATA_TUPLES_CHACHE_LENGTH / 4) {
                fillUpwards();
            }
            return top_ < topOfFile_;  // TODO noch nicht ganz richtig
        }
        
        bool getPrev(T &ele) {
            base_ = (base_ + ((DATA_TUPLES_CHACHE_LENGTH - 1))) % DATA_TUPLES_CHACHE_LENGTH;
            ele = data_[base_];
            if (fillLevelDown() < DATA_TUPLES_CHACHE_LENGTH / 4) {
                fillDownwards();
            }
            return bottom_ > 0;  // TODO noch nicht ganz richtig
        }

    private:

		friend class UnitTest1::UnitTest;

        unsigned int base() {return filePointer_ & (DATA_TUPLES_CHACHE_LENGTH - 1);}

        /** Anzahl Elemente im Cache in Aufwärts-Richtung. */
        size_t fillLevelUp() {
            // Casting zu int, damit Überlauf-Arithmetik definiert funktioniert (Überlauf bei unsigned wäre UB):.
            return static_cast<size_t>(static_cast<int>(top_ & (DATA_TUPLES_CHACHE_LENGTH - 1)) - static_cast<int>(base_));
        }
        
        size_t fillLevelDown() {
            return static_cast<size_t>(static_cast<int>(base_) - static_cast<int>(bottom_)) & (DATA_TUPLES_CHACHE_LENGTH - 1);
        }

        void fillUpwards() {
            size_t top_in_cache = top_ & (DATA_TUPLES_CHACHE_LENGTH - 1);
            size_t space_in_cache = DATA_TUPLES_CHACHE_LENGTH - top_in_cache;
            size_t remaining{0};  // Anzahl, die nach Erreichen der Decke des Caches, am Anfang noch eingefügt werden müssen
            if (space_in_cache < DATA_TUPLES_CHACHE_LENGTH / 4) {
                remaining = DATA_TUPLES_CHACHE_LENGTH / 4 - space_in_cache;
            }
            top_ += read_with_eof_check(data_ + top_in_cache, sizeof(T), space_in_cache, file_);
            if (remaining > 0) {
                top_ += read_with_eof_check(data_, sizeof(T), remaining, file_);
            }
            filePointer_ = top_;
            bottom_ = top_ - DATA_TUPLES_CHACHE_LENGTH;
        }
        
        void fillDownwards() {
            size_t bottom_in_cache = bottom_ & (DATA_TUPLES_CHACHE_LENGTH - 1);
            size_t space_in_cache = bottom_in_cache;
            size_t remaining{0};
            size_t destPtrInCache;
            size_t nToCopy;
            if (space_in_cache < DATA_TUPLES_CHACHE_LENGTH / 4) {
				// there is less than a quarter space left in chache in down direction
                remaining = DATA_TUPLES_CHACHE_LENGTH / 4 - space_in_cache;
                destPtrInCache = 0;
                nToCopy = space_in_cache;
            } else {
				// there is plenty of space left in down direction
                destPtrInCache = bottom_in_cache - DATA_TUPLES_CHACHE_LENGTH / 4;
                nToCopy = DATA_TUPLES_CHACHE_LENGTH / 4;
            }
            size_t newBottom = bottom_ - nToCopy;
            int offset = -static_cast<int>(filePointer_ - newBottom);
            fseek(file_, offset * sizeof(T), SEEK_CUR);
            bottom_ -= fread(data_ + destPtrInCache, sizeof(T), nToCopy, file_);
            if (remaining > 0) {
                fseek(file_, -static_cast<int>(nToCopy + remaining) * sizeof(T), SEEK_CUR);
                bottom_ -= fread(data_ + DATA_TUPLES_CHACHE_LENGTH - remaining, sizeof(T), remaining, file_);
            }
            top_ = bottom_ + DATA_TUPLES_CHACHE_LENGTH;
            filePointer_ = bottom_ + DATA_TUPLES_CHACHE_LENGTH / 4;
        }
        
        size_t read_with_eof_check(void *data, size_t elementSize, size_t N, FILE *f) {
            if (N > topOfFile_ - top_) {
                N = topOfFile_ - top_;
            }
            size_t nRead = fread(data, elementSize, N, f);
            if (nRead < N) {
                _ASSERT(topOfFile_ == std::numeric_limits<unsigned int>::max());  // sollte nur 1x hier reinkommen.
                topOfFile_ = top_ + nRead;
            }
            return nRead;
        }

        T data_[DATA_TUPLES_CHACHE_LENGTH];
        FILE *file_;
        /** Totale Anzahl Elemente im File. Wird runtergesetzt, sobald EOF erreicht wird. */
        unsigned int topOfFile_{ std::numeric_limits<unsigned int>::max() };
        /** Lese-Pointer im Cache */
        size_t base_;
        /** Lese-Pointer in der Datei */
        size_t filePointer_;
        /** höchster Element-Index im Cache */
        unsigned int top_;
        size_t bottom_;
};

#endif
