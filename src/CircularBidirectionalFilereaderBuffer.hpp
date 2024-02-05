
#ifndef CIRCULARBIDIRECTIONALFILEREADERBUFFER_HPP_
#define CIRCULARBIDIRECTIONALFILEREADERBUFFER_HPP_

#include <stdio.h> // FILE
#include <limits>
#include <mutex>

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
         * OK Alles gut
s         * ALMOST_EMPTY Nur noch ein Wert übrig im Cache (aber noch mehr im File). Fill dringend nötig. Sollte eigentlich nicht vorkommen in gut abgestimmtem System.
         * END_OF_FILE ele ist der letzte vorhandene Wert in dieser Richtung. Also der letzte in der Datei.
         * CACHE_OVERFLOWB Kein Wert mehr vorhanden
         */
        enum class CacheState_t {
            OK,
            ALMOST_EMPTY,
            END_OF_FILE,
            CACHE_OVERFLOW
        };

        /**
         * Interface zur Benachrichtigung, dass der Cache aufgefüllt werden soll. Also, das aus einem
         * Worker-Task fillUpwards oder fillDownwards aufgerufen werden soll, um neue Werte bereit zu machen
         */
        class IBackgroundTaskListener {
        public:

            virtual ~IBackgroundTaskListener() {}

            /**
             * @param up true, wenn fillUpwards aufgerufen werden soll. false, wenn fillDownwards aufgerufen werden soll
             */
            virtual void requestFill(bool up) = 0;

            virtual bool initialize(CircularBidirectionalFilereaderBuffer<int, 1024>*) = 0;
        };

        /**
         * @param file zum Lesen geöffnete Datei
         * @param listener wird benachrichtigt, wenn der Cache aufgefüllt werden muss
         */
        CircularBidirectionalFilereaderBuffer(FILE* file, IBackgroundTaskListener& listener) :
            file_(file), listener_(listener) {
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
        
        void getCurrent(T& ele) const {
            ele = data_[base_];
        }

        /**
        * @return @see CacheState_t
        */
        CacheState_t getNext(T& ele) {
            CacheState_t retVal{ CacheState_t::OK };
            std::lock_guard<std::recursive_mutex> lock{ mutex_ };
            base_ = (base_ + 1) % DATA_TUPLES_CHACHE_LENGTH;
            if (fillLevelUp() < DATA_TUPLES_CHACHE_LENGTH / 4) {
                listener_.requestFill(true);
            }
            if (top_ == topOfFile_ && base_ == DATA_TUPLES_CHACHE_LENGTH - 1) {
                retVal = CacheState_t::END_OF_FILE;
            }
            else if (top_ > topOfFile_) {
                retVal = CacheState_t::CACHE_OVERFLOW;
            }
            else if (fillLevelUp() <= 0 && top_ != topOfFile_) {  // FillLevel erneut abfragen; könnte schon geändert haben (wenn requestFill den fill im gleichen Kontext aufruft.
                retVal = CacheState_t::ALMOST_EMPTY;
            }
            ele = data_[base_];
            return retVal;
        }

        /**
         * @return @see CacheState_t
         */
        CacheState_t getPrev(T& ele) {
            CacheState_t retVal{ CacheState_t::OK };
            std::lock_guard<std::recursive_mutex> lock{ mutex_ };
            if (bottom_ == 0 && base_ == 0) {
                retVal = retVal = CacheState_t::CACHE_OVERFLOW;
            }
            else {
                base_ = (base_ + ((DATA_TUPLES_CHACHE_LENGTH - 1))) % DATA_TUPLES_CHACHE_LENGTH;
                ele = data_[base_];
                if (fillLevelDown() < DATA_TUPLES_CHACHE_LENGTH / 4 && bottom_ > 0) {
                    listener_.requestFill(false);
                }
                if (bottom_ == 0 && base_ == 0) {
                    retVal = CacheState_t::END_OF_FILE;
                }
                else if (fillLevelDown() <= 1 && bottom_ > 0) {
                    retVal = CacheState_t::ALMOST_EMPTY;
                }
            }
            return retVal;
        }

        /** Füllt den Cache aufwärts um einen Viertel der Gesamtlänge. */
        void fillUpwards() {
            std::lock_guard<std::recursive_mutex> lock{mutex_};
            if (fillLevelUp() < DATA_TUPLES_CHACHE_LENGTH / 4) {  // doppelte fillUpwards - Aufrufe abfangen
                size_t top_in_cache = top_ & (DATA_TUPLES_CHACHE_LENGTH - 1);
                size_t space_in_cache = (DATA_TUPLES_CHACHE_LENGTH - top_in_cache) % DATA_TUPLES_CHACHE_LENGTH;
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
        }
        
        /** Füllt den Cache abwärts um einen Viertel der Gesamtlänge. */
        void fillDownwards() {
            std::lock_guard<std::recursive_mutex> lock{mutex_};
            if (fillLevelDown() < DATA_TUPLES_CHACHE_LENGTH / 4) {  // doppelte fillDownwards - Aufrufe abfangen
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
        }

     private:

        friend class UnitTest1::UnitTest;

        /** Anzahl Elemente im Cache in Aufwärts-Richtung. */
        size_t fillLevelUp() const {
            // Casting zu int, damit Überlauf-Arithmetik definiert funktioniert (Überlauf bei unsigned wäre UB):.
            return static_cast<size_t>((static_cast<int>(top_ % DATA_TUPLES_CHACHE_LENGTH) - static_cast<int>(base_) + DATA_TUPLES_CHACHE_LENGTH) % DATA_TUPLES_CHACHE_LENGTH);
        }

        size_t fillLevelDown() const {
            return static_cast<size_t>(static_cast<int>(base_) - static_cast<int>(bottom_)) & (DATA_TUPLES_CHACHE_LENGTH - 1);
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
        IBackgroundTaskListener& listener_;
        /** Totale Anzahl Elemente im File. Wird runtergesetzt, sobald EOF erreicht wird. */
        size_t topOfFile_{ std::numeric_limits<unsigned int>::max() };
        /** Lese-Pointer im Cache */
        size_t base_;
        /** Lese-Pointer in der Datei */
        size_t filePointer_;
        /** höchster Element-Index im Cache */
        size_t top_;
        size_t bottom_;
        std::recursive_mutex mutex_;  // rekursiv für den Fall, wenn fill* direkt aus dem fillRequest aufgerufen wird
};

#endif
