#include "platform/bootloader_request.h"

#include <cassert>

int main()
{
	volatile bootloader_request request{
		bootloader_request_magic,
		~bootloader_request_magic
	};
	assert(consume_bootloader_request(request, true));
	assert(request.magic == 0 && request.inverse == 0);
	assert(!consume_bootloader_request(request, true));
	request.magic = bootloader_request_magic;
	request.inverse = ~bootloader_request_magic;
	assert(!consume_bootloader_request(request, false));
	assert(!consume_bootloader_request(request, true));
	request.magic = bootloader_request_magic;
	request.inverse = 0;
	assert(!consume_bootloader_request(request, true));
	request.magic = 0;
	request.inverse = ~bootloader_request_magic;
	assert(!consume_bootloader_request(request, true));
}
