# Circular Bidirectional Filereader Buffer



This class allows to sequentially read data elements from a file forwards and backwards. While you read data elements with `getNext` and `getPrev`, a background task ensures that a certain number (a fourth of CHACHE_LEN in both directions) of data elements is loaded into the RAM buffer.
This class can be useful in embedded systems with a limited amount of RAM when you have to read big files. The ability to go in both directions can be useful if you need your data not in a linear and steady but some kind of _noisy_ way.

Example:

    typedef struct {
		float xValue;
		float yValue;
	} myDataType;

	typedef CircularBidirectionalFilereaderBuffer<myDataType, 1024> myBufferType;

	FILE *f = fopen("data.bin", "rb");  // file must contain values of type myDataType
	myBufferType buffer{f};
	myBufferType::DefaultListener workerTask{buffer};

	myDataType element;

	buffer.getCurrent(element);
	while (true) {
		buffer.getNext(element);
		...  // do stuff
	}