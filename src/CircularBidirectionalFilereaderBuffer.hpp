
#ifndef CIRCULARBIDIRECTIONALFILEREADERBUFFER_HPP_
#define CIRCULARBIDIRECTIONALFILEREADERBUFFER_HPP_

#include <stdio.h> // FILE
#include <limits>
#include <mutex>
#include <cassert>

namespace UnitTest1 {
	class UnitTest;
}

/**
 * Cache f�r das Lesen aus einer Datei
 * @tparam T Typ der Datenelemente. L�nge in Bytes muss eine Zweierpotenz sein (1, 2, 4, ....)
 * @tparam DATA_TUPLES_CHACHE_LENGTH Anzahl Elemente von T. Muss eine Zweierpotenz sein
 */
template <class T, unsigned int DATA_TUPLES_CHACHE_LENGTH>
class CircularBidirectionalFilereaderBuffer {

    public:

        /**
         * R�ckgabetyp f�r getNext und getPrev.
         */
        enum class CacheState_t {
            /** Alles gut */
            OK,
            /** Nur noch ein Wert �brig im Cache (aber noch mehr im File). Fill dringend n�tig. Sollte eigentlich nicht vorkommen in gut abgestimmtem System. */
            ALMOST_EMPTY,
            /** ele ist der letzte vorhandene Wert in dieser Richtung. Also der letzte in der Datei. */
            END_OF_FILE,
            /** Kein Wert mehr vorhanden */
            CACHE_OVERFLOW
        };

        /**
         * Interface zur Benachrichtigung, dass der Cache aufgef�llt werden soll. Also, das aus einem
         * Worker-Task CircularBidirectionalFilereaderBuffer<T, DATA_TUPLES_CHACHE_LENGTH>#fillUpwards oder CircularBidirectionalFilereaderBuffer<T, DATA_TUPLES_CHACHE_LENGTH>#fillDownwards aufgerufen werden soll, um neue Werte bereit zu machen.
         * @see DefaultListener Beispiel-/Default-Implementierung
         */
        class IBackgroundTaskListener {
        public:

            virtual ~IBackgroundTaskListener() {}

            /**
             * @param up true, wenn fillUpwards aufgerufen werden soll. false, wenn fillDownwards aufgerufen werden soll
             */
            virtual void requestFill(bool up) = 0;
        };

        /**
         * Diese Implementierung von IBackgroundTaskListener kann standardm�ssig verwendet werden.
         */
        class DefaultListener : public CircularBidirectionalFilereaderBuffer<T, DATA_TUPLES_CHACHE_LENGTH>::IBackgroundTaskListener {
        public:

            DefaultListener(CircularBidirectionalFilereaderBuffer<T, DATA_TUPLES_CHACHE_LENGTH> &theBuffer) : theBuffer_(theBuffer) {
                theBuffer_.setListener(this);
            }

            virtual ~DefaultListener() {
                assert(!thread_.joinable());  // vorher tearDown aufrufen
            }

            virtual void requestFill(bool up) override {
                fillRequestDirectionUp_ = up;
                cv.notify_one();
            }

            void tearDown() {
                keepRunning = false;
                cv.notify_one();
                thread_.join();
            }

        private:

            void run() {
                while (keepRunning) {
                    std::mutex mutex;  // eigenen Mutex, damit der des Buffers nicht rekursiv sein muss.
                    {
                        std::unique_lock<std::mutex> lock{mutex};
                        cv.wait(lock);
                        if (fillRequestDirectionUp_) {
                            theBuffer_.fillUpwards();
                        } else {
                            theBuffer_.fillDownwards();
                        }
                    }
                }
            }

            CircularBidirectionalFilereaderBuffer<T, DATA_TUPLES_CHACHE_LENGTH> &theBuffer_;
            std::thread thread_{&DefaultListener::run, this};
            std::condition_variable cv;
            bool keepRunning{true};
            bool fillRequestDirectionUp_{false};

        };

        /**
         * @param file zum Lesen ge�ffnete Datei
         * @param listener wird benachrichtigt, wenn der Cache aufgef�llt werden muss
         */
        CircularBidirectionalFilereaderBuffer(FILE* file) :
            file_(file) {
            static_assert(DATA_TUPLES_CHACHE_LENGTH && !(DATA_TUPLES_CHACHE_LENGTH & (DATA_TUPLES_CHACHE_LENGTH - 1)));  // Power of two
			static_assert(sizeof(T) && !(sizeof(T) & (sizeof(T) - 1)));
            initialize();
        }

        /**
        * Setzen des Listeners. Muss ausgef�hrt werden bevor #getNext oder #getPrev aufgerufen werden.
        */
        void setListener(IBackgroundTaskListener* l) {
            listener_ = l;
        }

        /**
         * Setzt den Cache zur�ck. Lesezeiger am Anfang des Caches. Cache bis zur H�lfte gef�llt mit Daten aus der Datei.
         */
        void initialize() {
            filePointer_ = 0;
            base_ = 0;
            bottom_ = 0;
            rewind(file_);
            top_ = fread(data_, sizeof(T), DATA_TUPLES_CHACHE_LENGTH / 2, file_);
            filePointer_ = top_;
        }
        
        /**
         * R�ckgabe des Elements an der aktuellen Position
         */
        void getCurrent(T& ele) const {
            ele = data_[base_];
        }

        /**
        * R�ckgabe des n�chsten Werts
        * @param[out] ele Der Wert
        * @return @see CacheState_t
        * @pre #setListener ausgef�hrt.
        */
        CacheState_t getNext(T& ele) {
            CacheState_t retVal{ CacheState_t::OK };
            bool requestFill{false};
            {
                std::lock_guard<std::mutex> lock{mutex_};
                base_ = (base_ + 1) % DATA_TUPLES_CHACHE_LENGTH;
                if (top_ == topOfFile_ && base_ == DATA_TUPLES_CHACHE_LENGTH - 1) {
                    retVal = CacheState_t::END_OF_FILE;
                } else if (top_ > topOfFile_) {
                    retVal = CacheState_t::CACHE_OVERFLOW;
                } else if (fillLevelUp() <= 0 && top_ != topOfFile_) {  // FillLevel erneut abfragen; k�nnte schon ge�ndert haben (wenn requestFill den fill im gleichen Kontext aufruft.
                    retVal = CacheState_t::ALMOST_EMPTY;
                }
                ele = data_[base_];
                requestFill = fillLevelUp() < DATA_TUPLES_CHACHE_LENGTH / 4;
            }  // lock scope
            if (requestFill) {
                listener_->requestFill(true);
            }
            return retVal;
        }

        /**
        * R�ckgabe des vorherigen Werts
        * @param[out] ele Der Wert
        * @return @see CacheState_t
        */
        CacheState_t getPrev(T& ele) {
            CacheState_t retVal{ CacheState_t::OK };
            bool requestFill{false};
            {
                std::lock_guard<std::mutex> lock{mutex_};
                if (bottom_ == 0 && base_ == 0) {
                    retVal = retVal = CacheState_t::CACHE_OVERFLOW;
                } else {
                    base_ = (base_ + ((DATA_TUPLES_CHACHE_LENGTH - 1))) % DATA_TUPLES_CHACHE_LENGTH;
                    ele = data_[base_];
                    if (bottom_ == 0 && base_ == 0) {
                        retVal = CacheState_t::END_OF_FILE;
                    } else if (fillLevelDown() <= 1 && bottom_ > 0) {
                        retVal = CacheState_t::ALMOST_EMPTY;
                    }
                    requestFill = fillLevelDown() < DATA_TUPLES_CHACHE_LENGTH / 4 && bottom_ > 0;
                }
            }  // lock scope
            if (requestFill) {
                listener_->requestFill(false);
            }
            return retVal;
        }

        /** F�llt den Cache aufw�rts um einen Viertel der Gesamtl�nge. */
        void fillUpwards() {
            std::lock_guard<std::mutex> lock{mutex_};
            if (fillLevelUp() < DATA_TUPLES_CHACHE_LENGTH / 4) {  // doppelte fillUpwards - Aufrufe abfangen
                size_t top_in_cache = top_ & (DATA_TUPLES_CHACHE_LENGTH - 1);
                size_t space_in_cache = (DATA_TUPLES_CHACHE_LENGTH - top_in_cache) % DATA_TUPLES_CHACHE_LENGTH;
                size_t remaining{0};  // Anzahl, die nach Erreichen der Decke des Caches, am Anfang noch eingef�gt werden m�ssen
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
        
        /** F�llt den Cache abw�rts um einen Viertel der Gesamtl�nge. */
        void fillDownwards() {
            std::lock_guard<std::mutex> lock{mutex_};
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
                const size_t newBottom = bottom_ - nToCopy;
                const int offset = -static_cast<int>(filePointer_ - newBottom);
                fseek(file_, offset * sizeof(T), SEEK_CUR);
                bottom_ -= fread(data_ + destPtrInCache, sizeof(T), nToCopy, file_);
                // Im zweiten Schritt am oberen Ende den Rest einf�llen
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
        friend class DefaultListener;  // Zugriff auf mutex_

        /** Anzahl Elemente im Cache in Aufw�rts-Richtung. */
        size_t fillLevelUp() const {
            return ((top_ % DATA_TUPLES_CHACHE_LENGTH) + DATA_TUPLES_CHACHE_LENGTH - base_) % DATA_TUPLES_CHACHE_LENGTH;
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
                assert(topOfFile_ == std::numeric_limits<unsigned int>::max());  // sollte nur 1x hier reinkommen.
                topOfFile_ = top_ + nRead;
            }
            return nRead;
        }

        T data_[DATA_TUPLES_CHACHE_LENGTH];
        FILE *file_;
        IBackgroundTaskListener *listener_;
        /** Totale Anzahl Elemente im File. Wird runtergesetzt, sobald EOF erreicht wird. */
        size_t topOfFile_{ std::numeric_limits<unsigned int>::max() };
        /** Lese-Pointer im Cache. Index, der bei getNext ausgegeben wird. */
        size_t base_;
        /** Lese-Pointer in der Datei. Merkt sich, wo der Lese-Pointer der ge�ffneten Datei steht. Eigentlich das, was ftell zur�ckgeben w�rde. */
        size_t filePointer_;
        /** h�chster Element-Index aus der Datei, der im Cache. Genauer gesagt: 1 h�her als der oberste, g�ltige Wert. */
        size_t top_;
        /** Niedrigster Element-Index aus der Datei, der im Cache gespeichert ist. */
        size_t bottom_;
        std::mutex mutex_;
};

#endif
