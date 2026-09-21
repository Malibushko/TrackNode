extern "C" {
#include <ff.h>
#include <diskio.h>
}

#include "libs/fatfs/sd_diskio.h"
#include "drivers/devices/sd_card.h"

namespace
{
constexpr BYTE sd_drive = 0;
sd_card* card = nullptr;
}

void bind_sd_card(sd_card& instance)
{
	card = &instance;
}

void unbind_sd_card(sd_card& instance)
{
	if (card == &instance)
	{
		card = nullptr;
	}
}

extern "C" DSTATUS disk_status(BYTE pdrv)
{
	if (pdrv != sd_drive || card == nullptr) {
		return STA_NOINIT;
	}

	return 0;
}

extern "C" DSTATUS disk_initialize(BYTE pdrv)
{
	return disk_status(pdrv);
}

extern "C" DRESULT disk_read(BYTE pdrv, BYTE* buffer, LBA_t sector, UINT count)
{
	if (pdrv != sd_drive || buffer == nullptr || count == 0)
	{
		return RES_PARERR;
	}

	if (card == nullptr)
	{
		return RES_NOTRDY;
	}


	for (UINT i = 0; i < count; i++)
	{
		sd_card::r1_t response;

		std::span<uint8_t> block{buffer + i * 512, 512};

		if (!card->read_single_block(static_cast<uint32_t>(sector + i), block, response))
		{
			return RES_ERROR;
		}
	}

	return RES_OK;
}

extern "C" DRESULT disk_write(BYTE pdrv, const BYTE* buffer, LBA_t sector, UINT count)
{
	if (pdrv != sd_drive || buffer == nullptr || count == 0)
	{
		return RES_PARERR;
	}

	if (card == nullptr)
	{
		return RES_NOTRDY;
	}

	for (UINT i = 0; i < count; ++i)
	{
		sd_card::r1_t response;

		std::span<const uint8_t> block{ buffer + i * 512, 512 };

		if (!card->write_block(static_cast<uint32_t>(sector + i), block, response))
		{
			return RES_ERROR;
		}
	}

	return RES_OK;
}

extern "C" DRESULT disk_ioctl(BYTE pdrv, BYTE command, void* buffer)
{
	if (pdrv != sd_drive) {
		return RES_PARERR;
	}

	if (card == nullptr)
	{
		return RES_NOTRDY;
	}

	switch (command)
	{
		case CTRL_SYNC:
		{
			return RES_OK;
		}

		case GET_SECTOR_SIZE:
		{
			if (buffer == nullptr)
			{
				return RES_PARERR;
			}
			*static_cast<WORD*>(buffer) = 512;

			return RES_OK;
		}

		case GET_SECTOR_COUNT:
		{
			if (buffer == nullptr)
			{
				return RES_PARERR;
			}
			uint64_t sector_count{};
			if (auto status = card->sector_count(sector_count); !status)
			{
				return RES_ERROR;
			}

			*static_cast<LBA_t*>(buffer) = sector_count;
			return RES_OK;
		}

		case GET_BLOCK_SIZE:
		{
			if (buffer == nullptr)
			{
				return RES_PARERR;
			}
			*static_cast<LBA_t*>(buffer) = DWORD{1};
			return RES_OK;
		}

		default:
		{
			return RES_PARERR;
		}
	}

	return RES_OK;
}